# White-frame test matrix (Lake, app 1118240)

> **Obsolete as of `mali-gl/patches/0005`.** The kbase tunables this matrix sets are no longer
> read from the environment: `PANVK_DEBUG`'s `bc_compute`, `bc_wide`, `no_bc_emul`, `no_gs`,
> `wsi_no_afbc`, `no_crc`, `no_wb_mmap`, `cached_before_coherent` and `gpu_prerast` are gone, and
> `PANVK_BC_AFRC` no longer exists. The driver now derives all of them from the GPU probe in
> `panvk_kbase_profile_for()`, so every test below runs the same code path and none of them can
> render. To compare two profile choices, edit that function and rebuild
> (`.github/workflows/panvk-icd-build.yml`). The matrix is kept only for its reading rules.

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
required feature rather than changing a code path. `bc_compute` is the switch that keeps the feature
and still changes the path.

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

`PANVK_DEBUG` takes a comma-separated list.

### T1 — is it the fragment-pass BC shadow?

```json
{"games": {"1118240": {"PANVK_DEBUG": "bc_compute"}}}
```

`csf-v11/150` sets `bc_gfx = bc_emul && arch >= 10 && 2D && !BC_COMPUTE && !NO_GS`. `bc_compute`
clears `bc_gfx` but leaves `bc_emul` set, so `textureCompressionBC` survives and DXVK still creates
the device. BC decode moves from the fragment-pass AFBC/AFRC shadow (`150`/`151`) to the old compute
path (`022`).

**Fixes →** the bug is in `150`/`151`. Go to T2.
**Still white →** BC shadow is innocent. Skip T2/T3, go to T4.

### T2 — is it AFRC specifically?

```json
{"games": {"1118240": {"PANVK_BC_AFRC": "0"}}}
```

Note: plain env var, not a `PANVK_DEBUG` flag, and it changes no feature bit. Default fragment shadow
path, AFRC off (`151`). Splits "the shadow is wrong" from "the AFRC encoding is wrong".

**Fixes →** the bug is the AFRC path in `151`. **Still white →** it is `150`'s shadow, not AFRC.

### T3 — is it the compact shadow format?

```json
{"games": {"1118240": {"PANVK_DEBUG": "bc_compute,bc_wide"}}}
```

Only run if T1 rendered but was still wrong. `bc_wide` keeps 16-bit storage for BC4/BC5/BC6H UF, so
this separates the compact-format choice from the storage format.

### T4 — is it the presentation-side AFBC?

```json
{"games": {"1118240": {"PANVK_DEBUG": "wsi_no_afbc"}}}
```

`csf-v11/033` enables AFBC for WSI images by default. A wrong AFBC presentation layout shows as a
uniform wrong frame, which is what we have. Does not touch any feature bit.

**Fixes →** WSI AFBC. **Still white →** not presentation.

### T5 — is it host/GPU cache coherency?

```json
{"games": {"1118240": {"PANVK_DEBUG": "cached_before_coherent"}}}
```

If this one fixes it, also run T6 and T7 individually, because they are the same class and the
answer tells us which buffer type is mishandled.

### T6 — coherency, write-back mapping

```json
{"games": {"1118240": {"PANVK_DEBUG": "no_wb_mmap"}}}
```

### T7 — coherency, invalidation of other mappings

```json
{"games": {"1118240": {"PANVK_DEBUG": "implicit_others_inv"}}}
```

### T8 — control

```json
{"games": {"1118240": {"PANVK_DEBUG": "no_crc"}}}
```

Known to be white from the 2026-10-22-06 run. Re-run it only if every other test comes back white, to
confirm the env plumbing is still live and the shares are landing. If T8 now renders, something
outside the driver changed.

## Decision table

| T1 | T2 | reading | next |
|---|---|---|---|
| renders | — | fragment shadow or AFRC | T2 to split them |
| white | — | not BC decode at all | T4, then T5-T7 |
| — | renders | AFRC encoding (`151`) | patch `151` |
| — | white | shadow layout (`150`) | patch `150` |

## Per-test checklist

1. Set the per-game block to exactly the JSON above. Replace the whole block; do not merge.
2. Confirm no other `PANVK_DEBUG` / `PANVK_BC_AFRC` key exists under `games.1118240` or `shared`.
3. Launch once. Let it reach the menu.
4. Share the session, one zip per test.
5. Say which test it was.

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
