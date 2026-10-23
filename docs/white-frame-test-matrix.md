# White-frame test matrix (Lake, app 1118240)

> **The tests below are no longer env vars.** `mali-gl/patches/0005` moved the kbase tunables out
> of the environment and into `panvk_kbase_profile_for()`, so `PANVK_DEBUG`'s `bc_compute`,
> `bc_wide`, `no_bc_emul`, `no_gs`, `wsi_no_afbc`, `no_crc`, `no_wb_mmap`, `cached_before_coherent`
> and `gpu_prerast` are gone, and `PANVK_BC_AFRC` no longer exists. Setting them now does nothing:
> the run silently falls back to the compiled defaults and every test looks identical.
>
> The matrix is still the right test order. It is now driven by `PANVK_PROFILE_KNOB`, a
> `workflow_dispatch` input on `.github/workflows/panvk-icd-build.yml` that rewrites one field of
> `panvk_kbase_profile_for()`'s initializer before the build (see the table below). Each row is one
> build, one imported driver, one launch. The reading rules and the checklist still apply.

Every test below is **one launch, one share**. The zip holds one session only, so send one zip per
test. Each test lists the complete environment block it needs; do not carry values over between
tests, because a leftover key silently changes the run.

## How a tester sets a knob (there is no environment editor)

The app has **no environment editor and no per-game profiles**: a game has only its Proton build and
its driver bundle. So every env var below goes in **Lake's own Steam launch options**, in Steam's
documented form:

```
NAME=value %command%
```

`%command%` is what makes the words an environment variable. Written bare, they land after the
executable and become arguments that nothing reads -- which is exactly how the 2026-10-23-05 D1 run
was invalidated (see D1 below). `droiddeck-game-env` now also lifts bare `NAME=value` words into the
environment and prints "moved into the environment: ...", so a bare word is no longer silently
dropped, but write the `%command%` form anyway.

The tests that are **driver builds** (`profile_knob`) need no env var at all: dispatch the build,
import the zip, launch.

## Why these and not the extension list

An earlier revision of this file said extension bisection was dead. That was wrong, and the
correction matters because it reopens the plan's step 2, which is the cheapest test available.

DXVK marks 53 of its 174 feature/extension entries `require = true`
(`src/dxvk/dxvk_device_info.cpp`). Hiding one of those fails `checkDeviceCompatibility()`,
`DxvkAdapter::isCompatible()` returns false, and the game dies at startup instead of rendering.
Hiding an entry that is `require = false` does not: `chainFeature()` only sets the feature when the
extension's `specVersion` is non-zero, so a hidden extension leaves the feature false and DXVK
takes its fallback path.

Only **6 extensions** carry a `require = true` feature, and `VK_LAYER_DROIDDECK_hideext` refuses
exactly those:

| extension | required feature |
|---|---|
| `VK_KHR_swapchain` | presentation |
| `VK_KHR_load_store_op_none` | `loadStoreOpNone` |
| `VK_KHR_maintenance5` | `maintenance5` |
| `VK_KHR_maintenance6` | `maintenance6` |
| `VK_EXT_depth_clip_enable` | `depthClipEnable` |
| `VK_EXT_robustness2` | `robustBufferAccess2`, `nullDescriptor` |

Everything else in the brief's list is `require = false` and therefore hideable:
`VK_KHR_unified_image_layouts`, `VK_EXT_extended_dynamic_state3`,
`VK_EXT_attachment_feedback_loop_layout`, `VK_KHR_dynamic_rendering_local_read`,
`VK_KHR_shader_float_controls2`, `VK_KHR_shader_untyped_pointers`, `VK_EXT_transform_feedback`,
`VK_EXT_multi_draw`, `VK_EXT_depth_bias_control`, `VK_EXT_non_seamless_cube_map`,
`VK_EXT_custom_border_color`, `VK_KHR_maintenance8`, `VK_KHR_maintenance9`.

`no_bc_emul` (`textureCompressionBC`, line 868) and `no_gs` (`geometryShader`, line 834) remain
dead ends, because those are `require = true` core features, not extensions.

G715 reports `texture_features 0xc3fe001e`, and `0xc3fe001e & 0x1ff80 == 0`, so it has no native BC:
BC emulation is exactly what supplies `textureCompressionBC`. `no_bc_emul` therefore removes a
required feature rather than changing a code path. Forcing `PANVK_BC_MODE_COMPUTE` is the change
that keeps the feature and still moves the path (T1).

## Extension bisection (E-tests)

These need **no rebuild**. `DROIDDECK_HIDE_EXTS` names a comma-separated list; the layer hides those
device extensions from DXVK. Put it in Lake's launch options and launch once, exactly like the
T-tests. `droiddeck-game-env` derives `DROIDDECK_HIDE_EXTS_ENABLE=1` from it, so only one variable
is set. The layer prints what it hid to stderr, which lands in `session.log`.

