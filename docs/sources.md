# Sources and references

## Vita Linux and boot chain

| Project | What it is |
| --- | --- |
| https://github.com/devwithzachary/LinuxOnVita | Linux 6.12 distribution, bare-metal loader integration, build scripts, VPK installer |
| https://github.com/incognitojam/linux_vita | Vita kernel port, branch `vita-port-6.12.0`; DTS, drivers, defconfig |
| https://github.com/xerpi/linux_vita | Original Vita kernel port (older branches; useful history) |
| https://github.com/xerpi/vita-baremetal-linux-loader | Payload that loads `zImage` + DTB and jumps to Linux |
| https://github.com/xerpi/vita-baremetal-loader | taiHEN plugin that stages the payload and reboots into bare metal |
| https://github.com/xerpi/vita-libbaremetal | Bare-metal hardware init library and sample |
| https://vitasdk.org/ | Vita homebrew SDK |
| https://vita.hacks.guide/ | Modding guide (HENkaku/HENlo/Enso, firmware requirements) |
| https://wiki.henkaku.xyz/ | Community hardware reverse-engineering wiki |

## Android

| Project | What it is |
| --- | --- |
| https://source.android.com/ | AOSP documentation |
| https://dl.google.com/android/repository/sys-img/android/ | Prebuilt Android SDK system images; use `android-22/default/armeabi-v7a` |
| https://developer.android.com/tools/sdkmanager | SDK Manager command-line tool for downloading the system image |
| https://android.googlesource.com/platform/manifest | AOSP manifest, if a custom image is built later |
| https://source.android.com/docs/setup/start/requirements | Build requirements (note: current page targets modern AOSP; 5.1 needs older hosts) |
| https://android.googlesource.com/platform/hardware/libhardware/ | HAL headers and reference modules (`gralloc`, `hwcomposer`, `audio`, etc.) |
| https://android.googlesource.com/platform/frameworks/native/ | SurfaceFlinger, HWC, input |
| https://android.googlesource.com/platform/system/core/ | init, logd, adbd, healthd |
| https://android.googlesource.com/platform/bionic/ | Android libc, linker, ashmem callers |

## Firmware and graphics

| Project | What it is |
| --- | --- |
| https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git | Wi-Fi firmware, regulatory database, and other redistributable blobs |
| LinuxOnVita 1.5.0 rootfs | Source of the glibc `wpa_supplicant`, dependency closure, and SD8787 firmware vendored under `vendor/vita-linux/` |
| https://github.com/google/swiftshader | CPU Vulkan implementation; modern versions are Vulkan-only |
| https://angleproject.org/ | ANGLE, GLES over Vulkan/OpenGL/D3D |
| https://mesa3d.org/ | Mesa; softpipe/llvmpipe are CPU rasterizers |
| https://github.com/android-x86 | Android-x86; useful reference for software rendering and fbdev on Android |
| https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/arc/ | ARC++ graphics/software rendering references (large, but informative) |

## Vita homebrew and Android game loaders (context, not the OS)

| Project | What it is |
| --- | --- |
| https://github.com/Rinnegatamante/yoyoloader_vita | Loader for Android YoYo Runner games on VitaOS |
| https://github.com/v-atamanenko/soloader-boilerplate | Boilerplate for TheFloW's so-loader Android-to-Vita ports |
| https://github.com/Rinnegatamante/vitaGL | OpenGL ES to SceGxm wrapper used by VitaOS homebrew |
| https://github.com/v-atamanenko/FalsoJNI | Fake JNI/JVM interface for Android-to-Vita ports |

These projects are useful for understanding Android native libraries, bionic,
and JNI, but they do not provide an Android OS or a Linux GPU driver.

## Notes

- Versions and branches change. Before building, check the default branch of
  each repository and the latest LinuxOnVita release notes.
- LinuxOnVita currently targets 3.60/3.65 Enso consoles. Check its README for
  the current compatibility list before buying hardware or downgrading.
- The first boot uses the prebuilt SDK image, not AOSP source. AOSP 5.1.1 is
  old; many links in modern AOSP docs no longer apply. Use the tag's own build
  documentation and old device trees only if a custom image becomes necessary.
- Do not download proprietary firmware or "ROM packs" from file hosts. Use
  `linux-firmware` and your own console dumps.
