// Copyright © 2026 AndroidOnVita
//
// Minimal EGL HAL module for PlayStation Vita SGX543MP4+
//
// Provides EGL 1.4 entry points backed by the psp2sgx kernel driver,
// with software emulation fallback via libEGL_emulation.so.

#define EGL_EGL_PROTOTYPES 1
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

extern int psp2sgx_init(void);
extern int psp2sgx_get_param(int param, int *value);

static void *sw_lib = NULL;
typedef EGLBoolean (*sw_init_fn)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*sw_term_fn)(EGLDisplay);
typedef const char *(*sw_qs_fn)(EGLDisplay, EGLint);
typedef EGLBoolean (*sw_get_configs_fn)(EGLDisplay, EGLConfig *, EGLint, EGLint *);
typedef EGLBoolean (*sw_choose_config_fn)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLSurface (*sw_create_ws_fn)(EGLDisplay, EGLConfig, void *, const EGLint *);
typedef EGLBoolean (*sw_destroy_surf_fn)(EGLDisplay, EGLSurface);
typedef EGLContext (*sw_create_ctx_fn)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*sw_destroy_ctx_fn)(EGLDisplay, EGLContext);
typedef EGLBoolean (*sw_make_current_fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*sw_swap_buffers_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*sw_get_config_attrib_fn)(EGLDisplay, EGLConfig, EGLint, EGLint *);
typedef EGLBoolean (*sw_query_surface_fn)(EGLDisplay, EGLSurface, EGLint, EGLint *);

static sw_init_fn sw_init = NULL;
static sw_term_fn sw_term = NULL;
static sw_qs_fn sw_qs = NULL;
static sw_get_configs_fn sw_get_configs = NULL;
static sw_choose_config_fn sw_choose_config = NULL;
static sw_create_ws_fn sw_create_ws = NULL;
static sw_destroy_surf_fn sw_destroy_surf = NULL;
static sw_create_ctx_fn sw_create_ctx = NULL;
static sw_destroy_ctx_fn sw_destroy_ctx = NULL;
static sw_make_current_fn sw_make_current = NULL;
static sw_swap_buffers_fn sw_swap_buffers = NULL;
static sw_get_config_attrib_fn sw_get_config_attrib = NULL;
static sw_query_surface_fn sw_query_surface = NULL;

static EGLBoolean g_initialized = EGL_FALSE;
static EGLConfig g_configs[8];
static int g_num_configs = 0;

static void load_sw(void) {
    if (sw_lib) return;
    sw_lib = dlopen("/system/lib/libEGL_emulation.so", RTLD_NOW | RTLD_LOCAL);
    if (!sw_lib) sw_lib = dlopen("/system/lib/libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!sw_lib) return;

    sw_init = (sw_init_fn)dlsym(sw_lib, "eglInitialize");
    sw_term = (sw_term_fn)dlsym(sw_lib, "eglTerminate");
    sw_qs = (sw_qs_fn)dlsym(sw_lib, "eglQueryString");
    sw_get_configs = (sw_get_configs_fn)dlsym(sw_lib, "eglGetConfigs");
    sw_choose_config = (sw_choose_config_fn)dlsym(sw_lib, "eglChooseConfig");
    sw_create_ws = (sw_create_ws_fn)dlsym(sw_lib, "eglCreateWindowSurface");
    sw_destroy_surf = (sw_destroy_surf_fn)dlsym(sw_lib, "eglDestroySurface");
    sw_create_ctx = (sw_create_ctx_fn)dlsym(sw_lib, "eglCreateContext");
    sw_destroy_ctx = (sw_destroy_ctx_fn)dlsym(sw_lib, "eglDestroyContext");
    sw_make_current = (sw_make_current_fn)dlsym(sw_lib, "eglMakeCurrent");
    sw_swap_buffers = (sw_swap_buffers_fn)dlsym(sw_lib, "eglSwapBuffers");
    sw_get_config_attrib = (sw_get_config_attrib_fn)dlsym(sw_lib, "eglGetConfigAttrib");
    sw_query_surface = (sw_query_surface_fn)dlsym(sw_lib, "eglQuerySurface");
}

EGLAPI EGLint EGLAPIENTRY eglGetError(void) { return EGL_SUCCESS; }

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType id) {
    return (EGLDisplay)1;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *maj, EGLint *min) {
    load_sw();
    if (!g_initialized) {
        if (psp2sgx_init() != 0) return EGL_FALSE;
        g_initialized = EGL_TRUE;
    }
    if (sw_init) return sw_init(dpy, maj, min);
    if (maj) *maj = 1;
    if (min) *min = 4;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy) {
    if (sw_term) return sw_term(dpy);
    g_initialized = EGL_FALSE;
    return EGL_TRUE;
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name) {
    if (sw_qs) return sw_qs(dpy, name);
    switch (name) {
    case EGL_VENDOR: return "PSP2SGX";
    case EGL_VERSION: return "1.4 PSP2SGX";
    case EGL_CLIENT_APIS: return "OpenGL_ES";
    default: return "";
    }
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *cfgs, EGLint size, EGLint *n) {
    if (sw_get_configs) return sw_get_configs(dpy, cfgs, size, n);
    if (!g_num_configs) {
        EGLConfig tmp[8];
        EGLint nn = 0;
        if (sw_choose_config) sw_choose_config(dpy, NULL, tmp, 8, &nn);
        else { tmp[0] = (EGLConfig)1; nn = 1; }
        for (int i = 0; i < nn && i < 8; i++) g_configs[i] = tmp[i];
        g_num_configs = nn;
    }
    if (cfgs) { for (int i = 0; i < g_num_configs; i++) cfgs[i] = g_configs[i]; }
    if (n) *n = g_num_configs;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attr,
                                               EGLConfig *cfgs, EGLint size, EGLint *n) {
    if (sw_choose_config) return sw_choose_config(dpy, attr, cfgs, size, n);
    if (!cfgs) { if (n) *n = g_num_configs; return EGL_TRUE; }
    int c = 0;
    for (int i = 0; i < g_num_configs && c < size; i++) cfgs[c++] = g_configs[i];
    if (n) *n = c;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig cfg,
                                                  EGLint attr, EGLint *val) {
    if (sw_get_config_attrib) return sw_get_config_attrib(dpy, cfg, attr, val);
    switch (attr) {
    case EGL_RED_SIZE: *val = 8; break;
    case EGL_GREEN_SIZE: *val = 8; break;
    case EGL_BLUE_SIZE: *val = 8; break;
    case EGL_ALPHA_SIZE: *val = 8; break;
    case EGL_DEPTH_SIZE: *val = 24; break;
    case EGL_STENCIL_SIZE: *val = 8; break;
    case EGL_RENDERABLE_TYPE: *val = EGL_OPENGL_ES2_BIT; break;
    case EGL_SURFACE_TYPE: *val = EGL_WINDOW_BIT; break;
    case EGL_CONFIG_ID: *val = 1; break;
    case EGL_CONFIG_CAVEAT: *val = EGL_NONE; break;
    case EGL_LEVEL: *val = 0; break;
    case EGL_NATIVE_RENDERABLE: *val = EGL_TRUE; break;
    case EGL_BIND_TO_TEXTURE_RGB: *val = EGL_FALSE; break;
    case EGL_BIND_TO_TEXTURE_RGBA: *val = EGL_FALSE; break;
    case EGL_SWAP_BEHAVIOR: *val = EGL_BUFFER_PRESERVED; break;
    default: *val = 0; break;
    }
    return EGL_TRUE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg,
                                                      void *win, const EGLint *attr) {
    if (sw_create_ws) return sw_create_ws(dpy, cfg, win, attr);
    return (EGLSurface)1;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surf) {
    if (sw_destroy_surf) return sw_destroy_surf(dpy, surf);
    return EGL_TRUE;
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig cfg,
                                                EGLContext share, const EGLint *attr) {
    if (sw_create_ctx) return sw_create_ctx(dpy, cfg, share, attr);
    return (EGLContext)1;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    if (sw_destroy_ctx) return sw_destroy_ctx(dpy, ctx);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                              EGLSurface read, EGLContext ctx) {
    if (sw_make_current) return sw_make_current(dpy, draw, read, ctx);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (sw_swap_buffers) return sw_swap_buffers(dpy, surface);
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surf,
                                                EGLint attr, EGLint value) { return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surf, EGLint buf) { return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surf, EGLint buf) { return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval) { return EGL_TRUE; }
EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void) { return EGL_NO_CONTEXT; }
EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint replane) { return EGL_NO_SURFACE; }
EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void) { return EGL_NO_DISPLAY; }
EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surf,
                                               EGLint attr, EGLint *val) { *val = 0; return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void) { return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine) { return EGL_TRUE; }
EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surf, void *buf) { return EGL_TRUE; }

EGLAPI void *EGLAPIENTRY eglGetProcAddress(const char *name) {
    if (sw_lib) {
        void *p = dlsym(sw_lib, name);
        if (p) return p;
    }
    if (!strcmp(name, "eglGetDisplay")) return (void *)eglGetDisplay;
    if (!strcmp(name, "eglInitialize")) return (void *)eglInitialize;
    if (!strcmp(name, "eglTerminate")) return (void *)eglTerminate;
    if (!strcmp(name, "eglQueryString")) return (void *)eglQueryString;
    if (!strcmp(name, "eglGetConfigs")) return (void *)eglGetConfigs;
    if (!strcmp(name, "eglChooseConfig")) return (void *)eglChooseConfig;
    if (!strcmp(name, "eglGetConfigAttrib")) return (void *)eglGetConfigAttrib;
    if (!strcmp(name, "eglCreateWindowSurface")) return (void *)eglCreateWindowSurface;
    if (!strcmp(name, "eglDestroySurface")) return (void *)eglDestroySurface;
    if (!strcmp(name, "eglCreateContext")) return (void *)eglCreateContext;
    if (!strcmp(name, "eglDestroyContext")) return (void *)eglDestroyContext;
    if (!strcmp(name, "eglMakeCurrent")) return (void *)eglMakeCurrent;
    if (!strcmp(name, "eglSwapBuffers")) return (void *)eglSwapBuffers;
    if (!strcmp(name, "eglGetProcAddress")) return (void *)eglGetProcAddress;
    if (!strcmp(name, "eglWaitGL")) return (void *)eglWaitGL;
    if (!strcmp(name, "eglWaitNative")) return (void *)eglWaitNative;
    if (!strcmp(name, "eglCopyBuffers")) return (void *)eglCopyBuffers;
    if (!strcmp(name, "eglSurfaceAttrib")) return (void *)eglSurfaceAttrib;
    if (!strcmp(name, "eglBindTexImage")) return (void *)eglBindTexImage;
    if (!strcmp(name, "eglReleaseTexImage")) return (void *)eglReleaseTexImage;
    if (!strcmp(name, "eglSwapInterval")) return (void *)eglSwapInterval;
    if (!strcmp(name, "eglGetCurrentContext")) return (void *)eglGetCurrentContext;
    if (!strcmp(name, "eglGetCurrentSurface")) return (void *)eglGetCurrentSurface;
    if (!strcmp(name, "eglGetCurrentDisplay")) return (void *)eglGetCurrentDisplay;
    if (!strcmp(name, "eglQuerySurface")) return (void *)eglQuerySurface;
    return NULL;
}