Bisect in halves. If hiding a half renders, the culprit is in that half; if it stays white, it is in
the other half. Four to five launches pin one extension.

| test | `DROIDDECK_HIDE_EXTS` | reading |
|---|---|---|
| E1 | `VK_KHR_unified_image_layouts,VK_EXT_extended_dynamic_state3,VK_EXT_attachment_feedback_loop_layout,VK_KHR_dynamic_rendering_local_read,VK_KHR_shader_float_controls2,VK_KHR_shader_untyped_pointers,VK_EXT_transform_feedback,VK_EXT_multi_draw,VK_EXT_depth_bias_control,VK_EXT_non_seamless_cube_map,VK_EXT_custom_border_color,VK_KHR_maintenance8,VK_KHR_maintenance9` | renders → halve. white → none of these, go to the render-path tests |
| E2 | first half of the E1 list | renders → halve again |
| E3 | second half of the E1 list | renders → halve again |

`VK_KHR_unified_image_layouts` is the first one to try on its own, because DXVK also exposes a
direct switch for it: `DXVK_CONFIG="dxvk.enableUnifiedImageLayouts = False"` needs no layer at all.
If that single line renders, the answer is found with one launch.

**Renders →** the bug is in that extension's PanVK path; read that path, then write the minimal
repro and take it to dEQP (plan step 5).
**Still white with everything hidden →** no optional extension is involved; go back to the
render-path tests below, and the answer is a shader or format path, not a feature.

## Frozen baseline: identical in every test

| variable | value | why |
|---|---|---|
| `PROTON_LOG` | `1` | log collection |
| `DROIDDECK_HIDE_YCBCR` | `1` | the shipped mali default; keep it constant |
| `MESA_SHADER_CACHE_DISABLE` | *(unset)* | keep the cache in its normal state |
| `DXVK_CONFIG` | *(no bisection keys)* | `zeroWorkgroupMemory` is not a real DXVK option |
| `DROIDDECK_HIDE_EXTS` | *(unset except in E-tests)* | the extension bisection variable |

Do **not** clear any cache, before or between tests. The "first two launches worked, then white
forever" pattern is what a poisoned cache looks like, and it was already refuted on device:
`MESA_SHADER_CACHE_DISABLE=true` changed nothing, and deleting DXVK's state cache changed nothing.
Clearing caches now would only add a variable to a run that has to be comparable.

## Desktop mode is a working control (D-tests)

**The menu renders correctly in desktop mode.** That is the first working control in this
investigation, and it re-localises the bug.

The two modes differ in exactly one place. In `droiddeck-session`, desktop mode (`lxqt`) reaches
`exec /usr/local/bin/droiddeck-desktop` at line 243, which is **before** `mali_gl_setup` (line 260)
and before the gamescope block (lines 291-296). So desktop mode never starts gamescope:

| | desktop mode | steam mode |
|---|---|---|
| guest compositor | **KWin** (`--wayland-display wayland-0 --socket wayland-1`) | **gamescope** (`--backend wayland`) |
| `mali_gl_setup` | not run | run (`WLR_XWAYLAND`, `PAN_MESA_DEBUG=gl3`, `/opt/mali-gl/lib`) |
| gamescope WSI layer | absent | present (`-e`, `libVkLayer_FROG_gamescope_wsi_aarch64.so`) |
| linux-dmabuf source | KWin's | gamescope's, hand-built by patch 0121 |
| composite of the game's dma-buf | KWin, **software** (`GALLIUM_DRIVER=llvmpipe`, `LIBGL_ALWAYS_SOFTWARE=1`) | gamescope, **on PanVK** |
| ends at | the app's native compositor (`wayland-0`) | the app's native compositor (`wayland-0`) |

Both modes present into the app's native compositor, so the compositor is still common and still
exonerated. What differs is gamescope: its WSI layer, its linux-dmabuf tranche, and its GPU-side
composite of the game's buffer.

### This revives the presentation theory

An earlier note here dismissed presentation because the vignette proves the game's post-processing
ran. That was too quick. A format or transfer-function mismatch at the swapchain boundary produces
exactly this symptom: dark features (the vignette) stay dark while mid-tones clip to white, so the
frame reads as "white with a vignette" without the scene itself being white. Desktop mode rendering
correctly, with the only difference being who owns the swapchain and the composite, is evidence for
that, not against it.

Two sub-hypotheses, both Mali-specific:

- **D-a: the gamescope WSI layer's swapchain.** The FROG layer picks the swapchain format and
  modifier that DXVK renders into. Steam mode only.
