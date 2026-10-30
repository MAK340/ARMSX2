# ARMSX2 on Allwinner H700 handhelds

RG35XX H / Plus / SP, RG28XX, RG40XX H/V and other H700 devices running
**Knulli**, **muOS** or the **stock** firmware.

> **Set expectations first.** The H700 is four in-order Cortex-A53 cores at
> 1.5 GHz with a two-core Mali-G31 and 1 GB of RAM - well below anything PS2
> emulation is normally shown on. Most 3D games run in single digits or low
> teens of fps. Treat this as a tech demo.
>
> On an RG35XX-H with Knulli, Ratchet & Clank (NTSC-U) runs cutscenes at
> ~24 fps and gameplay at ~8 fps with the settings below, without glitches.

## What is different about this build

The regular `armsx2-linux-arm64-sdl` handheld build does not run here, for two
reasons, and the `armsx2-linux-arm64-sdl-a53` build fixes both:

| Problem | Regular build | H700 build |
| --- | --- | --- |
| CPU | Built for ARMv8.1 (LSE atomics). The A53 is ARMv8.0, so it crashes with `SIGILL`. | `-march=armv8-a+crc+crypto -mtune=cortex-a53 -moutline-atomics` |
| Display | Vulkan only (`VK_KHR_display`). The G31 blob on these firmwares has no Vulkan. | `KMS_API`: OpenGL ES straight to the display through KMS/GBM, or Mali fbdev |

With `KMS_API`, the frontend picks the OpenGL renderer on first run when there
is no `libvulkan.so.1`, and `GLContextEGLKMS` takes over the display:

1. **GBM** (Mesa, or `libmali` built for gbm): opens `/dev/dri/card*`, takes the
   panel's preferred mode, renders into a `gbm_surface` and page-flips it on
   vblank.
2. **fbdev** (`libmali` built for fbdev): hands EGL `/dev/fb0`'s size and lets the
   blob draw to it.

GBM is tried first, then fbdev.

## Installing

1. Grab the `armsx2-linux-arm64-sdl-a53` artifact from the *Build All Platforms*
   workflow run and unpack it. Rename the folder to `armsx2`.
2. Copy `platforms/h700/ARMSX2.sh` next to that folder, and put both in the
   firmware's Ports folder (`roms/ports` on Knulli; check your muOS version's
   docs for where ports/applications live).
3. Put your PS2 BIOS dump in `armsx2/bios/` (the launcher turns on portable mode,
   so all data lives in that folder), or point Settings -> BIOS at the
   firmware's own BIOS folder (`/userdata/bios` on Knulli).
4. Launch **ARMSX2** from Ports. With no game passed in it opens the game list.
   Add a game directory under Settings -> Game List.

The launcher maps the built-in controls for SDL. In ARMSX2, bind Pad 1 to
`SDL-0` and put the pause menu on the Menu button (`SDL-0/Guide`).

## Memory

1 GB with no swap is the real limit: a running game leaves ~100 MB free, and
the out-of-memory killer picks the emulator when anything else needs some.

- **Launch from a fresh boot**, without another emulator (RetroArch) having run
  first. Leftover processes and fragmented memory are what get it killed.
- The launcher flushes caches, compacts memory and raises the kernel's free
  reserve (`vm.min_free_kbytes`) for the session, and puts it back on exit.
- Under 2 GB of RAM, the OpenGL renderer uses smaller upload and uniform
  buffers, which keeps ~100 MB of GPU (shared) memory free.

## Settings to start with

These matter more on this device than anywhere else:

- **Renderer: OpenGL** at **Native (1x)** resolution. Software is slower still.
  ARMSX2 warns that the graphics API isn't Automatic; that is expected, as
  Automatic would pick Vulkan.
- **Blending Accuracy: Basic**. Minimum is noticeably faster but breaks effects
  (Ratchet & Clank glitches with it).
- **Dithering: Off**. Costs a little banding.
- Leave **EE Cycle Skip at 0**: in Ratchet & Clank it soft-locks the game. EE
  Cycle Rate/Skip are worth trying per game, not globally.
- Prefer CHD images, which need less I/O than ISO.

## Environment overrides

| Variable | Effect |
| --- | --- |
| `ARMSX2_EGL_PLATFORM=gbm` / `fbdev` | Force one display path instead of trying GBM then fbdev |
| `ARMSX2_DRM_DEVICE=/dev/dri/card1` | Use a specific DRM node |
| `ARMSX2_FBDEV=/dev/fb1` | Use a specific framebuffer device |

`--video gl` / `--video vulkan` overrides the renderer for one launch.

## When it doesn't start

Everything goes to `armsx2/armsx2.log`. Look for the `EGL KMS:` lines:

- `No DRM device with a connected display`: the firmware's frontend still holds
  the display (DRM master). Launch through Ports so the frontend lets go of it,
  or try `ARMSX2_EGL_PLATFORM=fbdev`.
- `Loading libgbm.so.1 failed` followed by fbdev: expected on an fbdev blob.
- `GLIBC_2.xx not found`: the CI binary is built on Ubuntu 22.04 (glibc 2.35).
  A firmware with an older glibc needs a build in a matching sysroot.
- The picture is rotated: some H700 panels (RG28XX) are mounted portrait. There
  is no rotation in the KMS path yet.
