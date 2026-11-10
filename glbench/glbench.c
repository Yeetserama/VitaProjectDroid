/*
 * glbench -- EGL surfaceless + GLES2 fill-rate probe for the PlayStation Vita.
 *
 * Renders 960x544 into an offscreen FBO with Mesa's software rasterizer,
 * times a few UI-like scenes, and optionally presents the result to
 * /dev/graphics/fb0 (RGBA readback, row flip, optional R/B swap).
 *
 * This is a hardware gate, not a framework component: it answers "is a
 * software GLES2 renderer fast enough for a 960x544 UI on this SoC?"
 * before any Mesa-for-bionic or SurfaceFlinger work is attempted.
 *
 * Build (armhf glibc, against a softpipe-only Mesa):
 *   arm-linux-gnueabihf-gcc -O2 -march=armv7-a -mfpu=neon \
 *       -mfloat-abi=hard glbench.c -o glbench -lEGL -lGLESv2 -lm
 */

#define _GNU_SOURCE

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_WIDTH   960
#define DEFAULT_HEIGHT  544
#define DEFAULT_FRAMES  120
#define DEFAULT_UI_FRAMES 60
#define DEFAULT_GATE_FPS 20
#define DEFAULT_FB_PATH "/dev/graphics/fb0"
#define UI_QUADS        160

typedef struct {
    int width;
    int height;
    int frames;
    int ui_frames;
    int gate_fps;
    int hold_s;
    int repeats;
    const char *scene;      /* NULL means all scenes */
    const char *fb_path;
    int fb_swap_rb;         /* 1 = write BGRA, 0 = write RGBA (Vita default) */
    int present;
    int json;
} options;

typedef struct {
    const char *name;
    int frames;
    double ms;
    double fps;
    int ran;
} scene_result;

typedef struct {
    int fd;
    uint8_t *mem;
    size_t len;
    int width;
    int height;
    int stride;
    int bpp;
    char path[256];
} fbdev;

static int g_width;
static int g_height;
static GLuint g_vbo;
static GLuint g_tex;
static GLuint g_fbo;
static GLuint g_color;
static GLuint g_fill_prog;
static GLuint g_tex_prog;
static GLint g_fill_rect = -1, g_fill_res = -1, g_fill_color = -1;
static GLint g_tex_rect = -1, g_tex_res = -1, g_tex_color = -1, g_tex_sampler = -1;
static int g_json;

static void die(const char *fmt, ...)
{
    va_list ap;

    fputs("glbench: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

/* Diagnostics go to stdout for humans and to stderr for --json, so that
 * stdout stays valid JSON. */
static void info(const char *fmt, ...)
{
    va_list ap;
    FILE *out = g_json ? stderr : stdout;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
}

static double now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static const char *egl_error_string(EGLint e)
{
    switch (e) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
    default: return "EGL_<unknown>";
    }
}

static void check_egl(const char *what)
{
    EGLint e = eglGetError();

    if (e != EGL_SUCCESS)
        die("%s failed: %s (0x%04x)", what, egl_error_string(e), (unsigned)e);
}

static void check_gl(const char *what)
{
    GLenum e = glGetError();

    if (e != GL_NO_ERROR) {
        fprintf(stderr, "glbench: GL error 0x%04x at %s\n", (unsigned)e, what);
    }
}

static GLuint compile_shader(GLenum type, const char *src, const char *name)
{
    GLuint sh = glCreateShader(type);
    const GLchar *sources[1] = { src };
    GLint ok = 0;
    char log[4096];

    glShaderSource(sh, 1, sources, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(sh, (GLsizei)sizeof(log), NULL, log);
        die("%s shader failed to compile:\n%s", name, log);
    }
    return sh;
}

static GLuint link_program(const char *vs_src, const char *fs_src,
                           const char *name)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src, name);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, name);
    GLuint prog = glCreateProgram();
    GLint ok = 0;
    char log[4096];

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(prog, (GLsizei)sizeof(log), NULL, log);
        die("%s program failed to link:\n%s", name, log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static const char *vertex_shader =
    "attribute vec2 a_pos;\n"
    "uniform vec4 u_rect;\n"
    "uniform vec2 u_res;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_pos;\n"
    "    vec2 px = u_rect.xy + a_pos * u_rect.zw;\n"
    "    vec2 ndc = px / u_res * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

static const char *fill_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(u_color.rgb * (0.75 + 0.25 * v_uv.x), u_color.a);\n"
    "}\n";

static const char *tex_fragment_shader =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv) * u_color;\n"
    "}\n";