- **D-b: gamescope's own import and composite of the game's dma-buf.** Patch `0121` builds the
  linux-dmabuf v4 tranche by hand and names the formats itself, and its own header says it is
  **not device-tested** on this GPU. A wrong advertised format or modifier means the buffer is
  misread when gamescope samples it.

### D1 — is it the gamescope WSI layer?

One launch, **no rebuild and no layer**. The FROG layer's manifest gates it on
`disable_environment: {"DISABLE_GAMESCOPE_WSI": "1"}`, so the knob is read from the environment
**only**. There is no environment editor in the app, so it goes in Lake's Steam launch options in
the `%command%` form:

```
DISABLE_GAMESCOPE_WSI=1 %command%
```

**2026-10-23-05 typed it bare, without `%command%`, and it did not work**: Steam appends launch
options after the executable, so it arrived as the argv token
`Lake.exe' DISABLE_GAMESCOPE_WSI=1` and the FROG layer never saw it -- the Proton log still has all
eight `[Gamescope WSI]` lines, including `Creating swapchain for xid: ... format:
VK_FORMAT_R8G8B8A8_UNORM ... flip: true`. So that launch **does not refute D-a; the test never
ran.** `droiddeck-game-env` now lifts bare `NAME=value` launch options into the environment and logs
"moved into the environment: ...", which is the line to look for.

Before spending a launch, confirm the knob is really in the game process's environment
(`/proc/<pid>/environ`, or the session log's process dump). Re-run D1 with the knob in the
`%command%` form:

**Renders →** D-a. The WSI layer's swapchain is the bug, and the next step is to log the format
and modifier it negotiates.
**Still white →** D-a is out. Go to D2.

### D2 — is it gamescope's composite, or the guest compositor at all?

If D1 is white, the remaining difference is who composites. Run **steam mode** and reach the game
through KWin instead of gamescope. The supported route is already wired: launch the desktop session
(`BL_DESKTOP=1`) and start the game from Steam's desktop UI, which is the path that renders
correctly. Compare its `session.log` against a steam-mode one.

**Renders →** gamescope's composite or its linux-dmabuf tranche (D-b). Read patch `0121`'s
format list first, then log what gamescope advertises.
**Still white →** the working desktop case is a different *renderer* configuration, not a
compositor difference, and the mode comparison must be redone from the logs.

## DXVK-side tests (Z-tests)

These need **no rebuild and no layer**: DXVK reads `DXVK_CONFIG` (the `dxvkConfig` field the app
already injects), and each is one launch. Run them **first** — they are the cheapest way to test
the brief's theory 1, and the first two are things zink's GL path would not do, which is exactly the
"something DXVK uses that zink doesn't" shape of the bug.

Set the value in Lake's launch options:

| test | `DXVK_CONFIG` | reading |
|---|---|---|
| Z1 | `dxvk.tilerMode = False` | DXVK's tiler submission heuristics. PanVK is a tiler, so this path is live here and absent under zink/GL. Renders → the bug is in the tiler path |
| Z2 | `dxvk.enableUnifiedImageLayouts = False` | theory 3's unified-layouts arm. Renders → unified layouts |
| Z3 | `d3d11.disableDirectImageMapping = True` | theory 3's image arm: wrong row pitch or wrong reads on mapped dynamic images |
| Z4 | `dxvk.zeroMappedMemory = True` | reads of freshly allocated images that PanVK does not zero |

`dxvk.tilerMode` is documented as "only intended to be changed for performance testing and
debugging purposes", which is what this is. All four keys are real options in DXVK's `dxvk.conf`;
`d3d11.zeroWorkgroupMemory` (already ruled out) is not one, which is why that earlier test proved
nothing.

**Renders →** stop and read that path; a single config line has localised the bug.
**All four white →** no DXVK-side option is involved; go to the E-tests.

## The tests

Each test is a **driver build**, not an env var. Dispatch `panvk-icd-build.yml` with the
`profile_knob` input below, import the resulting `...-knob-...` zip as the Linux Vulkan driver, and
launch once. The knob is folded into the zip name and `meta.json`, so the driver row names the test.
Leave the environment at the frozen baseline above: no `PANVK_DEBUG`, no `PANVK_BC_AFRC`.

### T1 — is it the fragment-pass BC shadow?

```
profile_knob: bc_mode=PANVK_BC_MODE_COMPUTE
```

`csf-v11/150` sets `bc_gfx = bc_emul && arch >= 10 && 2D && !BC_COMPUTE && !NO_GS`. Forcing
`PANVK_BC_MODE_COMPUTE` clears the fragment shadow but leaves `bc_emul` set, so
`textureCompressionBC` survives and DXVK still creates the device. BC decode moves from the
fragment-pass AFBC/AFRC shadow (`150`/`151`) to the old compute path (`022`).

**Fixes →** the bug is in `150`/`151`. Go to T3.
**Still white →** BC shadow is innocent. Skip T3, go to T4.

### T2 — is AFRC specifically?

`PANVK_BC_AFRC` was deleted by `0005` and has no profile field, so this test cannot be built from
the knob. It only ran after T1 rendered; if T1 renders, read `151` directly instead.

### T3 — is it the compact shadow format?

```
profile_knob: bc_wide=true
```

`PANVK_PROFILE_KNOB` takes one field, so this build changes only the storage format and keeps the
default fragment shadow. Compare it with the plain T8 build: `bc_wide` keeps 16-bit storage for
BC4/BC5/BC6H UF, separating the compact-format choice from the storage format. Only meaningful if
the shadow path is implicated.

### T4 — is it the presentation-side AFBC?

```
profile_knob: wsi_afbc=false
```

`csf-v11/033` enables AFBC for WSI images by default. A wrong AFBC presentation layout shows as a
uniform wrong frame, which is what we have. Does not touch any feature bit.

**Fixes →** WSI AFBC. **Still white →** not presentation.

### T5 — is it host/GPU cache coherency?

```
profile_knob: cached_before_coherent=true
```

If this one fixes it, also run T6 and T7 individually, because they are the same class and the
answer tells us which buffer type is mishandled.

### T6 — coherency, write-back mapping

```
profile_knob: wb_mmap=false
```

### T7 — coherency, invalidation of other mappings

`implicit_others_inv` is still a live `PANVK_DEBUG` flag (`0005` did not remove it), so this one
stays an environment test. It needs no rebuild:

```
PANVK_DEBUG=implicit_others_inv
```

### T8 — control

```
profile_knob: (empty)
```

The unknobbed patched build. Known white. Re-run only if every other test is white, to confirm the
knob is not itself changing anything.

## Decision table

| T1 (bc_mode=COMPUTE) | reading | next |
|---|---|---|
| renders | the fragment BC shadow or AFRC is at fault | read `csf-v11/150`/`151`; T3 splits storage format |
| white | BC decode is not the cause | T4, then T5/T6, then T7 as an env test |

## Per-test checklist

1. Dispatch `panvk-icd-build.yml` with the test's `profile_knob` (or leave the environment at the
   baseline for T7, which is still an env var).
