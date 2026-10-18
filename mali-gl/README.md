# mali-gl: GPU-accelerated GL, GBM and Xwayland on Mali phones (kbase, no DRM node)

On Android phones with a Mali CSF GPU (Tensor G2-G4, Dimensity, Exynos with Mali) the GPU is
reached through Arm's `mali_kbase` driver (`/dev/mali0`), not DRM. PanVK already runs on it
through the kbase `pan_kmod` backend from zenithblue-oss/panvk-kbase-android. Everything that
expects a DRM render node is still missing, and that is what breaks Steam Big Picture in
DroidDeck: Xwayland gets no glamor (no GBM), so clients get no DRI3 and every GL frame is
copied through the X connection in software.

## Goal

A Linux userspace where, on kbase:

1. Panfrost gallium (native GL/GLES, EGL) runs on the same kbase `pan_kmod` backend as PanVK.
2. GBM opens `/dev/mali0` and allocates/exports dma-bufs.
3. Xwayland initialises glamor on that GBM device and offers DRI3 to clients.
4. gamescope/wlroots advertise the device so Xwayland and clients find it.

## Phases

| Phase | Deliverable | Test on the phone |
|---|---|---|
| 0 | CI probe: how far Panfrost GL/EGL/GBM is from building on kbase only | none (CI) |
| 1 | Panfrost gallium submits through kbase CSF (port PanVK's kbase queue code) | `eglinfo -B -p surfaceless`, offscreen GLES draw |
| 2 | Mesa loader maps a kbase fd to `panfrost`; GBM on `/dev/mali0`; dma-buf export/import | `gbm` allocation test, EGL dma-buf import |
| 3 | Xwayland: glamor on a GBM device given by path (no DRM node lookup) | `xdpyinfo` shows DRI3, `glxinfo -B` on Xwayland |
| 4 | gamescope/wlroots: expose the device; DroidDeck runtime packaging | Steam Big Picture in game mode |

## Layout

- `scripts/probe.sh` - Phase 0 probe (runs in `.github/workflows/mali-gl-probe.yml`).
- Results are pushed to the `mali-gl-results` branch (`results/latest/`).

Base: Mesa pinned by zenithblue-oss/panvk-kbase-android `sources.lock`, with its g615-v11-csf
patch series (v11 = Mali-G615/G715, which covers the Pixel 9 Pro XL's Tensor G4).
