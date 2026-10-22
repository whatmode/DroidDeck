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
test. Each test lists the complete per-game environment block; do not carry values over between
tests, because a leftover key silently changes the run.

## Why these and not the extension list

DXVK marks 53 features and extensions `require = true` (`src/dxvk/dxvk_device_info.cpp`). Hiding any
of them fails `checkDeviceCompatibility()`, `DxvkAdapter::isCompatible()` returns false, and the game
dies at startup instead of rendering. That kills the whole extension-bisection plan, and it kills
`no_bc_emul` (`textureCompressionBC`, line 868) and `no_gs` (`geometryShader`, line 834) too.

G715 reports `texture_features 0xc3fe001e`, and `0xc3fe001e & 0x1ff80 == 0`, so it has no native BC:
BC emulation is exactly what supplies `textureCompressionBC`. `no_bc_emul` therefore removes a
required feature rather than changing a code path. Forcing `PANVK_BC_MODE_COMPUTE` is the change
that keeps the feature and still moves the path (T1).

## Frozen baseline: identical in every test

| variable | value | why |
|---|---|---|
| `PROTON_LOG` | `1` | log collection |
| `DROIDDECK_HIDE_YCBCR` | `1` | the shipped mali default; keep it constant |
| `MESA_SHADER_CACHE_DISABLE` | *(unset)* | keep the cache in its normal state |
| `DXVK_CONFIG` | *(no bisection keys)* | `zeroWorkgroupMemory` is not a real DXVK option |
| `PANVK_DEBUG` | *(varies — the test variable)* | |

Do this **once**, before T1, not between tests:

```sh
rm -rf ~/.cache/mesa_shader_cache ~/.cache/dxvk
```

The "first two launches worked, then white forever" pattern is what a poisoned cache looks like, and
this rules it out for free.

## The tests

Each test is a **driver build**, not an env var. Dispatch `panvk-icd-build.yml` with the
`profile_knob` input below, import the resulting `...-knob-...` zip as the Linux Vulkan driver, and
launch once. The knob is folded into the zip name and `meta.json`, so the driver row names the test.
Leave the game environment at the frozen baseline above: no `PANVK_DEBUG`, no `PANVK_BC_AFRC`.

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
3. Confirm the game environment holds no `PANVK_DEBUG` / `PANVK_BC_AFRC` key under `games.1118240`
   or `shared` (except for T7, whose value is `implicit_others_inv`).
4. Launch once. Let it reach the menu.
5. Share the session, one zip per test.
6. Say which test it was.

## Rules that keep a run valid

- One launch per share. A zip holds one session; a "two-scenario" zip cannot exist.
- Never leave a `null` per-game override for a shared key. A null deletes the shared value and the
  run silently loses its whole environment.
- If the game dies at startup instead of rendering, that is a **launch failure, not a result**. Send
  the DXVK line and it will be read as one.

## If every test comes back white

Then the bug is not in any of these paths, and the next step is the one the plan already names:
RenderDoc capture on the phone, replay on a desktop GPU. If the replay is correct, it is a driver
bug; then step the draws to find the first one that produces white and read that draw's shaders.

## What I got wrong earlier

I suggested `PANVK_DEBUG=no_bc_emul` and `PANVK_DEBUG=no_gs` in the PR comments. Both clear a
DXVK-required feature and would have failed at startup. The matrix above replaces that advice.