2. Import the `...-knob-...` zip as the Linux Vulkan driver and select it. Check the driver row's
   name: it carries the knob, so it names the test.
3. Confirm Lake's launch options hold no `PANVK_DEBUG` / `PANVK_BC_AFRC` (except for T7, whose
   value is `implicit_others_inv`).
4. Launch once. Let it reach the menu.
5. Share the session, one zip per test.
6. Say which test it was.

## Rules that keep a run valid

- One launch per share. A zip holds one session; a "two-scenario" zip cannot exist.
- Write env knobs in the `NAME=value %command%` form. Bare words become arguments and do nothing,
  and a run that did nothing looks exactly like a run that tested something (D1, 2026-10-23-05).
- Never leave a knob from the previous test in Lake's launch options. There is one shared
  environment, so a leftover key silently changes the next run too.
- If the game dies at startup instead of rendering, that is a **launch failure, not a result**. Send
  the DXVK line and it will be read as one.

## If every test comes back white

Then the bug is not in any of these paths, and the next step is the one the plan already names:
RenderDoc capture on the phone, replay on a desktop GPU. If the replay is correct, it is a driver
bug; then step the draws to find the first one that produces white and read that draw's shaders.

## What I got wrong earlier

I suggested `PANVK_DEBUG=no_bc_emul` and `PANVK_DEBUG=no_gs` in the PR comments. Both clear a
DXVK-required feature and would have failed at startup. The matrix above replaces that advice.

I also argued that the vignette exonerated the presentation path, on the grounds that a wrong
swapchain format could not leave dark features dark. That was wrong: a format or transfer-function
mismatch clips mid-tones to white while dark features stay dark, which is the exact symptom. The
D-tests above exist because of that error, and they are now the cheapest test in the file.

I then read the 2026-10-23-05 log as "D1 failed, so the WSI layer is refuted". That was wrong too.
The knob went into Steam's launch options **without `%command%`**, which Steam appends after the
executable, so it reached the game as an argument and the FROG layer -- which reads
`DISABLE_GAMESCOPE_WSI` from the environment, via its manifest's `disable_environment` gate -- never
saw it. The Proton log still contains the layer's full `[Gamescope WSI]` startup, so the layer was
active for that whole run. D-a is untested, not refuted. `droiddeck-game-env` now lifts bare
`NAME=value` launch options into the environment so this cannot silently happen again, and it prints
what it moved.