static void make_checker_texture(void)
{
    enum { N = 256 };
    static uint32_t pixels[N * N];
    int x, y;

    for (y = 0; y < N; y++) {
        for (x = 0; x < N; x++) {
            /* Word is 0xAABBGGRR; bytes in memory are R,G,B,A. */
            pixels[y * N + x] = ((((x >> 5) + (y >> 5)) & 1)
                                 ? 0xfff7eee8u : 0xff503e2cu);
        }
    }

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    check_gl("checker texture");
}

static void make_fbo(int width, int height)
{
    GLenum status;

    glGenTextures(1, &g_color);
    glBindTexture(GL_TEXTURE_2D, g_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_color, 0);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        die("FBO %dx%d is incomplete (0x%04x)", width, height, (unsigned)status);
    check_gl("FBO setup");
}

static void init_gl(int width, int height)
{
    static const GLfloat quad[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f,
    };

    g_width = width;
    g_height = height;

    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

    g_fill_prog = link_program(vertex_shader, fill_fragment_shader, "fill");
    g_tex_prog = link_program(vertex_shader, tex_fragment_shader, "tex");

    g_fill_rect = glGetUniformLocation(g_fill_prog, "u_rect");
    g_fill_res = glGetUniformLocation(g_fill_prog, "u_res");
    g_fill_color = glGetUniformLocation(g_fill_prog, "u_color");

    g_tex_rect = glGetUniformLocation(g_tex_prog, "u_rect");
    g_tex_res = glGetUniformLocation(g_tex_prog, "u_res");
    g_tex_color = glGetUniformLocation(g_tex_prog, "u_color");
    g_tex_sampler = glGetUniformLocation(g_tex_prog, "u_tex");

    make_checker_texture();
    make_fbo(width, height);
    check_gl("GL init");
}

