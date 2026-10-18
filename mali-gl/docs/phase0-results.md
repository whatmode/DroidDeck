# Phase 0 results (probe 3d5083d, 2026-10-18)

Base: Mesa 5a07217f (26.3.0-devel) + zenithblue g615-v11-csf series (179 patches).
Config: `-Dgallium-drivers=panfrost -Dvulkan-drivers=panfrost -Dpanfrost-kmds=kbase -Degl=enabled -Dgbm=enabled -Dopengl=true -Dglx=dri`.

**It builds with no errors**: libgallium (Panfrost GL), libEGL, libgbm + dri_gbm and
libvulkan_panfrost all link with only the kbase KMD. So the work is runtime behaviour, not
build plumbing.

## What still assumes DRM/panthor at runtime

| Area | Where | What it does today | kbase replacement |
|---|---|---|---|
| CSF group/queue lifecycle | `gallium/drivers/panfrost/pan_csf.c` (group create/destroy, tiler heap create/destroy) | `DRM_IOCTL_PANTHOR_GROUP_CREATE`, `TILER_HEAP_CREATE` | `kbase_kmod_csf_group_create`, `queue_bind`, `tiler_heap_create` (already used by PanVK patch csf/007) |
| Job submission | `pan_csf.c` `csf_submit_*` -> `DRM_IOCTL_PANTHOR_GROUP_SUBMIT` | kernel copies a CALL into the ring | userspace ring buffer write + `kbase_kmod_csf_queue_kick` (PanVK csf/007 pattern) |
| GPU fault/timeout state | `pan_csf.c` `GROUP_GET_STATE` | panthor group state | `kbase_kmod_csf_has_error` / `wait_event` |
| CSIF props / flush id | `panthor_kmod_get_csif_props`, `get_flush_id` | panthor queries | `kbase_kmod_get_csif_props`, `kbase_kmod_get_flush_id` |
| Completion tracking | `pan_csf.c` VM timeline syncobj, per-BO sync points | DRM timeline syncobjs | seqno in GPU memory signalled by CS `SYNC_ADD64`, CPU wait via `kbase_kmod_csf_wait_cqs64` |
| Fences | `pan_fence.c`, `pan_context.c` (`drmSyncobjCreate/Wait/ExportSyncFile/ImportSyncFile`) | DRM syncobjs for pipe fences and sync_file interop | small fence object = (seqno address, value); sync_file export via kbase KCPU queue (PanVK patch csf-v11/075) |
| Device open | `pan_kmod_dev_create` uses `drmGetVersion` | fails on `/dev/mali0` | select `kbase_kmod_ops` when the fd is a kbase node (PanVK already does this for its own probe) |
| Driver lookup | `loader/loader.c` `loader_get_driver_for_fd` (EGL, GBM, DRI3) | `drmGetVersion` name | map a kbase fd to `panfrost` |
| JM path | `pan_jm.c` | panfrost DRM | not needed: Mali-G715 is CSF |

## Size

PanVK's equivalent (csf/007 queue path + kbase-common/006 + csf-v11/075 sync_file) is
about 3,100 lines of patch. The gallium port is of the same order: roughly 1,500-2,500 lines,
mostly `pan_csf.c` and a fence/sync layer, then a few dozen lines each for device open and
the loader.

## Phase 1 plan

1. `pan_kmod`: kbase fd detection in `pan_kmod_dev_create` (no `drmGetVersion`).
2. `pan_csf.c`: kbase branch for group/queue/tiler-heap lifecycle and submission, ported
   from PanVK csf/007, behind `HAVE_PAN_KMOD_KBASE` and a runtime `dev->kmod.dev->ops` check.
3. Fences: `panfrost_fence` backed by (seqno BO, value) on kbase; synchronous
   `pipe->flush` waits first (correctness before async, as the PanVK project did).
4. Test binary for the phone: an offscreen EGL (surfaceless) GLES clear + readback, run from
   the DroidDeck desktop terminal.
