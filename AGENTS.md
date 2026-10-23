# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

## PanVK/DXVK white frame (`mali-gl/patches/0002`-`0004`)

- The shipped ICD is the zenithblue `g615-v11-csf` series at `sources.lock`'s Mesa pin (`5a07217`).
  That pin predates three value-correctness fixes, all ported as `mali-gl/patches/0002`-`0004`:
  - `0002` (`81db71cf`): compute jobs copy the *shared* TLS size into their TSD instead of their own
    stack size. The buggy block appears three times in the CSF tree (`cmd_dispatch`, `cmd_precomp`,
    and DroidDeck's `gpu_prerast_launch_cs_desc`); all three need the size word copied, not just the
    pointer.
  - `0003` (`33e67876`): the register allocator's TLS spills/refills (`bi_load_tl`/`bi_store_tl`)
    dropped the forced-TLS memory-access hint that `43ffcf06` moved into a per-instruction field.
  - `0004` (`45ea3ed8`): TLS stores were not treated as helper-generating in the helper analysis.
- `allow_merging_workgroups` (`cf98950a`) is **deliberately not carried**. The `memset` in
  `pan_postprocess_nir` does clear the flag, and that is a real correctness issue, but the flag is
  consumed at dispatch (`panvk_vX_cmd_dispatch.c` writes it into `COMPUTE_SIZE_WORKGROUP`, and
  `panvk_vX_cmd_precomp.c` likewise), so enabling it turns on actual hardware workgroup merging.
  Before the fix the field was always false, which is safe. It did not fix the white frame, so a
  patch that changes how workgroups launch is not worth carrying for it.
- The shipped ICD is not byte-reproducible from this repo (its build CI is elsewhere), so `build.sh`
  has `PANVK_BASELINE=1` to build the vendor series with no patches. Always A/B a driver change
  against that baseline before blaming the patches for a failure to start.
- Do not "fix" this with DXVK config: `d3d11.zeroWorkgroupMemory` does not exist in DXVK 3.1.1
  (the keys are in `src/d3d11/d3d11_options.cpp`), so setting it does nothing. DXVK's compute
  shaders come from dxbc-spirv, which always emits `OpConstantNull` initializers for LDS variables;
  Mesa then always runs `nir_zero_initialize_shared_memory`, so `VK_KHR_zero_initialize_workgroup_memory`
  is already satisfied on this path.
- **Extension hiding is viable, contrary to an earlier note here.** DXVK marks 53 of its 174
  feature entries `require = true` in `src/dxvk/dxvk_device_info.cpp`, and `chainFeature()` gates a
  feature on the extension's `specVersion`, so hiding a `require = false` extension makes DXVK take
  its fallback path. Only six extensions carry a required feature: `VK_KHR_swapchain`,
  `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`, `VK_KHR_maintenance6`,
  `VK_EXT_depth_clip_enable`, `VK_EXT_robustness2`. `VK_LAYER_DROIDDECK_hideext`
  (`DROIDDECK_HIDE_EXTS`) hides the rest and refuses those six. Prefer a DXVK `DXVK_CONFIG` key
  when one exists (`dxvk.tilerMode`, `dxvk.enableUnifiedImageLayouts`, `d3d11.disableDirectImageMapping`,
  `dxvk.zeroMappedMemory`): it needs no layer and no rebuild.
- `dxvk.tilerMode` is the highest-value DXVK-side switch to try first, because PanVK is a tiler and
  zink's GL path is not: it turns on submission heuristics that only exist on a tiler, which matches
  "DXVK uses something zink does not".
- `mali-gl/scripts/build.sh` builds GL only (`-Dvulkan-drivers=`). The PanVK ICD is built by
  `tools/panvk/build.sh` (`.github/workflows/panvk-icd-build.yml`) and imported into the app as a
  Linux Vulkan driver zip.

## The white frame: desktop mode is the working control

The menu renders correctly in desktop mode. `droiddeck-session` execs `droiddeck-desktop` at line
243, before `mali_gl_setup` (260) and before the gamescope block (291-296), so desktop mode runs
**KWin** as the guest compositor and never starts gamescope. Steam mode runs gamescope, and
gamescope is the only difference: its WSI layer (`-e`,
`libVkLayer_FROG_gamescope_wsi_aarch64.so`), its hand-built linux-dmabuf tranche (patch `0121`,
whose own header says it is not device-tested on this GPU) and its GPU-side composite of the game
buffer. Both modes present into the app's native compositor, so the compositor itself is common.

Do not dismiss the presentation path because the vignette proves the game's post-processing ran. A
format or transfer-function mismatch clips mid-tones to white while dark features stay dark, which
is exactly "white with a vignette". The cheapest test is `DISABLE_GAMESCOPE_WSI=1` for the game
(no rebuild, no layer); the open question is whether it reaches a Proton game Steam launches
itself, so check `/proc/<pid>/environ` rather than assuming.

## The shipped RC4 ICD is not reproducible from `panvk-kbase-v0.1.0-rc4`

- The vendor tag `panvk-kbase-v0.1.0-rc4` resolves to commit `5b87b74d` (2026-10-16), and the
  release notes state outright that the build's source "was not committed ... built on 2026-10-17
  from the patch series of that day". So the tag is a reference point, not the shipped source.
- `5b87b74d`'s `patches/csf-v11` stops at `186`. The vendor's own RC4 binary analysis
  (`worklogs/g615-dxvk/rc4-binary-analysis.md`) finds strings that only `187`-`196` add, so the
  shipped RC4 binary was built from a series that includes those.
- Consequence: an ICD built from `5b87b74d` differs from the shipped driver by far more than the
  `mali-gl/patches` fixes. The earlier "patched vs baseline" A/B could not attribute a startup
  failure to the patches, because the control was not the shipped driver either.
- Build from `150c7cdd` (2026-10-16) instead: the last vendor commit whose `csf-v11` series ends at
  `196`, which matches what RC4 contains. `5b87b74d..150c7cdd` is 11 commits, all patch/worklog.
- `PANVK_DEBUG=no_crc` exists in this series (`csf-v11/018` adds `{"no_crc", PANVK_DEBUG_NO_CRC}`)
  and disables transaction elimination (CRC) tile write-out. Worth a launch before blaming the
  compiler: `csf-v11/019` is a CRC class where tiles stayed CRC-skipped and read back stale.

## Reading a log zip: two traps that silently invalidate an A/B run

Both were hit on the 2026-10-22-03 session, so check for them before trusting any result.

- There are no per-game environment overrides any more. `game-environment.json` carries a single
  `shared` map, so a diagnostic env var in `shared` reaches every game, and there is no UI row that
  could cancel it. Read the published file in the zip to confirm what actually applied.
- The zip's top-level directory is the session, and one session is one launch. A zip with a single
  session dir covers only that launch, whatever the tester says it contains. `device.txt` names the
  selected driver from `meta.json` ("Linux driver") and `session.log` names the imported
  `linux_vulkan_drivers/<id>`; read both to tell the patched ICD from the baseline one.
- `d3d11.zeroWorkgroupMemory` is a no-op in DXVK 3.1.1 (see above), so its presence in a run's
  `DXVK_CONFIG` is a red herring, not a variable under test.

## The white frame is not in `mali-gl/patches` (baseline A/B, 2026-10-22)

Two sessions, same env, `PANVK_DEBUG=no_crc` in `shared` (so it applied):

- `2026-10-22-03-steam` (patched ICD, `237e96e4`): white.
- `2026-10-22-06-steam` (baseline ICD, `4e2b5d7f`, vendor series with none of the patches): white.

So `0002`-`0004` are not the cause and must not be blamed for it. `no_crc` is also not the fix,
which clears the CRC/transaction-elimination class (and `csf-v11/019`). Both runs also show the
same `texture_features 0xc3fe001e`, "BC emulation on", and `MESA_SHADER_CACHE_DISABLE` off.

A third run on the `150c7cdd` pair (patched `b81707dc`, carrying `0002`-`0005`) is also white.
That is the build that first carried `0005`, so it closes the "the pin is stale / `0005` was the
missing fix" reading as well: white on `150c7cdd` with and without the patches. It does not test
`0005` as a cause, because `0005` is a refactor that changes no rendered output by design.

Consequence: stop A/B-ing the patch series against the rebuild. Every build in this repo renders
white, and the two refs are white too, so the difference from the shipped RC4 is not what is being
measured. The next test has to change one driver behaviour at a time *inside* the build -- which
now means editing `panvk_kbase_profile_for()` and rebuilding, since `0005` removed the env switches
that used to do it (see above). `no_crc` was the last one that could be set from the guest.

- What DXVK uses and zink does not, in this driver: BC emulation (`csf-v11/039` + `108`, default on)
  and the `gpu_prerast` geometry-shader path (`csf-v11/048`, default on). Both are DXVK-shaped and
  both are value-correctness risks.
- **`no_bc_emul` and `no_gs` are NOT usable discriminators.** Both clear a feature DXVK marks
  `require = true` (`src/dxvk/dxvk_device_info.cpp`): `no_bc_emul` clears `textureCompressionBC`
  (line 868) and `no_gs` clears `geometryShader` (line 834). DXVK then refuses the adapter
  (`checkDeviceCompatibility` -> "Device does not support required feature ..."), so the run fails
  at startup and produces no rendering result at all. Same trap as the `VK_KHR_maintenance5` round.
- **`PANVK_DEBUG=bc_compute` no longer exists in the patched ICD.** `0005` deleted the whole
  `panvk_debug_options` block that carried `bc_compute`, `no_crc`, `no_bc_emul`, `no_gs`,
  `gpu_prerast`, `bc_wide`, `wsi_no_afbc`, `no_wb_mmap` and `cached_before_coherent`, and moved
  those decisions into `panvk_kbase_profile_for()`. So on a build that carries `0005` the recorded
  "use `PANVK_DEBUG=bc_compute`" advice is dead: that switch cannot select the compute BC path any
  more, and neither can `PANVK_BC_AFRC` (`0005` deletes it too). In a `0005` build the only way to
  change one of these is to edit `panvk_kbase_profile_for()` and rebuild -- which is also why a
  "patched vs baseline" zip A/B cannot discriminate a BC-path bug: both builds run the same BC path.
- On a build **without** `0005` (e.g. `237e96e4`), `PANVK_DEBUG=bc_compute` does work: it sets
  `bc_gfx = false` while leaving `bc_emul = true` (`csf-v11/150`), so `textureCompressionBC` stays
  set and DXVK still creates the device, but BC decode falls back from the fragment-pass AFBC/AFRC
  shadow to the old compute path. That isolates `csf-v11/150`/`151`.
  `PANVK_BC_AFRC=0` (a plain env var, not a `PANVK_DEBUG` flag) then splits AFRC off from the
  fragment shadow; it changes no feature bit.
- The six extensions DXVK requires (see the PR#1 list) cannot be hidden without killing the launch,
  so the extension bisection is closed for this bug.
- `proton/steam-<appid>.log` is truncated from the front (`[... first N bytes left out ...]`) when
  large, so capability blocks near the top are absent from a long run. `session.log` keeps the
  `MESA: info:` lines.

## The pinned vendor ref is not the tip, and `main` is where the fixes are

`ZENITHBLUE_REF` is `150c7cdd` because that is the last ref whose `csf-v11` series matches the
shipped RC4 binary. It is not the vendor's current tree. `zenithblue-oss/panvk-kbase-android` `main`
is at `csf-v11/212` (the pin stops at `196`), same Mesa pin `5a07217f034b`, and the gap is almost
entirely the BCn path this GPU always runs plus the `gpu_prerast` arenas.

- **`csf-v11/205`** commits the 64 MiB `gpu_prerast` and 96 MiB tessellation arenas at the first
  draw that needs them, not at `VkDevice` creation. On MediaTek kbase, GPU-fault growth on those
  arenas **loses the first draw's output**, which is why they were committed and CPU-cleared up
  front. Every DXVK device paid 160 MiB for it.
- **`csf-v11/193`** is the clearest statement of the failure mode this repo keeps meeting: the GS
  passthrough copied whole `vec4` slots, so clip lanes past the clip array size "kept whatever the
  prerast arena held" — the result depended on what an earlier draw left in the arena. Uninitialized
  arena content reaching a shader, no fault, wrong values.
- **`csf-v11/209`** (util/u_queue) restarts the queue workers in a `fork()` child. A `fork()` child
  has only the forking thread, so a job queued in the child never runs and `util_queue_finish()`
  blocks forever. DroidDeck runs the whole stack under proot and Wine forks constantly, so this
  class is live here in a way it is not on a plain Android app.
- **`csf-v11/197-212`** rework BC decode (ETC2 transcode, BC6H/BC7 decode, BC1 palette fit) and
  `207` makes BC raw blocks lazy — readable BC images stop keeping raw blocks until something reads
  one back.

Two consequences for this repo:

- **A ref A/B is a dispatch, not an edit.** `workflow_dispatch` takes `zenithblue_ref` and
  `patch_numbers`, so building `main` with `0002 0003 0004` is one click.
  `PANVK_PATCH_NUMBERS` in `build.sh` selects the set; a named patch that does not apply still fails
  the build, so nothing is skipped silently.
- **`0005` does not apply to `main`** (it reworked `panvk_instance.c`/`panvk_physical_device.c`).
  Build `main` with `0002 0003 0004`, or rebase `0005` first.

The ICD zip name and the driver row both carry the vendor ref (`-zb<sha>`), because the Mesa pin is
identical across refs and an A/B would otherwise publish two assets under one name.

## The kbase tunables are now automatic (`mali-gl/patches/0005`)

The per-game switches the earlier sections reach for are gone. `0005` adds
`src/panfrost/vulkan/panvk_kbase_profile.h`: one `struct panvk_kbase_profile`, filled once per
physical device from `pan_kmod_dev_props` by `panvk_kbase_profile_for()` and stored in
`panvk_physical_device::profile`. Every site that used to read an environment variable now reads
that struct, so the same GPU always gets the same behaviour and no launcher-side env is needed.

- Removed env knobs (all read at runtime before): `PANVK_DEBUG`'s `no_bc_emul`, `bc_compute`,
  `bc_wide`, `no_gs`, `wsi_no_afbc`, `no_crc`, `no_wb_mmap`, `cached_before_coherent`,
  `gpu_prerast`, plus `PANVK_BC_AFRC`, `PANVK_KBASE_DVFS`, `PANVK_KBASE_HEAP_RENEW_INTERVAL`,
  `PANVK_KBASE_HEAP_RENEW_WORK`, `PANVK_KBASE_GPU_SEMAPHORE_WAITS`. The kbase DVFS sysfs
  frequency pinning and its helpers are deleted outright.
- Kept: the diagnostic-only toggles that never changed a shipped code path --
  `force_simultaneous`, `implicit_others_inv` (`PANVK_DEBUG`), and the trace/debug options
  (`PANVK_FAN_CHUNKS`, `PANVK_KBASE_ASYNC_HEAP_TERM`, `PANVK_V9_RT128`, `PANVK_ZS_PRELOAD_ALWAYS`,
  `PANVK_DESC_TRACEBUF_SIZE`, `PANVK_CS_TRACEBUF_SIZE`, `PANVK_UTRACE_CLONE_MEM_SIZE`).
- The arch gate no longer needs `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER` on the DRM path (v6/v7/v11/v14
  are what this series supports). The kbase path never had it.
- **The test matrix in `docs/white-frame-test-matrix.md` is obsolete as written.** Every `PANVK_DEBUG`
  value it sets except `force_simultaneous`/`implicit_others_inv` is now ignored, and `PANVK_BC_AFRC`
  no longer exists. The matrix has been rewritten to drive those tests through
  `PANVK_PROFILE_KNOB` (a `workflow_dispatch` input on `panvk-icd-build.yml`) instead: each row is
  one build with one `panvk_kbase_profile_for()` field overridden, and the knob is folded into the
  zip name and `meta.json` so the driver row names the test. `implicit_others_inv` (T7) is still a
  live `PANVK_DEBUG` flag and needs no rebuild.
- **Every value reproduces the vendor series' shipped default.** The point of `0005` is to make the
  environment unable to change rendering, not to retune it: `bc_mode` is `FRAGMENT_SHADOW` on v10+,
  `COMPUTE` below v10, and `NATIVE` only where the probe reports all ten BC formats
  (`panvk_kbase_has_native_bc`, mask `0x1ff80` -- the ten `MALI_BC*` texfeat bits 7-16). Geometry
  shaders are advertised from v9 on. `cached_before_coherent` stays **false**: it is a
  CTS-breaking perf experiment upstream, and it was off by default. Tiler heap geometry is
  1 MiB/10/400 (the kbase small-page path); the DRM path keeps its own 2 MiB/5/64. Heap renewal is
  128/65536.
- Consequence: `0005` is a refactor, not a fix. It changes no rendered output by itself. It removes
  the whole class of "the report describes the tester's environment" bugs and it is the prerequisite
  for any real white-frame fix, which is then a one-line change to `panvk_kbase_profile_for()`.

## The app has one shared game environment (no per-game profiles)

The per-game compatibility profile feature is gone from the app, not just hidden. A game now has
exactly two user-selectable things: its Proton build and its driver bundle.

- `GameEnvironment.Config` holds only `shared`. `effective(config, preset)` takes no scope, and
  `withValue(config, name, value)` is the only mutator. There is no `validScope`, no `games` map,
  no `NO_STEAM`/`CUSTOM_FEX_PRESET`/`CUSTOM_SYNC` sentinels. `withEntries()` now replaces the whole
  `shared` map, where it used to replace one scope's entries, so a `withEntries()` chained after a
  constructor that already set values drops them.
- The guest `game-environment.json` carries `version`, `shared` and an optional `dxvkConfig`. The
  `games` and `dxvkConfigGames` sections are gone; `apply_config(env, config, fixes=None)` and
  `sync_config(config)` take no app id. Old files with those sections still load, because the guest
  only reads the keys it knows.
- Per-game FEX preset and sync backend are gone with the profiles. Start-without-Steam moved from
  `DROIDDECK_NO_STEAM` in the environment to the `noSteamGames` set in the `session` prefs; it is
  still a per-game toggle (the game settings card), but it no longer rides the environment file.
- Deleted with the feature: `GameProfileManager`, `GameProfileFollowState`,
  `GameProfileRefreshState`, `GameProfileInventoryCache`, `SelectedGameProfile`,
  `LatestRequestCoordinator`, `TextureFilteringPrefs`, `ComponentsManager.gameComponents`/
  `setGameComponent`/`clearGameComponents`, `ProtonDefault.gameChoice`/`gameSelectedId`/
  `requestGame`/`parseGames`, `SessionPrefs.gameTextureFiltering` and friends,
  `SettingsReset.resetAll(context, gameProfiles, gameScopes)`, `ProtonDefault.GAMES`
  (`droiddeck-compat/games.json`), and `steam-compatibility`'s `requested_games`/`game_requests`/
  `DEFERRED_GAME_TOOL`.
- A `droiddeck-compat/games.json` left on a device from an older build is inert: nothing reads it.
  Do not "restore" it as a diagnostic, and do not add a per-game env var path back for one game --
  that is what made the earlier A/B runs unreadable.