static void draw_quad(GLuint prog, GLint rect, GLint res, GLint color,
                      float x, float y, float w, float h,
                      float r, float g, float b, float a)
{
    glUseProgram(prog);
    glUniform4f(rect, x, y, w, h);
    glUniform2f(res, (float)g_width, (float)g_height);
    glUniform4f(color, r, g, b, a);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void draw_ui_frame(void)
{
    int i;

    glUseProgram(g_tex_prog);
    glUniform2f(g_tex_res, (float)g_width, (float)g_height);
    glUniform1i(g_tex_sampler, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (i = 0; i < UI_QUADS; i++) {
        int x = (i * 97) % (g_width - 48 > 0 ? g_width - 48 : 1);
        int y = (i * 61) % (g_height - 48 > 0 ? g_height - 48 : 1);
        int w = 48 + (i % 4) * 16;
        int h = 32 + (i % 3) * 16;
        float alpha = 0.30f + 0.55f * (float)(i % 7) / 6.0f;
        float r = 0.30f + 0.70f * (float)((i * 37) % 100) / 100.0f;
        float g = 0.30f + 0.70f * (float)((i * 53) % 100) / 100.0f;
        float b = 0.30f + 0.70f * (float)((i * 71) % 100) / 100.0f;

        glUniform4f(g_tex_rect, (float)x, (float)y, (float)w, (float)h);
        glUniform4f(g_tex_color, r, g, b, alpha);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisable(GL_BLEND);
}

static void render_scene(const char *scene, int frame)
{
    float pulse = (float)(frame % 120) / 120.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, g_width, g_height);
    glClearColor(0.04f + 0.02f * pulse, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (strcmp(scene, "clear") == 0) {
        return;
    }

    if (strcmp(scene, "fill") == 0) {
        draw_quad(g_fill_prog, g_fill_rect, g_fill_res, g_fill_color,
                  0.0f, 0.0f, (float)g_width, (float)g_height,
                  0.20f, 0.55f, 0.90f, 1.0f);
        return;
    }

    if (strcmp(scene, "tex") == 0) {
        glUseProgram(g_tex_prog);
        glUniform2f(g_tex_res, (float)g_width, (float)g_height);
        glUniform1i(g_tex_sampler, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glUniform4f(g_tex_rect, 0.0f, 0.0f, (float)g_width, (float)g_height);
        glUniform4f(g_tex_color, 1.0f, 1.0f, 1.0f, 1.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        return;
    }

    if (strcmp(scene, "ui") == 0) {
        draw_quad(g_fill_prog, g_fill_rect, g_fill_res, g_fill_color,
                  0.0f, 0.0f, (float)g_width, (float)g_height,
                  0.10f, 0.12f, 0.18f, 1.0f);
        draw_ui_frame();
        return;
    }

    die("unknown scene: %s", scene);
}

static double bench_scene(const char *scene, int frames)
{
    int i;
    double t0, t1;

    if (frames < 1)
        frames = 1;

    render_scene(scene, 0);
    glFinish();

    t0 = now_ms();
    for (i = 0; i < frames; i++) {
        render_scene(scene, i);
        glFlush();
    }
    glFinish();
    t1 = now_ms();

    return (t1 - t0) / (double)frames;
}

static int fb_open(fbdev *fb, const char *path)
{
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    size_t len;
    void *mem;
    int fd;

    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
    snprintf(fb->path, sizeof(fb->path), "%s", path);

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "glbench: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) != 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fi) != 0) {
        fprintf(stderr, "glbench: %s is not an fbdev: %s\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    len = fi.smem_len;
    if (len == 0)
        len = (size_t)fi.line_length * vi.yres_virtual;

    mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "glbench: mmap %s failed: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    fb->fd = fd;
    fb->mem = mem;
    fb->len = len;
    fb->width = (int)vi.xres;
    fb->height = (int)vi.yres;
    fb->stride = (int)fi.line_length;
    fb->bpp = (int)vi.bits_per_pixel;

    if (fb->stride <= 0)
        fb->stride = fb->width * ((fb->bpp + 7) / 8);

    info("glbench: fb0 %dx%d, %d bpp, stride %d, smem %lu\n",
         fb->width, fb->height, fb->bpp, fb->stride,
         (unsigned long)fb->len);
    return 0;
}

static void fb_close(fbdev *fb)
{
    if (fb->mem && fb->mem != MAP_FAILED)
        munmap(fb->mem, fb->len);
    if (fb->fd >= 0)
        close(fb->fd);
    fb->mem = NULL;
    fb->fd = -1;
}

/*
 * Copy an RGBA readback (rows bottom-up, from glReadPixels) to the fbdev
 * (rows top-down).  The Vita DTB advertises "a8b8g8r8", which simplefb maps
 * to red=0, green=8, blue=16, alpha=24 (DRM_FORMAT_ABGR8888).  On a
 * little-endian CPU the bytes are therefore R,G,B,A and the common case is a
 * straight row copy.  swap_rb selects BGRA for panels wired the other way.
 * Returns the copy time in ms, or -1 if the framebuffer cannot take the image.
 */
static double fb_present(fbdev *fb, const uint32_t *rgba, int width, int height,
                         int swap_rb)
{
    double t0, t1;
    int x, y;
    int copy_w, copy_h;

    if (!fb->mem || fb->bpp != 32) {
        fprintf(stderr, "glbench: cannot present: fb is %d bpp\n", fb->bpp);
        return -1.0;
    }

    copy_w = width < fb->width ? width : fb->width;
    copy_h = height < fb->height ? height : fb->height;

    t0 = now_ms();
    for (y = 0; y < copy_h; y++) {
        const uint32_t *src = rgba + (size_t)(height - 1 - y) * (size_t)width;
        uint32_t *dst = (uint32_t *)(fb->mem + (size_t)y * (size_t)fb->stride);

        if (!swap_rb) {
            memcpy(dst, src, (size_t)copy_w * 4u);
            continue;
        }

        for (x = 0; x < copy_w; x++) {
            uint32_t s = src[x];
            /* RGBA bytes -> BGRA bytes (swap R and B). */
            dst[x] = (s & 0xff00ff00u) |
                     ((s & 0x00ff0000u) >> 16) |
                     ((s & 0x000000ffu) << 16);
        }
    }
    t1 = now_ms();
    return t1 - t0;
}

static void usage(const char *argv0)
{
    printf("usage: %s [options]\n"
           "  --width N        render width (default %d)\n"
           "  --height N       render height (default %d)\n"
           "  --frames N       frames for clear/fill/tex (default %d)\n"
           "  --ui-frames N    frames for the ui scene (default %d)\n"
           "  --scene NAME     clear, fill, tex, or ui (default: all)\n"
           "  --fb PATH        framebuffer device (default %s)\n"
           "  --fb-format F    rgba (default, DTB a8b8g8r8) or bgra\n"
           "  --no-present     do not touch the framebuffer\n"
           "  --hold SECONDS   keep the presented frame on screen\n"
           "  --gate FPS       pass/fail threshold for the ui scene (default %d)\n"
           "  --repeats N      present measurements (default 5)\n"
           "  --json           print JSON lines instead of text\n"
           "  --help           this text\n",
           argv0, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FRAMES,
           DEFAULT_UI_FRAMES, DEFAULT_FB_PATH, DEFAULT_GATE_FPS);
}

static void parse_args(options *o, int argc, char **argv)
{
    int i;

    memset(o, 0, sizeof(*o));
    o->width = DEFAULT_WIDTH;
    o->height = DEFAULT_HEIGHT;
    o->frames = DEFAULT_FRAMES;
    o->ui_frames = DEFAULT_UI_FRAMES;
    o->gate_fps = DEFAULT_GATE_FPS;
    o->repeats = 5;
    o->fb_path = DEFAULT_FB_PATH;
    o->present = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(a, "--json") == 0) {
            o->json = 1;
        } else if (strcmp(a, "--no-present") == 0) {
            o->present = 0;
        } else if (strcmp(a, "--width") == 0 && v) {
            o->width = atoi(v);
            i++;
        } else if (strcmp(a, "--height") == 0 && v) {
            o->height = atoi(v);
            i++;
        } else if (strcmp(a, "--frames") == 0 && v) {
            o->frames = atoi(v);
            i++;
        } else if (strcmp(a, "--ui-frames") == 0 && v) {
            o->ui_frames = atoi(v);
            i++;
        } else if (strcmp(a, "--scene") == 0 && v) {
            o->scene = v;
            i++;
        } else if (strcmp(a, "--fb") == 0 && v) {
            o->fb_path = v;
            i++;
        } else if (strcmp(a, "--fb-format") == 0 && v) {
            if (strcmp(v, "rgba") == 0)
                o->fb_swap_rb = 0;
            else if (strcmp(v, "bgra") == 0)
                o->fb_swap_rb = 1;
            else
                die("unknown fb format: %s (use rgba or bgra)", v);
            i++;
        } else if (strcmp(a, "--hold") == 0 && v) {
            o->hold_s = atoi(v);
            i++;
        } else if (strcmp(a, "--gate") == 0 && v) {
            o->gate_fps = atoi(v);
            i++;
        } else if (strcmp(a, "--repeats") == 0 && v) {
            o->repeats = atoi(v);
            i++;
        } else {
            die("unknown option: %s (try --help)", a);
        }
    }

    if (o->width < 16 || o->height < 16 || o->width > 4096 || o->height > 4096)
        die("bad size %dx%d", o->width, o->height);
    if (o->frames < 1 || o->ui_frames < 1)
        die("frame counts must be positive");
    if (o->repeats < 1)
        o->repeats = 1;

    if (o->scene && strcmp(o->scene, "clear") != 0 &&
        strcmp(o->scene, "fill") != 0 && strcmp(o->scene, "tex") != 0 &&
        strcmp(o->scene, "ui") != 0)
        die("unknown scene: %s", o->scene);

    g_json = o->json;
}

static void print_json(const char *renderer, const char *version,
                       const char *vendor, scene_result *results, int n,
                       double present_ms, double effective_fps,
                       const options *o)
{
    int i;

    printf("{\"event\":\"env\",\"renderer\":\"%s\",\"gl_version\":\"%s\","
           "\"vendor\":\"%s\",\"width\":%d,\"height\":%d}\n",
           renderer, version, vendor, o->width, o->height);
    for (i = 0; i < n; i++) {
        if (!results[i].ran)
            continue;
        printf("{\"event\":\"scene\",\"name\":\"%s\",\"frames\":%d,"
               "\"ms_per_frame\":%.3f,\"fps\":%.2f}\n",
               results[i].name, results[i].frames, results[i].ms,
               results[i].fps);
    }
    if (present_ms >= 0.0)
        printf("{\"event\":\"present\",\"path\":\"%s\",\"ms\":%.3f}\n",
               o->fb_path, present_ms);
    if (effective_fps > 0.0)
        printf("{\"event\":\"gate\",\"threshold_fps\":%d,"
               "\"effective_fps\":%.2f,\"verdict\":\"%s\"}\n",
               o->gate_fps, effective_fps,
               effective_fps >= (double)o->gate_fps ? "pass" : "fail");
}

int main(int argc, char **argv)
{
    options o;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLConfig cfg = NULL;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLint major = 0, minor = 0, num_cfg = 0;
    EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint pb_attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
    const char *renderer, *version, *vendor;
    scene_result results[4];
    int n_results = 0;
    int i;
    int want_ui;
    uint32_t *readback = NULL;
    fbdev fb;
    double present_ms = -1.0;
    double ui_render_ms = -1.0;
    double effective_fps = -1.0;

    parse_args(&o, argc, argv);

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display) {
        dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA,
                                   EGL_DEFAULT_DISPLAY, NULL);
    }
    if (dpy == EGL_NO_DISPLAY) {
        info("glbench: surfaceless platform unavailable, trying default EGL display\n");
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (dpy == EGL_NO_DISPLAY)
        die("no EGL display");

    if (!eglInitialize(dpy, &major, &minor))
        die("eglInitialize failed: %s", egl_error_string(eglGetError()));

    {
        const char *egl_vendor = eglQueryString(dpy, EGL_VENDOR);
        const char *egl_ext = eglQueryString(dpy, EGL_EXTENSIONS);

        info("glbench: EGL %d.%d, vendor %s\n", major, minor,
             egl_vendor ? egl_vendor : "(unknown)");
        info("glbench: EGL extensions: %s\n",
             egl_ext ? egl_ext : "(none)");
    }

    if (!eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num_cfg) || num_cfg < 1) {
        /* Surfaceless configs do not always advertise EGL_PBUFFER_BIT. */
        EGLint fallback[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        if (!eglChooseConfig(dpy, fallback, &cfg, 1, &num_cfg) || num_cfg < 1)
            die("no EGL config for GLES2 (surfaceless)");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API))
        check_egl("eglBindAPI");

    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT)
        check_egl("eglCreateContext");

    surf = eglCreatePbufferSurface(dpy, cfg, pb_attribs);
    if (surf != EGL_NO_SURFACE) {
        if (!eglMakeCurrent(dpy, surf, surf, ctx))
            check_egl("eglMakeCurrent (pbuffer)");
    } else {
        info("glbench: pbuffer unavailable, using surfaceless make-current\n");
        if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
            check_egl("eglMakeCurrent (no surface)");
    }

    renderer = (const char *)glGetString(GL_RENDERER);
    version = (const char *)glGetString(GL_VERSION);
    vendor = (const char *)glGetString(GL_VENDOR);
    if (!renderer || !version)
        die("no GL strings after make-current");
    if (!vendor)
        vendor = "";
    info("glbench: GL_RENDERER %s\n", renderer);
    info("glbench: GL_VERSION  %s\n", version);
    info("glbench: GL_VENDOR   %s\n", vendor);
    {
        const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

        info("glbench: GLSL       %s\n", glsl ? glsl : "(unknown)");
    }

    init_gl(o.width, o.height);

    results[0].name = "clear";
    results[1].name = "fill";
    results[2].name = "tex";
    results[3].name = "ui";

    for (i = 0; i < 4; i++) {
        int frames = (i == 3) ? o.ui_frames : o.frames;

        results[i].ran = 0;
        results[i].frames = frames;
        if (o.scene && strcmp(o.scene, results[i].name) != 0)
            continue;

        results[i].ms = bench_scene(results[i].name, frames);
        results[i].fps = 1000.0 / results[i].ms;
        results[i].ran = 1;
        n_results = i + 1;
    }

    want_ui = (!o.scene || strcmp(o.scene, "ui") == 0);
    ui_render_ms = results[3].ran ? results[3].ms : -1.0;

    if (o.present && want_ui) {
        readback = malloc((size_t)o.width * (size_t)o.height * 4);
        if (!readback)
            die("out of memory for the readback buffer");

        if (fb_open(&fb, o.fb_path) == 0) {
            double total = 0.0;
            int reps = 0;
            int r;

            for (r = 0; r < o.repeats; r++) {
                double rt0, rt1, ct;

                render_scene("ui", r);
                glFinish();

                rt0 = now_ms();
                glReadPixels(0, 0, o.width, o.height, GL_RGBA,
                             GL_UNSIGNED_BYTE, readback);
                rt1 = now_ms();

                ct = fb_present(&fb, readback, o.width, o.height,
                                o.fb_swap_rb);
                if (ct < 0.0)
                    break;

                if (r == 0)
                    info("glbench: present readback %.3f ms, copy %.3f ms\n",
                         rt1 - rt0, ct);
                total += (rt1 - rt0) + ct;
                reps++;
            }

            if (reps > 0)
                present_ms = total / (double)reps;

            if (o.hold_s > 0) {
                info("glbench: holding the last frame for %d s\n", o.hold_s);
                sleep((unsigned)o.hold_s);
            }

            fb_close(&fb);
        } else {
            fprintf(stderr, "glbench: presentation skipped\n");
        }
    }

    if (ui_render_ms > 0.0) {
        double render_fps = 1000.0 / ui_render_ms;

        if (present_ms >= 0.0)
            effective_fps = 1000.0 / (ui_render_ms + present_ms);
        else
            effective_fps = render_fps;
    }

    if (o.json) {
        print_json(renderer, version, vendor, results, n_results,
                   present_ms, effective_fps, &o);
    } else {
        printf("\nglbench: %s, %dx%d\n", renderer, o.width, o.height);
        for (i = 0; i < n_results; i++) {
            if (!results[i].ran)
                continue;
            printf("  %-5s %8.2f fps  %8.3f ms/frame  (%d frames)\n",
                   results[i].name, results[i].fps, results[i].ms,
                   results[i].frames);
        }
        if (present_ms >= 0.0)
            printf("  present %22.3f ms  (readback + %s copy)\n",
                   present_ms, o.fb_swap_rb ? "BGRA" : "RGBA");
        if (effective_fps > 0.0) {
            printf("\ngate: %d fps -> %s (effective %.2f fps%s)\n",
                   o.gate_fps,
                   effective_fps >= (double)o.gate_fps ? "PASS" : "FAIL",
                   effective_fps,
                   present_ms >= 0.0 ? ", render + present" : "");
        }
    }

    free(readback);
    if (ctx != EGL_NO_CONTEXT)
        eglDestroyContext(dpy, ctx);
    if (surf != EGL_NO_SURFACE)
        eglDestroySurface(dpy, surf);
    if (dpy != EGL_NO_DISPLAY)
        eglTerminate(dpy);

    return 0;
}
