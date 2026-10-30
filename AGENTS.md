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
is exactly "white with a vignette". The cheapest test is `DISABLE_GAMESCOPE_WSI=1`, which the FROG
layer reads from its manifest's `disable_environment` gate -- **as an environment variable**.

The app has no environment editor any more (see below), so the route is Lake's own Steam launch
options, in Steam's documented `NAME=value %command%` form:

```
DISABLE_GAMESCOPE_WSI=1 %command%
```

The `%command%` is what makes it an environment variable. Without it, Steam appends the words after
the executable and they arrive as arguments, where nothing reads them: on 2026-10-23-05 the knob was
typed bare, the layer stayed active for the whole run, and the result was read as "D1 refuted" when
the test had not run at all. `droiddeck-game-env` now lifts bare `NAME=value` words into the
environment too and logs what it moved, so check for that line, and confirm the knob in
`/proc/<pid>/environ` before trusting a launch either way.

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

## Reading a log zip: the traps that silently invalidate an A/B run

All of these were hit on a real session (2026-10-22-03 for the first two, 2026-10-23-05 for the
third, 2026-10-28-06 for the last), so check for them before trusting any result.

- There are no per-game environment overrides any more. `game-environment.json` carries a single
  `shared` map, so a diagnostic env var in `shared` reaches every game, and there is no UI row that
  could cancel it. Read the published file in the zip to confirm what actually applied.
- The zip's top-level directory is the session, and one session is one launch. A zip with a single
  session dir covers only that launch, whatever the tester says it contains. `device.txt` names the
  selected driver from `meta.json` ("Linux driver") and `session.log` names the imported
  `linux_vulkan_drivers/<id>`; read both to tell the patched ICD from the baseline one.
- **A knob set in Steam's launch options is an argument, not an environment variable -- unless it
  is written in Steam's `%command%` form.** Steam appends launch options after the executable, so
  bare `NAME=value` words arrive as argv and nothing reads them: not Proton, not DXVK, not
  gamescope, not a Vulkan layer. Only `NAME=value %command%` puts them in the environment. On
  2026-10-23-05 `DISABLE_GAMESCOPE_WSI=1` was passed bare, the FROG layer stayed active (its
  `[Gamescope WSI]` lines are in the Proton log), and the "D1 refuted" reading was wrong: the test
  never ran. `droiddeck-game-env` now moves bare tokens into the environment and logs
  "moved into the environment: ...", so check for that line; for an older zip, grep the log for
  the knob's own evidence (the layer's lines, the driver's) rather than for the string itself.
- `d3d11.zeroWorkgroupMemory` is a no-op in DXVK 3.1.1 (see above), so its presence in a run's
  `DXVK_CONFIG` is a red herring, not a variable under test.
- **`device.txt`'s `Version 0.4.0 (13)` does not identify the build, and neither does the driver's.**
  `versionCode`/`versionName` in `app/build.gradle` are bumped by hand and had not moved for many
  commits, so every install reports the same version and a report from a stale APK looks current.
  The guest driver was equally build-invariant until this session (see the `dd.<sha>` note below).
  Check what a bundle actually carries before accepting a result: `proton/game-environment.json`
  shows whether app-side changes are present (e.g. a `dxvk.maxFrameRate` in `DXVK_CONFIG` means the
  bundle predates `55fa3d8`), and the driver's own markers in `droiddeck_vulkan.txt` say which
  guest patches were applied (e.g. `deferring to the real pool` for 0023, `decoding on the GPU` for
  0024). A bundle can also be a byte-identical **prefix** of a later one -- the 2026-10-28-06
  bundle is the first 20,759 lines of 2026-10-29-02's `droiddeck_vulkan.txt`, same md5 -- so
  compare line counts and hashes before treating two bundles as two data points.
  `DeviceReport` now prints `Build`/`Commit` from `BuildConfig` for reports made after this note.

## The 2026-10-23-06 D1 run: D1 applied, and the reboot is a GPU fault

**Getting the bundle out of fex.net.** The share page is a SPA and the `fs*.fex.net/download/...`
URL returns HTML on its own. Fetch the anonymous token and send it as the `token` **cookie** - an
`Authorization` header returns 410:

```
TOK=$(curl -s https://api.fex.net/api/v1/config/anonymous | jq -r .anonymous.anonym_token)
URL=$(curl -s https://api.fex.net/api/v1/file/share/<link> | jq -r '.tree.children[0].download_url')
curl -L -H "Cookie: token=$TOK" "$URL" -o session.zip
```

- **D1 applied this time.** `DISABLE_GAMESCOPE_WSI=1` was written in the `%command%` form, so it
  reached the game's environment (`console_log.txt`: `DISABLE_GAMESCOPE_WSI=1 ... waitforexitandrun
  ... Lake.exe`, the knob before the executable), and the game's Proton log has **zero**
  `[Gamescope WSI]` lines (2026-10-23-05 had eight). The 15 `[Gamescope WSI]` lines in
  `session.log` belong to `steamsysinfo`, `explorer.exe` and `d3ddriverquery64.exe`, which run
  before the game and do not get the knob. D1 renders.
- **But the run is two-variable.** `device.txt` and `session.log` both name the selected driver as
  `DroidDeck-panvk-glibc-knob-bc_mode-PANVK_BC_MODE_COMPUTE_zb150c7` - the T1 knob build, not the
  baseline. This is D1 + T1 at once, so the render cannot be credited to the WSI layer alone.
  Re-run D1 on the baseline, or read T1's own row, before concluding D-a.
- **The reboot is a GPU fault, not memory or thermal.** The Proton log ends with
  `MESA: error: kbase: CSF group 0 fatal error: status 0x00c00488 (exception 0x88)`, then
  `Presenter: vkWaitForPresentKHR failed: VK_ERROR_DEVICE_LOST` and five
  `DxvkSubmissionQueue: Command submission failed: VK_ERROR_DEVICE_LOST`; the app logs
  `DroidDeckWayland: [error] GPU device lost (VK_ERROR_DEVICE_LOST in present): the compositor has
  stopped presenting`. `crash.log` is empty (nothing crashed in userspace) and `device.txt` shows
  thermal `NONE` with 2.8 GB free, so the device went down on the GPU fault.
- **Decode the exception with panthor's tables** (`drivers/gpu/drm/panthor/panthor_device.h`):
  `CS_EXCEPTION_TYPE(x) = x & 0xff`, so `0x00c00488` is type `0x88` =
  `DRM_PANTHOR_EXCEPTION_GPU_SHAREABILITY_FAULT` (data `0x00c004`). A *shareability* fault is a
  memory-attribute fault - the GPU touched a mapping with a shareability it does not permit - so
  this points at the coherency/attribute class the T5/T6/T7 rows test, not the BC/format class.
- **The zip carries no kernel log.** `crash.log` is the Android crash buffer only, and it is empty.
  Ask for `dmesg` (or a `mali_kbase` trace) alongside a fault; the faulting job and address live
  there, not in the app's files.
- **A display-mode flap storm runs alongside it.** `app.log` holds 2242 `perf: display` lines in
  ~3 minutes, alternating `60 → 120 Hz (mode 1)` and `120 → 120 Hz (mode 255)` about every 40 ms.
  `PerfMode.apply` writes `window.attributes.preferredDisplayModeId`, which fires `onDisplayChanged`
  → `followPanelRefresh` → `applyOutputPacing` → `PerfMode.apply` again. The two lines are the same
  120 Hz mode under two ids, and `selectMode` picked its winner with `maxByOrNull { it.refreshRate }`,
  which returns whichever of the tied modes comes first in `supportedModes` - and that order follows
  the current mode, so the winner flipped on every write and the `preferredDisplayModeId != best.modeId`
  guard never matched. Fixed: the choice now keeps the current mode when it is already allowed and
  as good, and otherwise breaks the tie on the mode id, so it cannot depend on the array order and
  the guard converges. `PerfModeTest.twoIdsForTheSameRefreshDoNotAlternate` locks the old code out.
  A deterministic simulation of the loop gave 50 writes in 50 iterations before and 1 write, then
  stable, after.

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
- Diagnostics use Steam's own per-app launch options in the `NAME=value %command%` form, which
  `droiddeck-proton-run`'s `split_options()` and Steam itself both define as "before `%command%` is
  the environment, after it is the program's arguments". That is Steam's UI, not a DroidDeck
  environment editor, so it does not reintroduce the path this section removes: nothing in the app
  writes a per-game key to `game-environment.json`.
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

## Compile-check the Venus guest ICD locally (`tools/venus/check-guest.sh`)

A change to the guest ICD does not need a 30-60 minute CI round to find out whether it compiles.
`tools/venus/check-guest.sh <mesa-src> [build-dir]` configures a Mesa build dir the same way
`build-guest.sh` does and compiles every guest driver object (`src/virtio/vulkan/vn_*.c`, minus the
Android-only `vn_android.c`) with the build's own flags. A clean tree compiles in about a minute;
after editing one file it takes seconds, because the build dir is reused.

- Take the flags from the build, not from a hand-written list. `ninja -t commands <obj>` is the
  source of truth; a copied `-D` list drifts (it needs 111 defines here, including
  `-DPACKAGE_VERSION` and the `HAVE_*` set) and produces errors that are the harness's, not the
  driver's.
- The script needs `ninja`, `meson`, `pkg-config`, the X11/xcb/Wayland dev headers and libdrm.
  On a bare Debian container that is
  `apt-get install -y pkg-config libdrm-dev libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev
  libxcb1-dev libxcb-dri3-dev libxcb-present-dev libxcb-shm0-dev libxcb-sync-dev
  libxcb-randr0-dev libxcb-xfixes0-dev libxshmfence-dev libxrandr-dev libxxf86vm-dev
  libwayland-dev libwayland-egl-backend-dev` plus `pip install ninja meson mako packaging`.
  The failures are misleading without them: a missing `packaging` module surfaces as "mako
  module >= 0.8.0 required", and a missing `pkg-config` as a libdrm lookup failure. CI's guest
  job already installs all of it.
- **`pkg-config` alone is not enough on this image, and it is not a libdrm lookup failure.**
  `wayland-scanner` is needed too: without it meson's regenerate step dies at
  `meson.build:1989: ERROR: Could not get pkg-config variable and no default provided for
  <PkgConfigDependency wayland-scanner>`, which reads like a wayland problem but is the same
  missing-tool class as the rest. The package is `libwayland-bin` on trixie; `wayland-scanner`
  itself is not a package name. `bison`/`flex` are also reported missing (non-fatal warnings) and
  worth installing so the log is not noisy.
- Once `pkg-config`, `libwayland-bin`, `bison`, `flex` and the X11/xcb dev headers are in, the real
  `check-guest.sh` runs to completion and reports **all 26 guest files passed** -- no stubs and no
  hand-rolled compile loop needed. Prefer that over the stub route below; the stub route is only for
  an image where the packages genuinely cannot be installed.

- `vn_android.c` is Android-only: meson adds it under `with_platform_android` and it needs the NDK's
  `vndk/hardware_buffer.h`. Skip it on the Linux side.
- `build-guest.sh` also publishes `mesa-virtio-src.tar.gz` "enough to compile-check a driver change
  outside this build". That is only true now that the tar list carries the headers the driver pulls
  in but the virtio/GL dirs do not: `include/vk_video`, `src/c11`, `src/compiler/shader_enums.h`,
  `src/compiler/spirv/spirv.h`, and `include/{c99_compat,no_extern_c,renderdoc_app}.h`. Drop one and
  the published tree fails to compile before it reaches any driver code, which is the shape of the
  header errors seen while generating headers by hand (a hand-rolled gen-script loop over the
  tarballs cannot re-run at all without `vk.xml`).
- The published `mesa-virtio-gen.tar.gz` holds generated headers but no `src/vulkan/registry/vk.xml`,
  so the gen scripts cannot be re-run from the tarballs alone; the local harness sidesteps this by
  compiling against a real checkout's build dir.
- Where the dev headers cannot be installed (a container with no root), the check still runs by
  dropping minimal stub headers for the X11/xcb/Xrandr types `include/vulkan/vulkan*.h` needs onto
  an extra `-I`, then compiling each `vn_*.c` with the build dir's recorded flags (the ones in
  `compile_commands.json` are the same `ninja -t commands` output). Every guest file compiles that
  way except `vn_renderer_virtgpu.c`, which needs `xf86drm.h` (libdrm) and fails before reaching
  any driver code — a missing system header, not a driver problem.
  - Do not run `check-guest.sh` itself for this: without `pkg-config` it dies in meson's
    `regenerate` step before compiling anything, and the reported error names whatever tool meson
    reached next (`wayland-scanner` was the one seen here), not the tool that is missing. Go
    straight to the stub-`-I` + `compile_commands.json` route.
  - The stubs that were enough: `xcb/xcb.h` (the `xcb_*_t` typedefs), `X11/Xlib.h`,
    `X11/Xlib-xcb.h`, `X11/extensions/Xrandr.h`. The first run only reports the first missing
    header, so add them one at a time and recompile; the errors converge in three passes.
  - Watch the `-MD`/`-MQ`/`-MF`/`-MT` flags in the recorded command. Compiling with `-o /dev/null`
    but leaving `-MD` in gives "to generate dependencies you must specify either -M or -MM", and
    the real errors stay hidden behind it. Drop the whole dependency-tracking group.

## The 24 fps ceiling: the guest drains the GPU before every present (`tools/venus/guest-patches/0021`)

The guest ICD sits near 24 fps on titles whose BC decode is only 4.6 ms of a ~41 ms frame, so the
cost is not decode. `vn_queue_wsi_present` (`src/virtio/vulkan/vn_queue.c`) explains it: the
renderer reports `has_implicit_fencing = false` (`vn_renderer_vtest.c`) and
`DRI_CONF_VENUS_IMPLICIT_FENCING` defaults to false (`vn_instance.c`), so the present takes the
else-branch that calls `vn_QueueWaitIdle` **once per present**. That is a full GPU drain with no
CPU/GPU overlap: the CPU cannot start frame N+1 until frame N is done, so the frame time is CPU
time plus GPU time in series instead of their maximum.

- Two ways to confirm before changing anything, both already instrumented:
  - `vn_QueueWaitIdle` -> `vn_WaitForFences` -> `vn_relax_fini` is already reported as
    `VN_RELAX_REASON_FENCE` in `droiddeck_vulkan.txt` by the "who waits" code (0017). If the drain
    is the ceiling, that reason dominates the per-frame time.
  - 0021 adds a frame-rate line to `droiddeck_vulkan.txt` every 10 s:
    `frames: N fps (M ms/frame); present GPU wait W ms/frame`. If `W` is most of `M`, the present
    drain is the frame.
- **A/B it with no rebuild.** `driParseOptionInfo` (`src/util/xmlconfig.c`) overrides a driconf
  default from an environment variable named exactly like the option (`os_get_option`, i.e.
  `getenv`). So `venus_implicit_fencing=true` in the environment takes the other branch, and
  `venus_implicit_fencing=false` forces the drain — no `.drirc`, no recompile. The XML section name
  is `venus`. This is the cheapest first experiment; the caveat below is why it may not help.
- `tools/venus/patches/0020` adds a second per-present wait that matters here: in
  `wsi_common_queue_present`, when `swapchain->dd_cpu_present` (the CPU-present path 0020
  introduces), it calls `wsi->WaitForFences(..., UINT64_MAX)` on the blit fence before presenting.
  `dd_cpu` is set when the X server has no DRI3 or no present (which is this guest), so that wait
  is taken. The fence is the one passed to the blit `QueueSubmit`, so waiting on it is another
  `vn_WaitForFences` -> `vn_relax_fini`, landing in the same `VN_RELAX_REASON_FENCE` bucket as the
  `vn_QueueWaitIdle` drain. So there are two places to remove the per-present sync, not one.
- **The drain is now off by default, and `=1` is a seqno wait rather than the drain.** 0021 added
  `DROIDDECK_VN_SYNC_PRESENT` with the old behaviour as the default (a no-op unless set to 0)
  because correctness had not been shown. The on-device numbers settled it: on Lake with FEX
  defaults the drain was **89% of the frame** (present GPU wait 17.6-18.0 ms of a 19.8 ms frame)
  and the game's own wait report agreed (`fence 1558x 5952.8 ms`, `semaphore 1560x 5838.1 ms` in
  one 10 s window). The default is now `false`. `a0deac6` then changed what `=1` *does*: it waits
  on the present's own ring seqno instead of calling `vn_QueueWaitIdle`, and falls back to the
  drain only when the submit carries no valid seqno. So the three states are: unset and `=0` are
  the same fast path, and `=1` orders the present after its submit without parking the CPU until
  the GPU idles. The white-frame symptom `=1` exists for is covered by that ordering only if the
  seqno is sufficient for it, which is reasoning from the implicit-fencing path in the same
  function and was never measured. To get the *old* drain back, use a build predating `a0deac6`.
- The caveat that still stands: `venus_implicit_fencing` only switches the guest's assumption
  about the kernel driver. Here the server and the Android driver are also in play, and the
  virtio-gpu kernel driver treats everything as one fence context ("no synchronization needed").
  Skipping the guest drain stops the guest blocking on the queue, but the `WaitForFences` above
  (the CPU-present blit fence, 0020) still blocks the present until the blit fence signals. That
  wait is not removable on its own: the CPU is about to read the image, so it must wait for the
  blit. If a title still tears after this default, that wait is the next suspect.
- What the frame line decided: `present GPU wait` was most of `ms/frame`, so the drain was the
  frame, and skipping it is the fix. With FEX defaults the CPU side of the frame fell to ~2 ms
  (it was ~16 ms at 24 fps), so the drain was all that was left between the game and 60.
- **The bottleneck moved, and the fix is a knob, not a rebuild.** Same 0021 frame line, two runs:
  at 24 fps the CPU was ~16 ms of the frame and the GPU wait ~4 ms; with FEX defaults the CPU is
  ~2 ms and the GPU wait is ~17.6 ms. `1000/18 = 55 fps` is the ceiling while the drain
  serialises the two, and the measured peak was 51.0 fps, consistent with that plus jitter.
  Removing the drain should let CPU and GPU overlap.
- **A `frames:` line near 59-60 fps is vsync, not a ceiling to chase.** gamescope's own engine
  holds 59.1-59.4 fps at a steady 16.85 ms for 50 s, which is the 60 Hz panel. That is also why
  lowering the game's resolution changes nothing: the cost being measured is per-frame (the drain,
  then the GPU work), not per-pixel. The game renders 1920x1080 into gamescope and gamescope
  composites to the 1602x720 screen, so the game's own resolution setting does not move this.
- Both the `wsi_mem` drain and the `mem_signal` chaining only happen on the non-dma-buf path.
  `has_external_sync = false` on the vtest renderer, so `wsi_prepare_signal_dma_buf_from_semaphore`
  returns `VK_ERROR_FEATURE_NOT_PRESENT` and the submit chains `wsi_memory_signal_submit_info`,
  which is what sets `submit->wsi_mem`. If external sync is ever enabled, the drain stops running
  and this whole analysis moves.

## BC decode is parallel now (`vn_droiddeck_bc.c`), and the numbers behind it
The BC upload path (`vn_dd_bc_submit` -> `bc_decode_cmd`) decodes on the submit thread, so its
cost lands directly on `vkQueueSubmit`. It is now split across a `util_queue` of one thread per
CPU (`DROIDDECK_VN_BC_THREADS` overrides; `=1` keeps the old serial path).

- The decode is embarrassingly parallel across (job, slice) pairs and safe to split further
  along rows: `util_format_translate` writes only the destination band it is given. `bcpar.c`
  and `bcsplit.c` confirm this byte-for-byte for BC1/3/4/5/6H/7 and sRGB variants, 384/384 cases
  exact, including heights that are not multiples of 4.
- Measured on the 4-core sandbox (AMD EPYC): BC3 4096x4096 goes from ~178 ms serial to ~71 ms at
  4 threads (2.5x). Scaling is ~2x on 2 cores, which is the ceiling on this shared sandbox, not
  the phone. The phone's 8 cores (X4 + 3xA720 + 4xA520) should scale better.
- Do not add a "too small to thread" threshold: 7 tiny mip-chain tasks (64 KiB out) measured
  0.93x (i.e. no regression) through the queue. Per-submit the pool costs one `util_queue_finish`
  (a barrier, `num_threads` fences), not a thread wake.
- `src_y` for a compressed source counts **block** rows, not texel rows, because `src_row` is one
  block row. Getting this wrong produces band-to-band mismatches that look like a driver race but
  are a harness bug; the fix is `y0 / util_format_get_blockheight(src_format)`.
- The decoder queue must be file-scope (not on the stack): `util_queue_init` registers an atexit
  handler. A stack queue segfaults after `main` returns, which is a harness artifact, not a driver
  bug.
- `util_queue_is_initialized` is false when `util_queue_init` fails, so the code falls back to
  serial decode on the submit thread. The same fallback covers a failed task-list `realloc`; the
  split loop must not drop jobs when it bails out.

## Check the BC band split (`tools/venus/bc-band-test/run.sh`)

`tools/venus/bc-band-test/run.sh <mesa-src> <build-dir>` extracts the BC decode functions from
`vn_droiddeck_bc.c` **verbatim** (via `genbcband.py`) and checks the row-band split end to end:
it replicates `bc_decode_cmd`'s split, runs each task through the real `bc_decode_task`, and
compares the assembled image byte-for-byte with a whole-image decode, across BC1/2/3/4/5, band
counts from 1 to 64, and array/cubemap slices (75 cases). It needs a build dir from
`check-guest.sh` (it uses the build's own flags and its `libmesa_util.a` for the reference
decode) and exits non-zero on any mismatch. CI runs it in the guest job, after the ICD build.

- This exists because testing the decoders and the split **separately** is not enough. The
  decoders were correct without bands, the split was correct with the generic decoder, and the
  fast paths still ignored the band offset, so every band after the first overwrote the top rows
  and the rest of the texture stayed staging garbage. On the phone that is "the map is cut in
  half". Both earlier harnesses passed while the phone was broken.
- Extract the code, never copy it. A hand-copied "verbatim copy" had the correct BC5 source
  stride (`s += block`) while the driver shipped `s += 8`, so the copy passed and the phone did
  not. `genbcband.py` reads the function bodies out of the driver, so a copy cannot drift.
- The check is what to reach for when a texture looks partially decoded, half-black, or
  "cut in half": if it passes, the bug is not in this decode path, and if it fails the mismatch
  line names the first bad byte and the row it is in.

## BC decode: block-at-a-time fast path (`bc_decode_s3tc`, `bc_decode_rgtc`)
Mesa's generic path (`util_format_translate` -> `u_format_s3tc.c`) decodes one texel at a time
and rebuilds the block's palette for each of the 16 texels. Mesa's own comment says it: *"to be
efficient, it would be necessary to decode 16 pixels at once"*. DroidDeck now does, for the
formats DXVK actually ships.

- Measured on the 4-core sandbox, versus the generic path, byte-for-byte exact:
  BC1/BC2/BC3 **3.3-4.5x**, BC4/BC5 **6.1-6.7x** (4096x4096 BC5 139 ms -> 23 ms). Combined with
  the thread pool that is roughly **9x** for S3TC and **15x** for BC4/5 versus the original code.
- Only the **linear** source formats need a case. `util_format_linear(vk_format_to_pipe_format(orig))`
  maps every sRGB BC format to its UNORM twin (BC1_RGB_SRGB -> DXT1_RGB, BC5_SNORM -> RGTC2_UNORM,
  ...), so sRGB needs no special handling: the substitute image is sRGB, so the decoded bytes must
  be the raw sRGB bytes. Verified with a format probe, not assumed.
- Two easy-to-get-wrong details, both caught by testing against the generic path: BC2/BC3 always
  use 4-color interpolation (`dxt_type > 1`), and BC1_RGBA in 3-color mode is transparent while
  BC1_RGB is opaque. Decoding BC3 with BC1's `color0 > color1` test mismatches every block.
- BC4/BC5 SNORM is fine on this path: `util_format_rgtc*_snorm_unpack_rgba_8unorm` in
  `u_format_rgtc.c` is a stub that only prints, but the driver never calls it. BC4_SNORM substitutes
  to R8_SNORM, so `translate` picks `util_format_rgtc1_snorm_unpack_r_8snorm`, which works.
- Unhandled formats (BC6H/BC7, and anything the generic path would scale) fall through to
  `util_format_translate`, so the fast path cannot silently corrupt them.
- These functions start at block `(0,0)` of whatever pointer they are given, and a task's `src` is
  the **slice** start, not the band start. So the caller (`bc_decode_task`) must add the band
  offset before calling them: `src + (y0/4) * src_row` and `dst + y0 * dst_row`. Getting this
  wrong is the "texture is cut in half" bug: band 0 decodes correctly and every later band
  overwrites the top rows, leaving the rest of the image as staging garbage. The generic path
  needs no such fixup because it is handed `dst_y`/`src_y` directly. **A previous version of this
  note claimed the opposite** ("the band's `src` is already offset ... do not add a `y0` term"),
  which is what shipped the bug.
- These functions must take their per-block source stride from `util_format_get_blocksize(fmt)`,
  never a literal. A BC5 block is 16 bytes (two 8-byte channels), so the `s += 8` that is right for
  BC4 walks BC5 at half speed and decodes half the blocks. This one is invisible to a test that
  only checks BC4.
- They also assume the job's `width`/`height` are multiples of 4, which the submit path guarantees:
  it rounds up with `w4 = align(imageExtent.width, 4)` and sizes the destination as `w4 * h4`, so
  a whole-block write is always in range. That is why a 6x6 BC texture works (it decodes an 8x8
  region). Do not call these with unaligned dimensions; fall through to the generic path instead.

## The BC decode cost is the memory it writes into, not the decode (`guest-patches/0022`)

The decode is ~7-10x slower on the phone than the same code is on a desktop, and the reason is
the destination memory, not the decoder. Measured, not inferred:

- The decoder itself is already at the limit. `genbcband.py` extracts the driver's real
  `bc_decode_s3tc`; compiled `-O2` on the sandbox it runs BC1 at **0.43 ms/MiB** and BC3 at
  **0.60 ms/MiB** of output. A rewrite that stores one 32-bit word per texel instead of four bytes
  is **not** faster (0.94-0.99x) and byte-identical, so there is no CPU win left in this loop.
  Do not "optimize" it again without re-measuring: the byte-store loop is already the fast one.
- On the phone the same decode reports **2.1 ms for a 0.49 MiB upload (~4.3 ms/MiB)**, i.e. the
  write side costs several times the whole decode. Every host-visible allocation is backed by
  `/dev/dma_heap/system-uncached` (`VenusBridge.kt`'s `VKR_DMA_HEAP` default, patch 0001), so the
  guest CPU writes decoded texels straight into uncached pages. `bc_stats`'s `decode_ns` is that
  wall time.
- **`decode_ns` is submit-thread wall time, and that is why it looks fatal.** `bc_decode_cmd`
  times the whole decode *including* `util_queue_finish` (a barrier), and it runs from
  `vn_QueueSubmit`. So "4331-6888 ms decoding" in a 10 s window means 43-69% of the submit
  thread's time, which is exactly the observed collapse to 3-6 fps. It is a per-submit stall, not
  a throughput number.

Why the uncached heap cannot simply be flipped back to `system` (or `system-heap`): the render
server does **no** dma-buf cache maintenance. `vtest_bo_invalidate`/`vtest_bo_flush`
(`vn_renderer_vtest.c`) are `nop`, and venus reads host-written memory with no invalidate at all
(the ring, `vn_ring_get_seqno_status`). That is *why* the uncached heap was chosen: with a cached
heap the GPU-written semaphore feedback went stale and gamescope waited forever. So a cached
staging buffer needs `DMA_BUF_IOCTL_SYNC` around the CPU write, and `vn_dd_bc_submit` has no fd
for the chunk (the bo's `res_fd` is not exposed to the driver) -- it is a host-side change, not a
one-line guest edit. Recorded here so the next attempt does not just flip the default and break
semaphores again.

What `0022` does instead is remove the part that is safely removable guest-side: the 8 MiB
dma-heap staging chunk was created and destroyed on **every command-buffer reset**, so a title
uploading BC textures each frame paid a dma-heap alloc/free per frame. The server's accounting
shows that churn directly -- `vkAllocateMemory` max **80.3 ms** per call, 227 calls and >1 s in
one 10 s window. `0022` keeps up to four chunks per device in a pool, hands one back from
`bc_chunk_new`, returns it in `vn_dd_bc_cmd_reset`, and frees the pool in `vn_dd_bc_device_fini`
(called from `vn_DestroyDevice`). A chunk is only pooled after its command buffer is reset, at
which point the copy that read it has completed, so a pooled chunk is never reused under a live
copy. The pool is keyed by device because a `VkBuffer` is not valid on another device.

- This is a real but partial fix. It removes the allocation churn (and the `vkAllocateMemory`
  spikes); it does **not** remove the uncached-write cost, which is the larger of the two. Expect
  the `frames:` line to move some, not to reach 60.
- The remaining order of work, if the uncached cost has to go: cache maintenance over the vtest
  socket (host + protocol change), then flip `VKR_DMA_HEAP` to a cached heap. Anything that
  touches only the guest will not move it.
- Secondary, still open, and unrelated to the above: **~1409 `VK_ERROR_OUT_OF_POOL_MEMORY`** from
  DXVK's descriptor pools, and the compositor's 100 ms vsync guard + fallback timer in
  `DroidDeckWayland`'s `schedule_render()`. Neither is the BC path.

## The descriptor-pool errors are the guest's own accounting, and are a no-rebuild knob

`VK_ERROR_OUT_OF_POOL_MEMORY` is not the real driver refusing: it is **Mesa's guest-side shadow
accounting** refusing. `vn_CreateDescriptorPool` (`vn_descriptor_set.c`) picks

```c
pool->initial_state = pool->current_state =
   VN_PERF(NO_ASYNC_SET_ALLOC) ? VN_ASYNC_SET_ALLOC_NONE
   : pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
      ? VN_ASYNC_SET_ALLOC_SAME_ALLOC : VN_ASYNC_SET_ALLOC_ALWAYS;
```

and `vn_AllocateDescriptorSets` then returns `VK_ERROR_OUT_OF_POOL_MEMORY` from
`vn_descriptor_pool_alloc_descriptors` (the guest's `pool->used`/`pool->max` counters) **before
the real `vkAllocateDescriptorSets` is ever sent** — that path is async
(`vn_async_vkAllocateDescriptorSets`), so the real pool may well have room. DXVK reads the failure
as "this pool is full", resets or recreates it, and retries, which is why the same window shows
`vkAllocateDescriptorSets 66447x 186.5 ms` alongside the failures: a failure/retry storm.

- The rate is not new and not caused by 0019/0021/0022. The per-launch counts in one bundle span
  **12 to 23,715** (`steam-1118240.log`), and the `~1409` in this note was one window of a
  different title. Treat it as a long-standing guest-side policy, not a regression.
- **It is switchable without a rebuild.** `VN_PERF=no_async_set_alloc` (`vn_perf_options` in
  `vn_common.c`, read with `os_get_option` i.e. `getenv`) sets `VN_ASYNC_SET_ALLOC_NONE`, which
  skips `vn_descriptor_pool_alloc_descriptors` entirely and sends the real
  `vkAllocateDescriptorSets` synchronously. The 112k failures go away; the cost is a round trip
  per allocation, so A/B it — it may win on correctness and lose on frame time, or the reverse.
- A `VK_ERROR_OUT_OF_POOL_MEMORY` that DXVK does not retry leaves a descriptor set unbound, which
  reads to the player as one object drawing white/black/garbage. That is the leading candidate for
  "a white wagon" (Lake's van), ahead of the BC path: the BC decode is byte-exact (below).

## What was ruled out for the "upside down" glitch

- **The BC decode is exact.** `tools/venus/bc-band-test/run.sh` (build the archives by hand if
  `ninja` is missing; `libmesa_util*.a` are already in `build-check`) reports
  `PASS: 60/60 cases exact (fast path + band split, real driver code)`. No row/column flip exists
  anywhere in `vn_droiddeck_bc.c` — the decoder only ever writes `by*4` rows down from the band's
  `y0`, so it cannot invert an image.
- **No BC uploads are dropped.** The stats line reports `0 copies left out` and `0 uploads from
  memory the driver cannot read` for all 33 windows, so no texture is left empty by the BC path.
- **`VK_ERROR_FORMAT_NOT_SUPPORTED` is a format-query probe**, not a draw failure: every
  occurrence is `vn_get_image_format_properties2_impl` / `vn_GetPhysicalDeviceImageFormatProperties2`,
  i.e. DXVK asking "can I make this image?" and being told no. It is the BC substitute's own
  restriction (`vn_dd_bc_format_info_begin` + the `bc_usage` gate in
  `vn_GetPhysicalDeviceImageFormatProperties2`: BC is offered for
  SAMPLED|TRANSFER_SRC|TRANSFER_DST + OPTIMAL only). The steady ~498-510 per launch is DXVK's
  capability probe, not a per-frame error.
- **`VN_DD_EMU_MULTI_VIEWPORT` is a real correctness risk, and is worth an A/B.** The Mali has
  `multiViewport=no` (`android-vulkan.txt`), so `vn_CmdSetViewport`/`vn_CmdSetViewportWithCount`
  clamp to viewport 0 and `vn_CmdSetScissor*` likewise. That is harmless while DXVK keeps viewport
  0 at the full target, but a draw that legitimately uses viewport 1 gets viewport 0's rectangle.
  `DROIDDECK_VN_EMULATE=0` turns every emulation off and is the control.

## Uncap the nested refresh without a rebuild

`droiddeck-session` already passes `-r "$BL_MALI_REFRESH"` (Mali branch), so the 60 Hz cap is
**one environment variable**, no script edit and no APK:

```
BL_MALI_REFRESH=120
```

Put it in the app's env-switch file — `envSwitchFile()` is
`/storage/emulated/0/Android/data/com.droiddeck.launcher/files/droiddeck-env` (one `NAME=value`
per line, `#` comments). `extraEnv()` appends it **after** the driver's GL environment and both go
through `/usr/bin/env -i`, so it wins. `Download/droiddeck-env` is the dead legacy path and is
ignored with a warning. `BL_FPS` must stay `0`, because it is tested *before* the Mali branch
(`if [ "${BL_FPS:-0}" -gt 0 ]`), so a non-zero session FPS limit silently overrides this.
The documented caveat stands: at 120 Hz the software X connection for Steam's UI can overrun
("Fatal IO error 11"), which is why the cap exists — expect UI stalls, and treat `BL_MALI_REFRESH=0`
(panel rate) and `=120` as the same experiment.

## The descriptor-pool errors are fixed at the source now (`guest-patches/0023`)

The section above describes `VN_PERF(no_async_set_alloc)` as the knob, and it is still the way to
turn the shadow accounting off for an A/B. `guest-patches/0023` makes the accounting non-fatal
instead, which is the fix and not a knob: when
`vn_descriptor_pool_alloc_descriptors()` says a pool is full, the guest no longer answers
`VK_ERROR_OUT_OF_POOL_MEMORY` on its own authority. It drops the pool to `VN_ASYNC_SET_ALLOC_NONE`
and lets the next `vn_call_vkAllocateDescriptorSets` ask the real driver, which is the only thing
that knows the pool's actual room. The state transition is the same one the pool already makes when
a set is freed, and `vn_descriptor_pool_free_descriptors` is guarded by the same
`!= VN_ASYNC_SET_ALLOC_NONE` test everywhere, so no accounting is added or skipped.

Why the shadow can be wrong: it is per-pool guest-side bookkeeping that does not see a host-side
reset and does not see another ring allocating from the same pool. DXVK retries a real
`VK_ERROR_OUT_OF_POOL_MEMORY`, so a false one is a reset/recreate storm, which is what the ~112k
errors were; a real one now comes back from the driver and is returned unchanged. The first
fallback writes one line to `droiddeck_vulkan.txt`:

```
descriptor pools: the guest's accounting said a pool was full; deferring to the real pool
```

If that line appears, the accounting was wrong and the fix is doing its job.

## The frame rate falls off a cliff in the first minute, and BC decode is why

In the 2026-10-27-05 Lake run the frame rate was 58 fps in the first window and 3-7 fps forty
seconds later, and the fall tracks the BC decode's CPU time, not the refresh cap:

| window | fps after | uploads decoded | decode ms | share of the window |
|---|---|---|---|---|
| 07:55:44 | 58.0 | 0 | 0.0 | 0% |
| 07:56:12 | 16.1 | 55 | 1062 | 10% |
| 07:56:24 | 3.2 | 915 | 4429 | 43% |
| 07:56:34 | 6.8 | 1454 | 6444 | 64% |
| 07:56:55 | 5.2 | 1651 | 3953 | 40% |

So the level loads its textures over the first ~30 s, each decode competes with the game's own
threads for the same CPU, and the steady state is the decode cost, not the driver's draw path. This
is the number to move for 24->60, ahead of the present-sync and refresh work: the refresh cap
cannot be the ceiling while the frame time is 200-300 ms. `DROIDDECK_VN_BC_THREADS` sets the decode
thread count (1 keeps the old single-threaded path, the default is one per CPU, clamped to 16), and
the Pixel 9 Pro XL has 8 cores with the game free to use all of them, so a lower count is worth an
A/B: the decode and the game are not the only two things on the CPU.

## The APK is no longer built for driver-only changes

`.github/workflows/build.yml` gained a `changes` job that runs before `build`. It diffs the pull
request and, if every changed path is driver-side (`tools/venus/`, `tools/panvk/`, `tools/comet/`,
`tools/ea/`, `mali-gl/patches/`, `docs/`, `.github/`, any `*.md`), sets `apk=false`; `build` then skips, and so
do `sign`, `announce` and `catalog`. The gate is an allowlist of paths the APK does *not* read, so
a new or unknown directory builds the APK rather than silently skipping it.

`paths-ignore` could not do this. On a `pull_request` event it evaluates the whole PR diff, so a PR
that has ever touched `app/` rebuilds the APK on every later push no matter how small. The `changes`
job diffs the push (`github.event.before..HEAD`, falling back to the target's tip when a force-push
makes that unusable), which is the granularity the request was about. Pushes to `main` and manual
runs are untouched and always build, so the signed release path is unchanged. `changes` passes when
it skips, so a required "Build APK" check still goes green, and its step summary lists the files it
saw and says which way it decided.

## The frame collapse is the BC decode writing uncached, and the drain is not it (2026-10-27 logs)

The `2026-10-27-09-steam` bundle settles what costs the frames, and it is neither the present drain
nor the descriptor pool. It is patch 0019's BC decode writing into `/dev/dma_heap/system-uncached`.

`bc_stats`'s `decode_ns` is **submit-thread wall time** (`bc_decode_cmd` times the whole decode
including the `util_queue_finish` barrier, and it runs from `vn_QueueSubmit`). As a fraction of the
submit thread it tracks the frame rate exactly, on all eight Lake runs in the bundle:

    BC decode share   0.1%  ->  19%  ->  37%  ->  64%  ->  66%
    frames:          52 fps -> 18 fps -> 5 fps -> 2.6 fps -> 5 fps

The peak window is `6550 ms of 10 s` = **65% of the submit thread**, and the frame at that point is
213 ms. Two thirds of it is the guest CPU writing decoded texels, one uncached byte at a time.

Across all 63 (decode-share, frame-rate) pairs in the bundle the relationship is monotonic, which is
what makes this the answer rather than a coincidence:

    decode share   0-10%   10-20%  20-30%  30-40%  40-50%  50-60%  60-70%
    mean fps        28.2     11.4     6.3      4.4      3.8      4.9      5.1
    n                22        6       2       13        5        3       12

`share <20% -> 24.6 fps (n=28)` against `share >=50% -> 5.1 fps (n=15)`. Nothing else in the bundle
splits the runs this cleanly, and the two earlier suspects do not split them at all.

- The rate is the problem, not the algorithm. The window decodes **1452 MiB of output in 10 s**, and
  the write side costs **4.5 ms/MiB** on the phone (measured, not assumed). At 60 fps a frame is
  16.7 ms; a 5% budget for BC is 0.83 ms, which is **2.4 MiB decoded per frame**. That is **10.9 ms
  of uncached writes at today's cost** -- so the writes have to get about **13x cheaper** before the
  BC path can sit inside a 60 fps frame. No guest-only tweak closes 13x: `0019`'s decoder is already
  byte-exact and already parallel, and `0022` already removed the alloc/free churn. The remaining
  cost is the memory the writes land in.
- **It is steady state, and that is the point.** It does not recover because it is not a pool filling
  up or a cache warming: once the world is loaded, every frame uploads BC textures and every one of
  those uploads writes uncached. This is why the earlier "it decays" reading was wrong.
- The host server agrees from the other side: `vkAllocateMemory` peaks at **95.7 ms a call** and the
  ring shows `vkCmdCopyBufferToImage` / `vkAllocateMemory` as the top entries in the same windows.
  The 6.4 s of "decoding" is guest CPU, so the host sees it as the guest being slow to submit.

The fix is the one AGENTS.md already named and then deferred: **give the staging memory a cache
maintenance path**. The plumbing is closer than the earlier note claimed -- it is not missing:

  * `vn_renderer_bo_ops.flush` / `.invalidate` already exist, and `vn_FlushMappedMemoryRanges` /
    `vn_InvalidateMappedMemoryRanges` (`vn_device_memory.c`) already call them on the mapped range.
  * `vtest_bo_flush` / `vtest_bo_invalidate` are the literal `/* nop */` bodies, so a cached heap
    would go stale -- which is why `system-uncached` was chosen. That reasoning is sound.
  * But `struct vtest_bo` already carries `res_fd` (the dma-buf fd), and `vn_renderer_bo_map` already
    mmaps it. So the fd needed for `DMA_BUF_IOCTL_SYNC` is reachable at the point of the nop; the
    host side is a real but bounded change, not a new protocol.

Order of work, cheapest first:
  1. **`DROIDDECK_VENUS_HEAP` is NOT a user-reachable A/B, despite the name.** `VenusBridge.kt`
     reads it with `System.getenv`, i.e. from the *app* process's environment, and the server is
     started with a fixed `env` array of its own. A line in `droiddeck-env` or the per-game
     Environment card reaches the *guest*, not the app, so it cannot set this. Testing a cached
     heap needs either `adb shell setprop`/`am start` with the var (no adb here) or a one-line
     change to the default in `VenusBridge.kt` plus an APK. Do not write it up as a no-rebuild
     knob again.
  2. **`DMA_BUF_IOCTL_SYNC` in `vtest_bo_flush`/`vtest_bo_invalidate`**, then default the heap to a
     cached one. This is the actual fix: it keeps the semaphores correct (the sync makes the cache
     coherent at the points the guest already calls) and makes the texel writes ~cache speed.
  3. **Only then** revisit the present path. It is not what costs the frames: the three runs with the
     drain ON and the five with it off have the same 52->3 fps curve.

### The no-rebuild test that confirms it, and what 60 fps would take

`DROIDDECK_VN_EMULATE_BC=0` (patch 0019, read in the guest) turns BC emulation off. The textures
that needed decoding then stay empty, so the picture is wrong on purpose -- but the `frames:` line
is the measurement: if it jumps back to ~50 fps, the BC path is the whole collapse, and the run at
23:42 already shows the shape of that (BC share ~1.5% over a 62 s window, ten consecutive samples
at ~50 fps, then 63% -> 3 fps). Set it in the per-game Environment card or `droiddeck-env`; no APK.

The arithmetic, so the target is not mistaken again. At 60 fps a frame is 16.7 ms. The bundle's peak
window decodes **1452 MiB in 10 s**, which is **2.4 MiB per frame at 60 fps**, and the write side
alone costs **4.5 ms/MiB** -- **10.9 ms**, two thirds of the frame, before the decode reads a byte.
Making the writes cache-speed removes that 10.9 ms, which is the difference between 4.7 fps and
roughly the high 20s; it does not by itself make 16.7 ms. The remaining decode+read work still has
to fit, so treat "cached heap" as the step that makes the frame rate usable, not as the step that
reaches 60. Reaching 60 on this title means not decoding on the CPU at all (a compute-shader decode,
or not re-uploading the textures), which is a much larger change than the heap.

## The descriptor pool is NOT what collapses the frame rate (the 23:42 run proves it)

The run that starts at 23:42 is a natural experiment, and it kills the descriptor-pool theory --
including the version of it written in this file. In minute 23:43 that run accumulated **116
descriptor-pool failures** and held **49-51 fps for six consecutive samples**:

    23:43:14  51.0 fps      (23:43 minute total: 116 pool failures)
    23:43:24  50.6 fps
    23:43:34  49.3 fps
    23:43:44  49.4 fps
    23:43:54  50.4 fps

Then minute 23:44 arrives with 307 failures and 3-6 fps. So the failures are present *during* the
50 fps minute, in the same minute, at 116 of them. They are therefore neither necessary nor
sufficient for the collapse, and the earlier "the host refuses too, so the pool is the frame cost"
conclusion in this file was wrong: it correlated two things that rise together (uploads and pool
failures) and read the wrong one as the cause. The pool still needs a fix -- 4878 real refusals is a
bug -- but it is not the frame rate.

What the 23:42 run *does* separate cleanly is decode share. Minute 23:43 is 0.2% decode share at
~50 fps; minute 23:44 is 33-63% at 3-6 fps. Same process, same pool behaviour, same present path.

## The CPU decode: write-bound or compute-bound is NOT settled by these logs

Regressing `decode_ns` on the two byte counts over the 57 windows with real work:

    decode_ms = 4.28 ms/MiB * output_MiB   R2 = 0.79
    decode_ms = 26.65 ms/MiB * input_MiB   R2 = 0.82

Both fits look equally good *only because input and output are collinear* here (the expansion ratio
is a steady ~6.2x once the world is loaded, so the two regressors are near-duplicates and the R2 gap
is not evidence). The coefficient does **not** settle it either, and an earlier version of this
section claimed it did: `4.28 ms/MiB` was called "the measured uncached-write cost", but the
4.3-4.5 ms/MiB it was compared against came from the same `decode_ns` data -- the same measurement
read twice, not a second opinion. The test that *would* discriminate is the expansion ratio, which
is not quite constant (3.95 in the first window, ~4.8-5.1 mid, ~6.2-6.6 once steady):

    write-bound    predicts corr(ratio, ms/MiB-in ) = +1   observed +0.20
    compute-bound  predicts corr(ratio, ms/MiB-out) = -1   observed -0.32

Both are weak and the ratio is near-constant in most windows, so **the logs do not settle write-bound
vs compute-bound.** If anything the compute-bound signal is the stronger of the two. What does argue
for the write side needs no timings: the inner loop stores each pixel as four separate byte writes
(`row[i*4+0..3]`) into host-visible memory, i.e. the uncached heap, so a 4x4 block is up to 64 byte
stores against 8-16 bytes read.

This matters for the plan, because it means a compute-shader decode and a cached heap are not two
different amounts of the same win:

- **Cached heap (option 1)** removes the dominant term: 4.28 ms/MiB -> roughly cache speed, i.e.
  about 10x on the write side. Sustaining the worst observed 178 MiB/s would then cost ~9% of a core
  instead of ~76%. Small, bounded change, already-plumbed hooks.
- **Compute decode (option 3)** removes both terms at once, and is the only option that does: the
  guest CPU stops writing texels at all, and the GPU writes them into device-local memory where the
  uncached penalty does not exist. That robustness to the ambiguity above is its main argument. It is
  a much larger change. One thing it does *not* need is the descriptor fix: `VK_ERROR_OUT_OF_POOL_MEMORY`
  is per-pool, and those 4878 refusals are the application's own pools (DXVK's), so a decode pipeline
  with its own pool is unaffected. An earlier version of this section claimed option 3 was blocked on
  the pool bug; that was wrong.

So option 3 is the right *end state* if the decode must leave the CPU entirely, and it is the
option to pick when the write/compute question above matters more than the size of the change.

## The descriptor pool: the host refuses too, so `0023` is not the fix

The `2026-10-27-09-steam` host log settles the open question in the section above. The server logs
`vkr: Warning: vkAllocateDescriptorSets failed(3294898296)` **4878 times**, and 3294898296 is
`-1000069000` as a signed int32, i.e. `VK_ERROR_OUT_OF_POOL_MEMORY` exactly. So the failure is **not
only** the guest's shadow accounting: the real pool refuses as well, which means `guest-patches/0023`
(hand the decision to the real driver) cannot be the fix on its own -- it is taken once per run and
the allocations still fail afterwards. The pool is genuinely out.

What that costs is real but secondary: the per-10s host counts go `vkAllocateDescriptorSets` 4302 ->
98765, `vkUpdateDescriptorSets` 4062 -> 75430, `vkResetDescriptorPool` 106 -> 350, and the ring goes
from 115 ms busy to ~2000 ms busy. That is DXVK retrying, and it is worth its own fix -- but it is
**not** the 52->3 fps curve, because the curve is identical in runs where the decode share is low and
the descriptor failures are equally present.

`VN_PERF=no_async_set_alloc` remains the no-rebuild A/B for the guest half. It will not remove the
host-side refusals, because those come from the real pool.

## The "white van upside down" is one object, not the frame

"The white van is still upside down" after the drain went back on is the useful detail: a single
object drawn wrong, with the rest of the frame correct, is not a whole-frame present-ordering fault
(that is white/garbled *frames*). It points at one draw reading a wrong descriptor or a wrong
viewport. Two candidates, both already switchable without a rebuild:

- **A descriptor set left unbound** by an allocation that failed and was not retried. This is the
  stronger candidate, because **one unbound set explains both symptoms at once**: a constant buffer
  the vertex shader reads for its transform is garbage, so the object lands in the wrong orientation
  ("upside down"), and the albedo it reads is garbage, so it draws white. One mechanism, not two.
  The 4878 host-side refusals above are what make it plausible, and it is why the van is worth
  re-checking *after* the descriptor-pool fix rather than before it.
- **`VN_DD_EMU_MULTI_VIEWPORT`** (patch 0013) is the alternative for the *orientation* half alone:
  the Mali has `multiViewport=no`, so `vn_CmdSetViewport`/`vn_CmdSetScissor*` clamp to viewport 0 and
  a draw that legitimately uses viewport 1 gets viewport 0's rectangle. It does not explain the
  white, so it is the weaker of the two -- but `DROIDDECK_VN_EMULATE=0` is the one-flag control for
  it, and it is free to try.

Set either in the per-game Environment card (FrontEndSetup.kt, `game_env_*`; values are plain
`NAME=value`) or in the session-wide `droiddeck-env` file, then relaunch. Neither needs an APK.

## The white/garbled frames are the present drain, and it is off by default (`DROIDDECK_VN_SYNC_PRESENT`)

If a title shows white or garbled frames on the `venus-bridge` build, set this first:

```
DROIDDECK_VN_SYNC_PRESENT=1
```

in `/storage/emulated/0/Android/data/com.droiddeck.launcher/files/droiddeck-env`, then relaunch.
It needs no rebuild, no APK and no driver change: the file is read by `extraEnv()` in
`SessionService.kt` and passed through `/usr/bin/env -i`.

Why this is the first thing to try. Guest patch 0021 deliberately removed the queue drain that used
to run before every present. Upstream `vn_queue_wsi_present` calls `vn_QueueWaitIdle` when the
renderer reports no implicit fencing, which is this guest. That drain was 89% of the frame on Lake
(17.6 ms of 19.8 ms), so 0021 skips it by default and the guest no longer waits for the frame's GPU
work to finish before handing the buffer to the server. When that ordering is wrong for a title,
the server can read the swapchain image while the GPU is still writing it, and the player sees a
white or garbled frame.

What `=1` does now is not the drain. `a0deac6` made it wait on the present's own ring seqno (and
fall back to `vn_QueueWaitIdle` only when the submit has no valid seqno), which orders the present
after its submit without parking the CPU until the GPU idles. The ordering is the same guarantee
the drain gave; the cost is not. So `=1` is the cheap ordering and the *unset* default is "no
ordering at all" -- the fast path. That also means this is not a drain/no-drain A/B any more: to
put the old full drain back you need a build predating `a0deac6`.

This is the honest reading of the evidence: the drain's removal was a deliberate speed choice whose
comment says the ordering it relies on "is the design intent, not something this change proves",
and its own escape hatch is for "a title that tears or flickers". White/garbled frames are that
symptom. The two are worth A/B-ing against each other rather than assuming the drain is innocent.

What this means for the other candidates. The descriptor-pool fix (`guest-patches/0023`) is still
correct and still worth having, but it cannot be what produces a white frame: the pristine
`vn_AllocateDescriptorSets` returned `VK_ERROR_OUT_OF_POOL_MEMORY` only when the guest's shadow
accounting refused, and that refusal aborted the whole call with `pDescriptorSets` zeroed, so DXVK
saw a failed allocation and retried -- it never bound a half-written set. A garbled frame is more
plausibly a read of an image the GPU has not finished writing, which is the drain.

## The driver's knobs are set by the driver, not by the APK

`SessionService.addClientEnvironment` no longer states any knob the driver owns. It used to add
`DROIDDECK_VN_SYNC_PRESENT=1` itself; that line is gone. The only place a driver knob is stated is
the driver's own `env` file in its zip (`tools/venus/guest-env`), so changing one means shipping a
driver and never an APK build. A knob the driver's `env` leaves commented is not in the environment
the driver owns, so the user's `droiddeck-env` is still where that one is set.

The environment the app *does* build is the session plumbing the guest cannot get from a static
artifact -- `DROIDDECK_BWRAP_SOCKET`, `PULSE_SERVER`, `VK_ICD_FILENAMES`, `XDG_RUNTIME_DIR`,
`BL_ANDROID_CLIPBOARD`, `FAKE_EVDEV_*`, `BL_WIDTH`/`BL_HEIGHT`, `BL_VENUS`, the CPU pins -- plus
the non-driver Mesa/Steam knobs. Those name paths and values that exist only at run time on this
device, so they cannot live in a zip downloaded from CI.

Note what `=1` means changed underneath this setting. When the session started forcing it, `=1` was
the full `vn_QueueWaitIdle` drain, so the build the user was testing had the drain on and the
`=0`/unset control was the fast path. `a0deac6` then redefined `=1` as a wait on the present's own
ring seqno with the drain only as a no-seqno fallback. The driver's `env` now states `=1`, so the
default is still "on", but "on" is the cheap ordering and the control `=0` is "no ordering". The
env var is still the only thing that decides it, so the A/B is unchanged -- only what the two sides
mean moved, and now the line that sets it ships with the driver.

This flips the default that guest patch 0021 chose. 0021 turned the drain off to win the frame time
and left `DROIDDECK_VN_SYNC_PRESENT` as the way back; the white/garbled frames are the symptom that
switch exists for, so the driver's `env` states it on.

The APK is built by `workflow_dispatch` on the branch (`gh workflow run build.yml --ref
venus-bridge`), which the `changes` gate lets through unconditionally and which skips the
PR-only merge step. Forks get the testkey-signed APK in the `droiddeck-apk` artifact; the `sign`
job needs upstream's private key and is skipped, so a testkey build replaces a release-signed
install only after an uninstall.

## Reading `droiddeck_vulkan.txt`: where it is, and the 600-line cap

The guest's own log is written by patch 0011's `vn_droiddeck_log`, to
`<Steam root>/logs/droiddeck_vulkan.txt` inside the guest. Patch 0011 picks that directory as
`$STEAM_COMPAT_CLIENT_INSTALL_PATH/logs`, else `$HOME/.local/share/Steam/logs`, else the literal
`/root/.local/share/Steam/logs`, and gives up (no file at all) unless the directory already exists.
On the Android side that is
`/data/data/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/logs/droiddeck_vulkan.txt`,
or `root/.droiddeck-x64/.local/share/Steam/logs/` when the Steam client is x86_64.

Getting it needs neither root nor adb, and no file on the device. The session drawer's
**Support > Session logs > Share logs** button zips the live Steam log directory into the shared
zip as `<session>/steam/droiddeck_vulkan.txt` (`SessionLogShare.liveSteamLogs`, which copies the
live directory only for the current session and only while the session has no `steam/` folder of
its own). So: share logs while the session is still running, and the file is in the zip. A file
manager cannot reach the same file by hand -- it lives under `/data/data/`, and on Android 11+ the
`Android/data/` path is closed to other apps, so the drawer is the way in.

Setting a variable for one game also needs no file: the game's card has an **Environment > Edit
variables** field (NAME=value, space separated, `GameLaunchEnv`). Per-game values are applied by
`droiddeck-game-env`'s `apply_config`, which assigns over the inherited environment, so a game
card's `DROIDDECK_VN_SYNC_PRESENT=0` beats the session's `=1`. That is the A/B, in the app, with
no root, no adb and no rebuild.

**The trap: the log stops after 600 lines per process.** `vn_droiddeck_log` keeps a `static unsigned
lines` and only writes while `++lines <= 600`, and the file is opened once per process, so a session
gets its first 600 log lines and nothing after. A diagnostic line that fires late in a long session
never appears, and "the line is not in the file" is not evidence that the code did not run. Reproduce
within the first 600 lines of a fresh launch, or raise the cap, before reading anything into absence.
The frame-rate line (0021) and the pool-fallback line (0023) are both subject to this.

## The Lake logs (2026-10-27) say the frame rate is not the present drain

The `2026-10-27-09-steam` share (fex.net/uk/s/atrykfb) is the first evidence with the guest's own
numbers in it. It overturns the earlier read that the 24->60 fps work was about the present path.

Every Lake.exe run in that file looks the same, on all three builds in it:

    frames: 52.8 fps  ->  18.7 fps  ->  5.1 fps  ->  2.6 fps   (over ~40 s)

and the `present GPU wait` stays small (4.04 -> 4.07 -> 13.33 -> 37.85 ms). So the drain is **not**
what costs the frames: even with the drain skipped the game collapses from 52 fps to 3 fps. The old
"the drain is 89% of the frame, skip it" note in patch 0021 was measured on a build whose log we no
longer have; this log does not support it as the explanation for the 24 fps target.

What does track the collapse, in the same window:

  * `vkAllocateDescriptorSets: VK_ERROR_OUT_OF_POOL_MEMORY` in the guest, 494 times in a 90 s run,
    and the host server logs the same failure 4878 times. The guest's `descriptor pools: ... deferring
    to the real pool` line fires once per run, so patch 0023's fallback *is* being taken -- and the
    allocations still fail afterwards. The fallback is therefore not the fix; the pool really is out.
  * The host ring goes from ~115 ms busy per 10 s to ~2000 ms, and the per-call counts explode:
    `vkAllocateDescriptorSets` 4302 -> 98765, `vkUpdateDescriptorSets` 4062 -> 75430,
    `vkResetDescriptorPool` 106 -> 350 per 10 s. DXVK is retrying the failed allocation in a loop,
    which is what eats the frame.
  * `vkCreateComputePipelines 114x 9969 ms of 10 s` in one window -- a compute-pipeline rebuild
    storm, and the only place the host is ~100% busy.

So the descriptor pool is the thing to fix, not the present path. The three runs before this log
already had `DROIDDECK_VN_SYNC_PRESENT` handling in the guest and behave identically to the newest
one, so sync-present neither caused nor cures this. Read the `frames:` and `present GPU wait` lines
together before believing any theory about the present path again.

## The GPU BC decode check (`tools/venus/bc-gpu-test/run.sh`)

The compute shader that decodes BC1/BC2/BC3 on the GPU and `bc_decode_s3tc()` in
`vn_droiddeck_bc.c` are two implementations of one contract: a block of compressed bytes must
produce the same RGBA8 texels either way. Reading the shader is not enough to know that, because
the failure mode is quiet. BC3's alpha indices live in a 48-bit field held as a 16-bit low word and
a 32-bit high one, and treating the low word as a full 32 bits reads zeros for every index from the
fifth on -- a plausible-looking wrong alpha, not a crash.

`run.sh <mesa-src>` compiles `bc_s3tc.comp` to SPIR-V with glslangValidator, translates that SPIR-V
to C++ with SPIRV-Cross, runs it on the host, and diffs whole 16x16 images against the reference
decoder `genref.py` extracts from the driver. It is the same anti-drift rule as
`bc-band-test/run.sh`: the artifact tested is the one that ships. **1.28M pixels, 0 mismatches** as
of this writing, and the check is worth having because it fails when it should -- deleting the
16-bit low word's straddle in `bits3()` (so the field is read as 32 bits) yields **14821
mismatches, all BC3**, exit 1.

- SPIRV-Cross's C++ backend names the push-constant member `pc` and then emits
  `register_push_constant(pc__)`, which does not exist, while its own `#define pc __res->pc.get()`
  shadows the member. **This no longer applies**: the parameters became a storage buffer (binding 2)
  when the push constant turned out to be shared state with the program's own command buffer, so
  the generated code has no `pc` member to rename, and `run.sh` no longer does the three `sed` edits.
- The test pads the source block rows on every third pass, which is what `bufferRowLength > width`
  does. The shader indexed source blocks by linear block index before, so a padded upload decoded
  garbage rows **and the check still passed**: both sides were wrong in the same way, because the
  reference decoder was handed the packed pitch. The shader now takes a `src_pitch` and the harness
  passes the padded row to the reference too. That is the failure shape to keep in mind here -- a
  differential test only catches a bug the two implementations disagree about.
- The shader runtime is header-only (`include/spirv_cross/`), so `libspirv-cross.a` is not needed;
  only the CLI is. `source.env` pins `SPIRV_CROSS_REF` and `GLM_REF`, and CI builds the CLI and
  clones glm before running the check. The distro `spirv-cross` package ships neither the runtime
  headers nor the CLI this needs.
- The check needs `SPIRV_CROSS_DIR` (a checkout: the CLI plus `include/spirv_cross/`) and `GLM_DIR`.
  It finds the CLI under `<dir>`, `<dir>/build` or `<dir>/bin`.

## The frame collapse is memory bandwidth, and `DROIDDECK_VN_EMULATE_BC=0` is the test that decides

The decode share (from the `BC textures emulated over ... ms decoding` line) against the `frames:`
line, over the 57 windows of `2026-10-27-09-steam`, is a step, not a slope:

    decode share   0-5%    5-20%   20-40%   40-60%   60%+
    mean fps       48.8    14.3     8.0      6.5     4.5      (corr -0.737)

A 5-20% share is 5-20% of the submit thread. It cannot cost 70% of the frame rate if the decode's
cost were only its own CPU time, and it does not behave like it is. Expressed per frame (decode ms
per frame = `decode_ms / (fps * window)`), the decode is **0.1%** of the frame in the fast windows
and **41.7%** in the slow ones (>100 ms/frame): large at the cliff, but not dominant -- more than
half the frame is something else. And the frame time left after subtracting the decode still *grows*
with it (corr **+0.44**, against **-0.645** for decode vs fps), so the decode is not only adding its
own time, it is slowing the rest of the frame down too.

The per-window rate says the same thing more directly. `decode_ms / output_MiB` is not a constant --
it runs from **2.6 to 10.6 ms/MiB** across the windows (median 4.57), with the same code and the same
uncached heap throughout. A fixed per-byte write cost cannot vary by 4x. What varies is how much else
is running: the decode is competing with the game's own traffic for memory bandwidth, and the windows
where it is slowest are the windows where the frame is slowest. That is why "remove the write cost
and the frame reaches the high 20s" overstates a decode optimisation: even in the worst windows the
decode is under half the frame, so removing it outright cannot do much better than double the rate.

So the number to get before building anything else is the BC-off one, and it is already a
no-rebuild knob: **`DROIDDECK_VN_EMULATE_BC=0`** (patch 0019, read in the guest via
`debug_get_bool_option`) turns BC emulation off. The textures that needed decoding stay empty, so
the picture is wrong on purpose -- but the `frames:` line answers the question:

- back to ~50 fps: the BC path is the whole collapse, and moving the decode off the CPU (or off the
  uncached heap) is the fix;
- still 3-7 fps: the collapse is the frame's own memory traffic, and no decode change reaches 60.

It has not been run in any log bundle so far, and it costs one setting change, so it comes before
the GPU integration, not after.

## The GPU BC decode ships as `guest-patches/0024`, on by default

The GPU decode (`DROIDDECK_VN_BC_GPU`, on by default) was developed in a working tree and existed
nowhere else until now: patches `0001`-`0023` reproduce the CPU-only driver, so a `build-guest.sh`
run from the repo built a guest ICD **without** the GPU path no matter what the working tree held.
`0024` carries it into the repo. It touches three files:

- `vn_droiddeck_bc.c` / `vn_droiddeck_bc.h`: the decode path itself.
- `vn_droiddeck_bc_spirv.h`: the embedded SPIR-V, a generated file. `bc-gpu-test/gen-spirv-header.py`
  regenerates it from `bc_s3tc.comp` and `run.sh` recompiles the `.comp` and fails if the array in
  the patch is not the same words, so the shipped shader cannot drift from the tested one. That
  check is the reason the header is committed rather than generated at build time: the guest ICD has
  no shader loader, so the array *is* the shader the GPU runs.

The parameters travel in a storage buffer, not a push constant. The dispatch is recorded into the
program's own command buffer, and push constants are state shared between the graphics and compute
bind points, so writing them would corrupt whatever the program pushed and has not re-pushed before
its next draw. Descriptor bindings are per bind point, which is why the params are binding 2.

The descriptor pool declares **two** pool sizes: the two plain storage buffers (`src`, `dst`) and
the one dynamic buffer (`params`). Declaring only the dynamic type makes `vkAllocateDescriptorSets`
fail once the pool is exercised -- the same "the picture is wrong, not the call" shape as the rest
of this file, and it is invisible to a shader-only test because the shader test never allocates a
real set.

### Which BC formats a program actually uses, and the shader now covering BC7

The claim that the game "uses BC1/BC3/BC5/BC7" came from the launcher's probe list, not from a
measurement: `app/src/main/cpp/deviceinfo/vkreport.c` asks about a fixed representative set
regardless of the program, and all of them report `optimal tiling features = 0x0`, so the emulated
path is what makes them work. Nothing in the driver logged which formats reached the BC path, so
the shader work was being aimed by that list alone. The stats line now carries one more line per
report:

    BC textures by format (images/compressed MiB): BC1 412/38, BC3 96/11, BC7 7/2

It is a single line, not one per format, because the guest log stops after 600 lines per process.
Read it before widening the shader again: if the program's compressed bytes are almost all BC1/BC3
the widening cannot move much of the decode off the CPU, and the frame collapse is elsewhere (see
the bandwidth section above).

**BC7 is decoded on the GPU as `kind == 4`, but only with `DROIDDECK_VN_BC7_GPU=1`.** It was the
largest real gap that also fits the existing destination: `bc_sub()` maps BC7 to RGBA8, four bytes a
texel, the same model the S3TC path already writes, so it needed no change to the copy path.
BC4/BC5/BC6H substitute to 1, 2 and 8 bytes a texel, so they do **not** fit the one-uint-per-pixel
output and would need that path reworked first; BC4/BC5 also already have a CPU decoder
(`bc_decode_rgtc`). The switch is off by default -- see "BC7 is off on the GPU by default again"
below for the measurement that put it there.

BC7 has eight modes and no fixed field layout, so its decode reads the block as bytes
(`bc7_byte`/`bc7_bits`) rather than as words, and the mode, partition and anchor tables are
generated from Mesa's own `texcompress_bptc_tmp.h` by `gen-bc7-tables.py`. They are generated, not
typed, because a wrong partition or anchor row does not crash -- it reads a plausible wrong texel.
`run.sh` regenerates them and `check-bc7-tables.py` fails if the shader's copy is not the same
numbers, the same anti-drift rule as `genref.py` and `check-embedded-spirv.py`.

The reference for the BC7 test is `decompress_rgba_unorm_block()` extracted from that header by
`genref-bc7.py`: the decoder the CPU path actually runs for BC7, since `bc_decode_s3tc()` and
`bc_decode_rgtc()` do not handle it and the job falls through to `util_format_translate`. `test_bc7`
compares whole images over 5000 random blocks, forcing each of the eight modes in turn plus the
reserved mode (byte 0 == 0), whose zero-fill the reference writes and the shader must match.

One bug that only the differential test could find: GLSL passes scalars by value, so
`bc7_endpoints()` advancing its own `bit_off` did not reach the caller, and the index offsets were
computed from the start of the endpoint block instead of the end. It compiled, it looked right, and
it put every pixel in the wrong subset. `bc7_endpoints()` returns the new offset now.

Two things the patch does **not** do, both recorded in the source: only the first Vulkan device
decodes on the GPU (a second device logs a warning), and the GPU path has never run on hardware.
There is no Vulkan implementation in the build environment, so it compiles and the shader is
differentially tested, but the pipeline, descriptor set and barriers around it are not. It is on by
default anyway, and `DROIDDECK_VN_BC_GPU=0` puts the decode back on the CPU without a rebuild, so
the A/B is one line in `droiddeck-env`. The fallback is the reason a default-on untested path is
defensible: every failure (no compute queue, no device-local memory, no descriptor set, a dispatch
too large, a copy inside a render pass) returns false before anything is recorded, and the caller
then records the CPU path into the same command buffer. A device that cannot run the dispatch gets
the code that already shipped, not a wrong picture.

**The value must be a value Mesa parses.** `debug_get_bool_option` accepts `1`/`0`, `true`/`false`,
`yes`/`no`, `y`/`n`, `t`/`f` (case-insensitive) and falls back to the **default** for anything else.
So `DROIDDECK_VN_BC_GPU=on` is not a true value -- it silently reads as the default, which before
this change meant "off". Write `=1` or `=true`.

### Turning it on or off on the device

The session builds `/usr/bin/env -i` and appends the switch file last, and `env` keeps the last
duplicate, so the file wins over anything the session set itself:

```
Android/data/com.droiddeck.launcher/files/droiddeck-env
```

Add `DROIDDECK_VN_BC_GPU=0` (CPU) or `DROIDDECK_VN_BC_GPU=1` (GPU, the default) there and restart
the session. This is the same file the `DROIDDECK_VN_SYNC_PRESENT` A/B uses. Whether the GPU path
is actually running is in `droiddeck_vulkan.txt` (see the log section above), which prints either

```
BC textures: decoding on the GPU (DROIDDECK_VN_BC_GPU), alignment N
```

or one of the two fallback lines (`... staying on the CPU`, `a second Vulkan device ...`). Absence
of all three means the copy never reached the BC path at all.

BC7 has its own switch in the same file, off unless set: `DROIDDECK_VN_BC7_GPU=1` sends BC7 to the
GPU, and leaving it unset (or `=0`) keeps it on the CPU. See the next section for why.

### BC7 is off on the GPU by default again (`DROIDDECK_VN_BC7_GPU`)

The BC7 GPU decode shipped on by default and is now opt-in, for two measured reasons.

- **It cannot move the frame rate.** The per-format stats line from the device says BC7 is **1.4%**
  of the compressed bytes (`BC7 44/8M` against `BC1 60.4%/349M` and `BC3 38.1%/220M`). Even a free
  decode of that share is inside the noise of a 24 fps frame. The FPS target is not reachable from
  this format, so leaving it on the CPU costs nothing that was ever going to be won.
- **It is the only thing that changed for BC7, and the logs after it shipped report black
  textures.** The shader itself is not suspect: `bc-gpu-test/run.sh` translates the shipped SPIR-V
  and compares it against Mesa's own `decompress_rgba_unorm_block()` over 1.28M pixels with all
  eight modes forced -- `0 mismatches`. The tables match `texcompress_bptc_tmp.h`, and the embedded
  array matches the tested shader. So the decode is byte-exact in the differential test while the
  GPU path around it has never run on hardware (there is no Vulkan implementation in the build
  environment), which is exactly the gap a test cannot cover.

`DROIDDECK_VN_BC7_GPU=1` puts BC7 back on the GPU for a re-test; the default (`=0`, and anything
Mesa does not parse as true) keeps it on the CPU, which is the decode that shipped before the BC7
commit and needs no new code. The rest of the GPU path (BC1/BC2/BC3) is unchanged and still on by
default.

Verified in `venus-bridge-build.yml` on `da47ddd`: the guest job compiled the ICD with all 24
patches, passed the band split, and printed `1280000 pixels compared, 0 mismatches` and `1280000
BC7 pixels compared, 0 mismatches, modes seen 0xff`. The published
`libvulkan_panvk.so` in `Venus-Bridge-guest-mesa-25.2.5-84c1064e.zip` carries the
`DROIDDECK_VN_BC7_GPU` string, so the switch is in the artifact the phone installs.

**A latent use-before-init came out of this.** `bc_gpu_copy()` dereferences `cmd->dd_bc` through
`bc_chunk_gpu_current()`, and `vn_dd_bc_copy_begin()` called it *before* the `calloc` that creates
`cmd->dd_bc` -- the allocation sat below the GPU call, on the CPU fallback only. It was masked in
practice (the first upload in a command buffer is usually larger than the 8 MiB per-copy cap, which
returns to the CPU path, and that path allocates the state), which is why 761 logged GPU decodes
did not crash. It is not masked for a small first upload. The allocation now happens before the GPU
call, so the state exists on both paths.

### Generating the patch

`guest-patches/*.patch` are `diff -ruN a/... b/...` output with `--- /dev/null` for new files (no
`index` line), which is what `git apply --whitespace=nowarn` in `build-guest.sh` consumes. To
regenerate one, diff a checkout with `0001`-`0023` applied against the working tree; diffing against
a checkout that already has the new file applied produces an empty patch, which is the failure mode
to watch for (the working tree had the GPU code in *untracked* files, so `git diff` never showed
it). After adding or changing a patch, confirm the whole series still applies to pristine Mesa:

```
git clone --depth 1 --branch mesa-25.2.5 https://gitlab.freedesktop.org/mesa/mesa.git /tmp/v
cd /tmp/v && for p in <repo>/tools/venus/guest-patches/*.patch; do git apply --whitespace=nowarn "$p"; done
```


## The GPU BC decode takes S3TC only, and it allocated per upload (2026-10-27-10 bundle)

The `2026-10-27-10-steam` bundle was re-fetched to re-check the GPU decode. It confirms the path is
live -- `BC textures: decoding on the GPU (DROIDDECK_VN_BC_GPU), alignment 64` appears once, for pid
13490 -- and it shows why the path does not lift the frame rate by itself.

**The path takes S3TC only.** `bc_gpu_kind()` maps DXT1/DXT3/DXT5 (BC1/BC2/BC3) and returns -1 for
everything else, and the caller additionally requires the substitute format's block size to be 4
bytes. So BC4 (R8), BC5 (R8G8), BC6H (RGBA16F) and BC7 (RGBA8, but with no `bc_gpu_kind` case) all
fall to the CPU. The `android-vulkan.txt` probe in the same bundle shows BC1/BC3/BC5/BC7 are all
unsupported by the Mali driver (`=0x0`), so every one of them is substituted and emulated. The stats
line separates the two paths without a trace: the GPU path adds `decode_ns = 0`, the CPU path adds
its wall time, so any nonzero `ms decoding` means the CPU ran. In the run with the GPU path on,
`22:49:28` reads `1046 uploads (1046 regions) decoded, ... 3675.2 ms decoding`, so the CPU decoded
most of them, and the frame rate over those windows falls 51.7 -> 24.5 -> 20.6 -> 4.0 fps.

**It allocated a fresh chunk per upload.** `bc_gpu_copy()` called `bc_chunk_gpu_new()`
unconditionally, which creates three buffers, allocates three memories and allocates a descriptor
set, and `vn_dd_bc_cmd_reset()` then destroyed all of it. `0022` had already given the CPU path a
pool; the GPU path had not. The host log shows the cost: `vkAllocateMemory` up to `2953x/1076 ms` in
a single 10 s window, and `2345x/840 ms` and `2137x/691 ms` in two more. Three allocations per
upload at ~1000-2600 uploads per window is the same order as those counts, and after `0022` the GPU
path is the only remaining per-upload allocator. (One window shows `vkCreateComputePipelines
111x/10051 ms`, max 165.9 ms each; that is a one-off shader-compile warmup, not the steady-state
cost, and it is not the BC pipeline.)

`0024` now pools and packs the GPU chunks the way `0022` pools the CPU ones: the chunk carries
capacity cursors for all three buffers, `bc_chunk_gpu_current()` packs another copy into the last
chunk when it has room (it almost always does -- the minimums are 8 MiB dst/src and 512 KiB params,
against a per-copy cap of 8 MiB in `bc_gpu_copy`), `bc_chunk_take_pooled()` matches on all three
sizes, and a reset returns a GPU chunk to the pool instead of destroying it. `vn_dd_bc_device_fini()`
destroys pooled GPU chunks in full (descriptor set, params, src, dst) before `bc_gpu_dev_fini()`
tears down the descriptor pool. The stats line gained `N on the GPU` so a log says what share of the
uploads left the CPU.

Still open, and why this is not yet the 60 fps fix: the path is S3TC-only, so BC4/BC5/BC6H/BC7 --
the formats the probe says this driver must emulate -- still decode on the CPU. Widening the shader
to those formats is the next step and needs a new `.comp`, a new reference decoder in `genref.py`
and a new embedded SPIR-V array; it is not a config change.

## The GPU BC decode declined the large textures, on a bound its own source size inflated

`0024`'s GPU path is on by default, but every bundle on this machine predates it -- no `BC textures`
line carries the `N on the GPU` counter that `0024` added -- so the path has no on-device evidence
yet, in either direction. What the code says is that `bc_gpu_copy()` gave up on two size tests, and
one of them was wrong:

- `src_total` was computed as `align(w4 * h4 * 4 * slices)`, the *uncompressed* size, while the copy
  it reserves for is `src_slice * slices` compressed -- `block` bytes a 4x4 block, so 4x smaller for
  BC1 and 2x for BC3/BC7. It is now computed from the same `bufferRowLength`/`bufferImageHeight`
  strides pass two copies with. Because the old expression was identical to `dst_total`, this did not
  change which copies were accepted; it changes how much source capacity a chunk reserves.
- The per-copy cap was `8u << 20` on both totals. The destination is RGBA8, so 8 MiB is 2 Mi blocks,
  i.e. nothing past ~1448x1448: a 2048x2048 texture expands to 16 MiB and fell to the CPU, and with
  it the sizes most of a level's bytes sit in. `bc_gpu_max_bytes()` now reads
  `DROIDDECK_VN_BC_GPU_MAX_MB` (default 16, clamped 1..256), so the bound is 16 MiB and can be A/B'd
  on a device without a rebuild. The chunk floor stays 8 MiB; a larger copy allocates its own size,
  and `bc_chunk_take_pooled()` matches on all three capacities, so the pool is not over-read.

This is not by itself the 2x. The cap decides the tail: the mean upload in these logs is small --
`1651 uploads` expand to `1013 MiB`, so ~0.6 MiB of output each -- and most were already under 8 MiB.
Raising the cap removes the fallback for the large textures, which is where the bytes are, but
whether that pays depends on the GPU path's own cost (the staging copy, the dispatch, and a compute
shader through venus), which only a device log with the `on the GPU` counter can settle.

One earlier argument for the uncached-write cost does not survive the compiler. The section above
notes the inner loop stores each pixel as four byte writes into host-visible memory; in the shipped
build it does not: `bc_decode_task`'s disassembly has one `movb` and 122 32-bit stores, so GCC
coalesces `row[i*4+0..3]` into one store. The byte-store count is a property of the source, not of
the code that runs, so it cannot be offered as the machine-level cost of the decode.

The driver that carries this is built and published by CI, not locally. On `0810dcc` the
`venus-bridge-build` run compiled the ICD with all 24 patches, passed the band split (`75/75`) and
the shader check (`0 mismatches`, S3TC and BC7), and the `publish` job clobbered the
`venus-bridge-latest` release at 20:59. The guest zip's `libvulkan_panvk.so` carries
`DROIDDECK_VN_BC_GPU_MAX_MB`, a string that exists only in the new patch, so the artifact the phone
installs contains the raised cap and not the old 8 MiB bound. Two things about that check are worth
knowing: the release's `target_commitish` stays at `84b66bfa` because the tag was created once and
`gh release upload --clobber` only replaces assets, so the target is not the run that produced them
-- the asset `created_at` is; and the published `libvulkan_panvk.so` is AArch64, so the default
cannot be read out of the shipped binary here without an ARM disassembler, which is why the string
is the evidence rather than the constant.

Also worth checking before raising that cap: the use-before-init below was *masked* by the cap
(`the first upload in a command buffer is usually larger than the 8 MiB per-copy cap`). The fix for
it is in `vn_dd_bc_copy_begin()`, which now `calloc`s `cmd->dd_bc` before the GPU call, so the cap
can move without exposing it. A smaller first upload that now takes the GPU path is exactly the case
that used to crash, so that ordering is a precondition of this change and not an independent fix.

## The power levers a plain app cannot have, and what the drawer offers instead

The session drawer's performance page was extended with the Deck-QAM controls that are reachable
without root. Three of the Deck's are not, and they are recorded here so the next attempt does not
re-derive it:

- **TDP.** SteamOS writes an SMU power limit through the kernel's power interface. Android has no
  equivalent a normal app may call: the power HAL (`android.hardware.power`) is a vendor service
  reached over binder, and the kernel's `sysfs` nodes for it (`/sys/class/power_supply/*/…`,
  `/sys/devices/system/cpu/cpu*/cpufreq/*`) are root-owned and not writable by the app's uid. There
  is no public `PowerManager` call that sets a watt or thermal budget.
- **Mali GPU clock / DVFS.** The `gpuClockPin` switch the Performance page already had is the
  **Adreno** path: `GpuClockPin.start()` calls `nativeSetGpuTurbo()`, which is
  `adrenotools_set_turbo()` (see `waylandcomp_jni.c`), and it is gated on `/dev/kgsl-3d0` existing.
  Neither holds on this target. Tensor G4 is **Mali-G715 MC7**, there is no `/dev/kgsl-3d0`, and
  `GpuClockPin` reports `gpu clock: no /dev/kgsl-3d0, left governed`. The Mali equivalent was the
  kbase DVFS `sysfs` pinning, and `mali-gl/patches/0005` **deletes** it outright (see "The kbase
  tunables are now automatic"), so it is not a knob on this branch either.
- **`GameManager.setGameMode()`.** Hidden `SystemApi` behind the `android.permission.MANAGE_GAME_MODE`
  signature|privileged permission, so it is not callable from a sideloaded app.

What the drawer does offer, and why each is safe unprivileged:

- **Refresh-rate cap** (`mode_refresh` / `display_frame_rate`). Only *lowers* the ceiling the
  session picks its display mode under, through the public display-mode API. The Android
  `peak_refresh_rate` setting and the glasses' output rate still cap it on top, so it can never ask
  for a rate the system forbids. `SessionPrefs.tightestRefreshLimit()` combines it with the mirror's
  rate; 0 means "the panel's own", which is the default and the previous behaviour.
- **Frame limit** (`mode_fps`). The live native knob (`g_fps_limit`), unchanged; the drawer now
  exposes it on the DISPLAY page next to the refresh cap.
- **Sustained performance mode** (`mode_sustained`). `Window.setSustainedPerformanceMode()`, a
  public no-permission call. It is a *window* flag, so it is applied in `onResume` as well as when
  the switch is flipped -- the activity's window is replaced on resume and the flag goes with it.
- **Performance overlay level** (`overlay_off` / `overlay_level_1..3`). The app's own HUD, so it is
  free. Level 1 is the frame-rate line the HUD always drew; 2 adds the zero-copy counters and 3 the
  HDR and frame-gen numbers, all read from `WaylandCompositor` natives that are wrapped in `try`
  because they are only valid once the compositor has a surface.
- **ADPF thread hints** (`perf_threadhints`). `PerformanceHintManager`, public since API 31, already
  implemented in `GameThreadHints`. It had a preference and a reader but no setter and no UI; both
  were added.

One bug worth recording, because the same shape will recur: the new per-mode writes first used
`SessionState.mode`, but a game launched from the library runs in `MODE_RUN` while the *reader* uses
`SessionPrefs.prefMode()`, which maps `run` -> `steam`. A change made from a game's session would
have landed on `refreshLimit.run`, a key no later session reads. Every per-mode read and write in
`SessionActivity` now goes through one `prefMode()` helper.

## The 2026-10-28-06 bundle: the plateau is CPU + present-wait in series, and the collapse is not the decode

This bundle is the first one with both builds in the same file, so the plateau can be compared
directly rather than across sessions. It answers the "framepacing still happens" report and it
retires three of the open candidates.

**The two builds.** `steam/droiddeck_vulkan.txt` is a concatenation of many sessions, and the BC
stats line distinguishes them without a version string: the `on the GPU` field only exists in the
build with the raised `DROIDDECK_VN_BC_GPU_MAX_MB` cap (`0810dcc` and later), and the run with
`BC textures: decoding on the GPU ... alignment 64` is the GPU-decode build. Both are in the file,
interleaved, so any per-window number has to be read with its build.

**The plateau did not move, and it is a serialization.** Every plateau window, on both builds, sits
at 17.3-20.3 ms/frame, i.e. ~51 fps. Across the 18 plateau windows the frame time decomposes
exactly:

    mean ms/frame = 19.72   mean present GPU wait = 7.00   mean cpu = 12.72
    12.72 + 7.00 = 19.72

so the frame is CPU time **plus** the present wait, not their maximum. That is the signature of the
drain: with the queue drained before every present the CPU cannot run ahead of the GPU by even one
frame. The GPU-decode build has the same plateau (18.98, 19.28, 19.33, 19.35 ms) as the CPU-decode
one (17.25-20.15 ms), so the decode is not what the plateau is made of.

**The drain is on, and the app is why.** `SessionService.kt:580` adds
`DROIDDECK_VN_SYNC_PRESENT=1` on the venus branch (commit `fcb2347`, an ancestor of HEAD), which
re-enables the drain patch 0021 turned off by default. The log confirms it from the other side: the
`present GPU wait` field is only ever assigned inside `if (sync_present)`, and it is nonzero
(up to 94.4 ms) in the runs that show the plateau. The runs with `present GPU wait 0.00` are the
ones launched with the per-game override.

**The collapse to 3-6 fps is not the decode, and not the drain.** Two clean separations:

  * Windows with `present GPU wait 0.00` still read 9.8-19.6 fps, so the drain is not what produces
    the collapse (it is what caps the plateau).
  * The decode share is weak-to-moderate at best (`corr(decode_ms, fps) = -0.54`, `-0.29` on the
    strict window set) and there are windows with **0-0.5% decode share and 9-19 fps**. So decode is
    a contributor while the level streams, not the steady-state cause.

**What does scale with the collapse is the descriptor storm.** In the fast window the host ring is
`766 ms busy of 10 s` with `vkAllocateDescriptorSets 3413x`; in the slow window it is `2333-2915 ms
busy of 10 s` with `vkAllocateDescriptorSets 11173x -> 66782x` and `vkUpdateDescriptorSets
8027x -> 49499x`. The per-10s allocation count grows 20-50x exactly where the frame rate falls.
`corr(vkExecuteCommandStreamsMESA ms, fps) = -0.57` is the strongest host correlation found.
`guest VK_ERROR_OUT_OF_POOL_MEMORY` is ~490 per run in every run (flat), and patch 0023's
`deferring to the real pool` line fires once per run -- so the guest's shadow accounting is wrong
once, 0023 hands the decision to the driver, and the real pool is what refuses afterwards. The
refusals themselves are DXVK's own pools, which is why they are not the frame cost either.

**Host-visible allocations are heavy, but "total" is not live memory.** The host logs
`dma-heap ... total 1624, 15688.6 MiB` against a `heap 0 (15356 MiB)`. Read `vkr_dd_memory_stats`
in `tools/venus/patches/0009` before quoting that: `count`/`bytes` are per-interval, and
`total_count`/`total_bytes` are **cumulative since process start**, so `total 1624, 15688.6 MiB`
means 1624 allocations totalling 15.7 GB were *made*, not that 15.7 GB is resident. The heap
number is not evidence of exhaustion or a leak. What it does show is churn: the peak interval was
654 allocations / 6483 MiB in 10 s. Host-visible memory comes from
`/dev/dma_heap/system-uncached`, so every guest upload that lands there is uncached on the CPU side
too, which is the real cost.

**What was tried and did not help.** The GPU BC decode (0024) with the raised cap took the decode
cost from 3675 ms to 255 ms in the best windows and the plateau is byte-identical, so the decode is
not on the critical path at the plateau. The descriptor deferral (0023) is correct but does not
remove the host refusals. The BC7 GPU decode is off by default and stays off.

**The fix this bundle argues for.** The plateau is the drain, the app forces the drain on, and the
drain is a full `vkQueueWaitIdle`. The ordering it exists for -- the server must not read a
swapchain image the frame is still writing -- is already available more cheaply: the ring seqno of
the present's own submit, which is exactly what the implicit-fencing path in the same function
waits on. Patch 0021 now waits on that seqno when `DROIDDECK_VN_SYNC_PRESENT=1` and falls back to
`vn_QueueWaitIdle` only when there is no seqno to wait on. The wait still covers the submit that
hands the buffer to the server, so the ordering the drain existed for is kept by construction, but
the CPU is no longer parked until the GPU is idle, so the frame can go back to max(cpu, gpu)
instead of cpu + gpu. That the seqno wait is *sufficient* for the white-frame case is reasoning
from the implicit-fencing path in the same function, not something this bundle measured: the
white-frame regression itself was never captured, so the fix is not yet confirmed against it.
`DROIDDECK_VN_SYNC_PRESENT=0` remains the fast-path control and `=1` the ordered-present control
(see the `a0deac6` note above: `=1` is a seqno wait now, not the drain), so the next bundle can A/B
the three (unset, =0, =1) without a rebuild.

## The 2026-10-29-04 bundle: the limiter is gone, the plateau is gone, and the stutter tracks the decode

This is the first bundle from a build with the frame limiter removed (`55fa3d8`), and it is the
first that can answer the limiter section above. It is *not* a build with the per-build driver
identity (`1076a5a`, 14:16Z): the session ran 12:22:50Z-12:28:38Z, so `device.txt` still reads the
build-invariant `DroidDeck-venus-bridge mesa-25.2.5 mesa-25.2.5 84c1064e` and there is no `Commit`
line. It does carry `a0deac6`/`7f3415a` (the seqno present) and `55fa3d8`, and its launch log
confirms the limiter is gone:

    prefix bundle   Found config env: dxvk.maxFrameRate = 60; dxvk.enableGraphicsPipelineLibrary = False
    this bundle     Found config env: dxvk.enableGraphicsPipelineLibrary = False

**The limiter was the plateau, and removing it removed the plateau.** The 29.9-30.0 fps / ~33.4 ms
signature appears **1 of 6** game windows here against **26 of 150** in the bundle that still had the
limiter. The window that does sit there is the first one (29.7 fps), which is the level load before
the decode starts, not a steady state. So `55fa3d8` is confirmed against the report it was written
for.

**What is left is a collapse, and the decode tracks it.** The game runs 29.7 fps at 14:23:44,
11.9 at 14:24:03, and then 5-7 fps from 14:24:16 to the end of the run. The decode windows line up
with it:

| window | decode ms | share of window | fps after |
|---|---|---|---|
| 14:24:02 | 0 (1 upload) | 0% | 11.9 |
| 14:24:12 | 1757 | 17.4% | 5.0 |
| 14:24:23 | 1964 | 19.3% | 7.3 |
| 14:24:33 | 2470 | 24.7% | 5.5 |
| 14:24:43 | 1364 | 13.6% | 6.1 |

`corr(decode share, fps) = -0.873` over those five windows, the same sign and a similar strength to
the -0.737 recorded for `2026-10-27-09`. The decode is running on the GPU (4220 of 4369 uploads in
the worst window) with zero fallback copies and zero "uploads from memory the driver cannot read",
so this is the *fixed* GPU path. But the magnitude does not add up: a 13-25% share cannot by itself
cost the 6x between 33.7 ms and 180 ms/frame, so the decode is a marker for the collapse rather than
its whole price -- exactly the tension the memory-bandwidth section records. `DROIDDECK_VN_EMULATE_BC=0`
is still the one-setting test that separates "the BC path is the collapse" from "the frame's own
traffic is", it is reachable from the `droiddeck-env` file with no rebuild, and it has still not been
run in any bundle.

**The stutter the report is about is the guest waiting on its own submits, and it grows with the
decode.** The guest's wait report totals 15.1% of wall time in the first game window and 75.8% in
the last, and the buckets are ordered `semaphore` then `ring seqno` then `fence`:

    over  10s: semaphore 889 ms, ring seqno 594 ms, fence 30 ms      (14:23:39, 15.1% total)
    over  10s: semaphore 5533 ms, ring seqno 1581 ms, ring space 420 ms, fence 49 ms  (14:24:52, 75.8%)

`semaphore` is 72% of the last window's wait. `vkWaitSemaphores`/`vkQueueSubmit2` are also the top
host cost in the same windows (899x 186 ms and 1087x 182 ms in one 10 s window), so the wait is real
on both sides, not a guest-side accounting artifact. This is what a frame that misses its budget by
a little looks like when the present is ordered on the ring seqno: the CPU is not parked until the
GPU idles any more, but every frame that needs the previous frame's semaphore now waits for it.

**The frame times are quantized to the vblank, so the cost and the pacing are separable here too.**
Every game window's ms/frame is within 0.1-2.7% of an integer multiple of 16.667 ms:

    33.71 = 2.02 vblanks   83.82 = 5.03   199.55 = 11.97   136.87 = 8.21   183.09 = 10.99   164.78 = 9.89

The same holds in the prefix bundle, so this is not new. It means the frame is presenting at the next
vblank after its work finishes, which is what you want; the collapse is the work, not the pacing.
It also means a frame's cost is being rounded up to a vblank boundary, so a decode cost just over
one vblank budget costs two.

**The descriptor storm is unchanged and still not the frame rate.** The host refuses
`vkAllocateDescriptorSets` 10806 times and the per-10s guest count reaches 237,610, against 290,288
in the prefix bundle -- the same behaviour, in a session with a completely different frame curve.
That is the third independent confirmation of the section above: the storm is a bug worth fixing and
it is not the collapse.

**The drain is gone, as designed.** The present GPU wait is 0.79-7.89 ms (2.5-5.8% of the frame),
against the 17.6 ms / 89% drain the 0021 work was written for. So `a0deac6`'s seqno wait is doing
what it claims: the present is ordered without the CPU parking until the GPU is idle. What it does
not do is fix the collapse, which was never the drain.

**The display flap storm is fixed.** `perf: display` appears 3 times, all `60 Hz`, against 2242
alternating lines in the bundle that found it. `app.log` also shows `fps limit 0` and the session
report shows `Session refresh 60.00 Hz` / `Panel refresh modes 120 Hz, 60 Hz`, so the nested refresh
is still capped at 60 and `BL_MALI_REFRESH=120` remains the untried lever for the pacing half.

## The 30 fps plateau is a stale DXVK frame limiter, and the app republished it forever

The `2026-10-29-02-steam` bundle pins the plateau to an exact number. Across every Lake run the
frame time is either the frame's own work or **33.37 ms**, and 33.37 ms is two 16.67 ms vblanks,
not one. The plateau's fps is 29.9-30.0, to the tenth, in every run that holds it. A frame time
that lands on a multiple of the vblank is pacing, not cost, and the host agrees: the guest's ring
is 19% busy, `render_scene` is 3.2 ms, and the compositor ticks 60/s while compositing exactly
300 scenes per 10 s (0.499, a hard half rate).

The pacing is DXVK's own limiter. The guest environment the app publishes carries

    DXVK_CONFIG = "dxvk.maxFrameRate = 60; dxvk.enableGraphicsPipelineLibrary = False"

and `dxvk.maxFrameRate = 60` on a 60 Hz panel is the worst case for DXVK's limiter: it paces to
60, yields to vsync, and the frame then lands on every second vblank. DXVK's own `dxvk.conf`
warns against exactly this and says an external limiter "WILL provide a significantly better
experience". The value is not an app default any more -- it came from the per-game DXVK option
editor removed in `f720f53`, whose text field defaulted to `dxvk.maxFrameRate = 60`. Removing the
editor removed the only way to change it, but `GameEnvironmentStore.publish` re-reads the saved
map from disk and republishes it to every launch, so the limiter is permanent and invisible.

`GameEnvironment.withoutFrameLimiter` now strips a `dxvk|dxgi|d3d9.maxFrameRate` option on the
publish path only. The saved settings keep whatever the user has, so nothing is destroyed; only
the guest snapshot loses the limiter, and the session's own frame limiter stays the supported
knob. The other option in that string, `enableGraphicsPipelineLibrary = False`, is left alone: it
is a hand-set value, it is `Auto` by default, and turning GPL off costs pipeline-compile time
rather than frame rate.

What this does **not** explain is why the same build and the same limiter gave 52.7 fps at 15:25
and 27.2 fps at 20:28. The limiter sets the ceiling and the vblank decides whether a frame clears
16.67 ms, so the run-to-run split is the frame's own work crossing that line, and that is the
separate, still-open question. With the limiter gone a frame just over the budget should present
at the next vblank instead of the second one, which is the difference between ~30 and ~52 fps.

### Which build a bundle came from, and why that was unanswerable

The `2026-10-28-06-steam` bundle (fex.net share `tvosscy`) is the one the plateau analysis above
reads. Two provenance facts about it matter more than its numbers, because they apply to every
bundle collected before this session:

- **The guest ICD could not name its own build.** `device.txt`'s `Linux driver` line read
  `DroidDeck-venus-bridge mesa-25.2.5 mesa-25.2.5 84c1064e` on this bundle *and* on
  2026-10-29-02: `build-guest.sh` cloned the upstream `mesa-25.2.5` tag, applied DroidDeck's
  patches without committing them, and then derived `meta.json`'s `name`/`driverVersion` and the
  zip name from that tag's commit. No DroidDeck commit could reach any of the three, so the driver
  menu, the session report and the artifact name were identical on every build the branch ever
  produced. `app/build.gradle`'s `versionCode` 13 is hand-bumped and equally static, so `Version
  0.4.0 (13)` does not identify a build either. A report from a stale install therefore looks
  exactly like one from the current build -- which is how a fixed build gets reported broken.
  `build-guest.sh` now appends DroidDeck's own commit as `dd.<sha>` to the name, the version and
  the zip name and records `droiddeckCommit` in `meta.json`; `DeviceReport` prints `Build` and
  `Commit` from `BuildConfig`. Read those two before attributing a result to a build.
- **`session.log`'s `mali-gl <sha> mesa <sha>` line is not the build under test.** It names the
  **mali-gl runtime's** commit (a separate release the APK downloads, per `mali-gl/release.env`),
  not the APK's and not the guest ICD's. It happens to read `1806802` here, a 2026-10-24 commit
  that is on neither `main` nor this branch, which is easy to mistake for the run's driver.

One caveat on treating bundles as independent samples: this bundle's `steam/droiddeck_vulkan.txt`
is a raw byte-prefix of the 2026-10-29-02 one (identical md5 `c27fa0d2ed72ddba8c099a098cac17ac`
over all 2,835,755 bytes; 20,759 of that bundle's 24,020 lines). The log is appended across
sessions, so a later bundle can contain an earlier one whole. Check the prefix before counting two
bundles as two data points. This session ran 21:21:42Z to 21:28:55Z on 2026-10-28 (`events.jsonl`;
`device.txt` prints the same instant as `23:21:42 GMT+02:00`), ending about 2.5 h before `a0deac6`
(23:52Z) and `7f3415a` (23:58Z), so it does not carry the seqno-present change and the plateau it
shows is the drain, as the section above establishes.

The `2026-10-29-04-steam` bundle repeats the pattern: its `steam/droiddeck_vulkan.txt` carries the
whole `2026-10-29-02-steam` file as a byte-prefix (byte-identical over the first 3,281,184 bytes,
md5 `8abaa3698a9134bbe5c6054890ffd0a0`), and only the last 1,505 lines are the new session
(12:22:50Z-12:28:38Z). So the file's 221 frame windows are 215 from the earlier session and 6 from
this one -- read the tail, not the file, and count 6 windows, not 221.

## Getting the log bundle out of fex.net, and the 2026-10-30-01 regression report

The user's bundles arrive as a fex.net share, and the download URL in the API metadata
(`GET https://api.fex.net/api/v1/file/share/<code>`) answers `307 -> /403` to a bare `curl` and
`401 {"error":"Unauthorized","source":"storaged"}` to the SPA's own JWT cookie. Neither is the way
in. The share page fetches an **anonymous** token and sends it as a bearer:

    curl -s https://api.fex.net/api/v1/anonymous/upload-token          # -> {"token":"eyJ..."}
    curl -sSL -H "Authorization: Bearer $token" https://fs19.fex.net/download/<id>/<code> -o bundle.zip

That is `Share.refreshAnonToken` in the SPA bundle; the token is a normal (unauthenticated) user
token, so no login and no browser session is needed. Check the result with `python3 -c
'import zipfile;zipfile.ZipFile("bundle.zip")'` before trusting it -- the 403 page is HTML.

The `2026-10-30-01-steam` bundle is the report **"Stuttering still exist. And it became worse"**. It
runs `Build remote clean · pr4 at 18d030e` (`device.txt`), so it is the first bundle from the commit
that moved the driver's knobs into the driver's zip. Three things are worth recording.

**The driver binary did not change between `0b68348` and `18d030e`, so no frame result can.** The
two zips' `libvulkan_panvk.so` are the same file
(`sha256 198c18f1b5d45acdcea84eaf6cfa117e05e78ad9563b590ee364dbca7019c468`, 2,912,968 bytes), the
`gl/env` files are byte-identical, and the only diff in the whole zip is comment text in the root
`env`. `18d030e` touched no patch and no guest source, so the "worse" is not the driver.

**The applied environment is identical too.** Both bundles log the same entries
(`driver environment: DROIDDECK_VN_SYNC_PRESENT=1 DROIDDECK_VN_BC_GPU=1 __GLX_... BL_MALI_CEF=gl
DROIDDECK_KOPPER_* ...`), because the knob values did not change -- only who states them. So the
regression is not a changed variable.

**What `18d030e` really changed is that the user can no longer set one.** It applied the driver's env
last and deleted any earlier entry of a name the driver stated, so `DROIDDECK_VN_SYNC_PRESENT=0` (or
any other A/B line) in `droiddeck-env` was silently discarded. The fix restores the precedence the
project depends on: the driver's entries go in after the app's own lines, the user's file and the
agent go in after the driver.

Read the frame line from the bundle's tail, not the file. `droiddeck_vulkan.txt` is a rolling log
that survives sessions: the 10-30 bundle's first line is byte-identical to the 10-29 bundle's
(`23:41:01.132 [4789 steamsysinfo -steamid]`), so a whole-file scan counts other sessions' windows.
Filter to the session's own `HH:MM:SS` range first. On that basis the 10-30 session is not worse than
the 10-29 one -- both collapse to 3-8 fps within seconds of the first frame
(`11:06:31` 227 fps -> `11:07:29` 2.0 fps; `08:24:11` 51.7 -> `08:25:03` 5.8), and the collapse
windows are the same shape. The "worse" is the pre-existing collapse plus the loss of the escape
hatch, not a new mechanism.

## The driver owns its environment now (`tools/venus/guest-env`, `env` in the zip)

Every knob this file recommends setting was unreachable on the device. The app spawns the guest
with `/usr/bin/env -i` and builds the whole environment itself (`SessionService.addClientEnvironment`),
so the only route for a variable was the user's env-switch file
(`Android/data/com.droiddeck.launcher/files/droiddeck-env`), which `extraEnv()` reads from a path
that did not exist in the bundles -- `device.txt` has no `droiddeck-env` line, so the file was
never there and every A/B that asked the user to add a line was a no-op. The reports that followed
("framepacing still happens", "there are still lags", "there are still stutter") were collected
against a driver whose knobs nobody could set.

The driver carries them itself now. `tools/venus/guest-env` is copied to the zip root as `env`
by `build-guest.sh`, and the app reads it when the driver is selected:

- `LinuxVulkanDriverManager.installDriver` keeps a root `env` entry (it used to drop every file it
  did not recognise, so the channel could not exist before this).
- `driverEnvironment(id, current)` parses it with the same reader as the GL part's `gl/env`:
  one `NAME=value` per line, `#` comments, `KEY+=value` appends with `:`, `@GL@` is the driver
  directory. The syntax is shared, so there is one parser and one test for both.
- `applyDriverEnvironment(id, guest)` adds the driver's entries after the app's own lines. It reads
  the root `env` and then `gl/env` in one pass, and `SessionService` calls it **before**
  `extraEnv()` and before `AgentEnv`, so a line in the user's `droiddeck-env` still wins over a knob
  the driver states. The app itself sets no driver knob: the `DROIDDECK_VN_SYNC_PRESENT=1` line it
  used to add is gone, so a driver knob has exactly one home, the zip.

The user's file staying last is deliberate and is the thing `18d030e` got wrong. That commit also
applied the driver last and **deleted** any earlier entry of a name the driver stated
(`applyEnvironment`, `guest.removeIf(startsWith(name + "="))`), which made every driver knob
unoverridable from the device. Every on-device A/B in this project is an edit to `droiddeck-env`
(`DROIDDECK_VN_SYNC_PRESENT=0` for the fast path, `DROIDDECK_VN_EMULATE_BC=0` to test the decode),
so the commit removed the only escape hatch that does not need another driver build -- and it did
so in the same release the user was told to test with. The next commit restores the precedence:
driver after the app's lines, user and agent after the driver.

The `gl/env` copy stays for the same reason it always did -- it is the channel **every shipped APK**
reads (`glEnvironment`, in the release since `51c52f2`), and an APK that predates the root file
applies it the old way, last-but-before the user's file. On a current APK the two files are read in
one pass, root then GL part, so a line in `gl/env` wins over the same line in the root file and a
`+=` line there appends to what the root file produced.

What ships baked in, and why these two:

- `DROIDDECK_VN_SYNC_PRESENT=1` -- the ordered present. The app used to set this line itself
  (`SessionService.kt`); it no longer does, so the driver's `env` is now the only place it is
  stated. That is what makes it the driver's decision rather than the APK's.
- `DROIDDECK_VN_BC_GPU=1` -- the GPU BC decode, which is already the default. Stated so the zip
  records which decode it carries, since the artifact is what a tester installs.

The rest of the known knobs are in the file as commented lines with their evidence, so an
experiment is a one-line edit, a push, and a driver install -- no APK, no file on the phone, no
manual env. The `env` file is checked by `tools/tests/test_venus_guest_env.py`: every active name
must be one the guest actually reads (a typo is otherwise silent -- the guest `getenv()`s a name
nothing defines), values must be `0`/`1`/a number (`debug_get_bool_option` falls back to its
default for anything else, so `=on` reads as the default), no duplicate names, and the build must
both ship the file and fail if it is empty.

## The 2026-10-30-02 bundle: the fix ran, the stutter is unchanged, and the GPU BC decode is cleared

The report is "Didn't help to fix stuttering" against the `82bcf65` build. `device.txt` reads
`Build remote clean · pr4 at 82bcf65` and `Linux driver ... dd.82bcf651`, so this bundle is the
first one that carries the precedence fix -- the user's `droiddeck-env` can override a driver knob
again. It does not help, and the reason is the one recorded two sections up: `82bcf65` restored the
escape hatch, it never claimed to be the stutter fix. The stutter is the pre-existing collapse.

**Retrieving this bundle needs a browser User-Agent.** The share is `is_anon: false`, so the
anonymous token alone is not enough: `fs17.fex.net/download/<id>/<code>` answers `410` to
`curl -H "Authorization: Bearer $token"` with no UA and `200` with `-A "Mozilla/5.0 ... Chrome/120"`.
The 410 is the gate, not a dead link -- do not conclude the share expired.

**The collapse is not the GPU BC decode.** This is the natural experiment the earlier notes wanted:
sessions before 10-29 ran the CPU decode (`0019`/`0022`, no "decoding on the GPU" line), sessions
from 10-29 on ran the GPU decode (`0024`). The collapse rate is the same:

| bundle | decode | frame windows | fps p50 | share <15 fps |
|---|---|---|---|---|
| 10-26-21 | CPU | 22 | 45.1 | 27% |
| 10-27-04 | CPU | 48 | 20.6 | 44% |
| 10-27-09 | CPU | 106 | 17.3 | 48% |
| 10-29-02 | GPU | 221 | 17.3 | 46% |
| 10-29-07 | GPU | 268 | 15.7 | 48% |
| 10-30-02 | GPU | 11 | 26.7 | 45% |

Every session starts at 46-52 fps and ends in the 3-8 fps band; the GPU decode moved the decode
cost (2.9 -> 1.9 ms/upload) and left the collapse where it was. So `DROIDDECK_VN_EMULATE_BC=0` is
not the discriminator it was recorded as being, and the decode share (13-25% of a window) cannot
pay for a 6x frame-time change either. The decode is a marker, not the price.

**The one signature that does track the GPU decode is the 30.0 fps half-rate plateau**, and it is
worth its own test rather than being folded into the collapse: it appears in 25 windows in each of
the two GPU-decode bundles and in **0** windows of all five CPU-decode bundles. `55fa3d8` removed
the DXVK frame limiter and `2026-10-29-04` showed the plateau gone, so a plateau that returns only
with the GPU decode is either a present that lands on every second vblank or a limiter the decode's
own submits re-introduce. Not established; recorded as the next thing to measure.

**The compositor blocks on its own ring in every session, and that is not the collapse either.**
`gamescope --backend` reports `tls ring seqno` waits of ~9.9 s in a 10 s window at startup and
~7.1 s in the next window, in the CPU-decode bundles and the GPU ones alike (`10-27-09` 11:48:02
and `10-30-02` 12:38:01 are the same numbers to a tenth of a percent). It is a fixed startup cost
in the first two windows, not a state the session degrades into, and it is the same mechanism the
`VN_RELAX_REASON_TLS_RING_SEQNO` note describes.

**The decode's chunk pool is shared and sized for the wrong path, but it is not the collapse.**
`bc_chunk_pool` is `BC_CHUNK_POOL_MAX 4` entries, added for the CPU path where a chunk is one
buffer. A GPU chunk is three (dst + src + params) and `bc_chunk_gpu_new` only reuses a pooled entry
when it is at least `BC_GPU_CHUNK_MIN` (8 MiB) in **both** dst and src plus `BC_GPU_PARAMS_MIN`
(512 KiB) of params, so a 4-entry pool cannot serve the ~6 chunks a frame needs and the buffers are
re-allocated per command buffer. The host accounting looks dramatic -- `dma-heap` 628 allocations /
6311 MiB in one 10 s window, 15.0 GiB cumulative over a session, `vkAllocateMemory` 616 calls /
1567 ms with a 149 ms worst call -- but **that is the level load, not the collapse**. Read
`vkAllocateMemory` per game-ring window across `10-30-02`: 250x/737 ms, 616x/1567 ms, 200x/512 ms in
windows 4-6, then **0x for eleven straight windows** (11-25) while the game sits at 3-8 fps. The
churn stops and the collapse continues, so the pool is a load-time cost and not the sustained one.
Widening it is still worth doing for load time, but it will not fix the stutter.

**What the host spends its time on during the collapse is descriptor churn.** Per 10 s game-ring
window in `10-30-02`, from window 8 on: `vkAllocateDescriptorSets` 130k-178k calls / 445-552 ms and
`vkUpdateDescriptorSets` 97k-138k calls / 431-546 ms -- about **1 s of every 10 s window**, the two
largest sustained entries, ahead of `vkExecuteCommandStreamsMESA` (540-880 calls / 700-1320 ms) and
`vkWaitSemaphores` (780-1280 / 85-142 ms). This is not specific to the GPU decode: `10-27-09` (CPU
decode) has a 147k-call window too. It is DXVK re-creating descriptor sets per frame, and it is the
same population the 0023 fallback exists for -- that fallback does fire (0 lines in the three
earliest bundles, 3/11/15/1 in the later ones), so the guest's shadow accounting is genuinely
falling out of sync and every set from then on is a synchronous round trip.

**The honest summary.** The collapse is a guest that spends most of each window blocked on its own
submits (`semaphore` 2-5 s of every 10 s, per the guest's own wait report) while the host churns
descriptor sets for ~10% of the window. The BC decode tracks it but does not cause it: the same
collapse is present at the same rate with the decode on the CPU. Three things are worth separating
next, in this order, because the first two need no rebuild:
1. `DROIDDECK_VN_EMULATE_BC=0` from the game card's **Environment > Edit variables** field (which
   `droiddeck-game-env`'s `apply_config` applies over the inherited environment, so it beats the
   driver's `env`). This is the one test that settles decode-or-not, and it has still never been run.
2. The descriptor churn: the next driver change to try is making the 0023 deferral sticky for the
   whole pool (or fixing the accounting) so the host is not handed 130k sets a window.
3. The 30.0 fps plateau, which *is* GPU-decode-specific and is the one thing that would explain a
   report of framepacing rather than throughput.

### The wait report had no owner for its biggest number (0025)

`0017` attributes call stacks to waits, but its gate took only `VN_RELAX_REASON_FENCE` and
`VN_RELAX_REASON_RING_SEQNO`. Summed over every bundle's Lake windows, **semaphore leads in all six
sessions (49-72% of the wait total)** and was the one reason that could never reach the stack list.
So the report showed `semaphore` as the largest line in its per-reason totals while its "wait stack
N" lines were all fence and ring, and the biggest number in the report read as if it had no owner --
or worse, as if the attributed stacks were the whole story. `ring space` and `tls ring seqno` were
excluded the same way. `0025` replaces the two-reason test with `VN_DD_REASON_ATTRIBUTED()` over
fence, ring seqno, TLS ring seqno, ring space and semaphore, and gives each its own label through
`vn_dd_reason_str()` instead of the fence-else-ring binary. The totals already prove the waits are
real; the point of the change is that the *stacks* for them were never collected, and a stack is
what says whether the semaphore time is presentation pacing (gamescope WSI) or the guest's own
submit pipeline -- the distinction the next fix depends on.

Note when reading those totals: the same window line is duplicated across the `steam` and `server`
logs of one bundle, so per-bundle sums double-count. Compare per-session, and only across
`Lake.exe] waits over` lines.

## The 2026-10-30-04 bundle: 0025 landed, and the wait now has an owner

`dd08fc6f87` ran and the stutter is unchanged (15 of 28 Lake windows under 10 fps, 54% -- the same
43-54% band as every bundle since 10-26, so the collapse predates all of 0021-0025). What changed is
the report: `0025` produced **25 semaphore wait stacks** where there were none, and 8 of them are the
game's own.

**The dominant sustained cost is now attributed and it is not the decode.** In the game process the
waits are 50-73% of wall in collapsed windows, and semaphore is the largest bucket in every one:

```
20:03:21  waits=1091  7316 ms of 10 s (73% wall)  semaphore=5436 ring seqno=1534 ring space=325
20:02:21  waits=10962 6552 ms of 10 s (66% wall)  semaphore=5058 ring seqno=1272
12:40:00  waits=700   6727 ms of 10 s (67% wall)  semaphore=4572 ring seqno=1647
```

Every game semaphore stack is the **same stack**, symbolized against the exact `dd08fc6f87`
`libvulkan_panvk.so` (whose SONAME is `libvulkan_virtio.so` -- `libvulkan_freedreno.so` is just the
file name it is installed under, which is why the frames read `freedreno`):

```
libvulkan_freedreno.so+0x9b1a0 -> vn_WaitSemaphores +592
win32u.so+0x139b00 -> winevulkan.so+0x1fe38 -> ntdll.so+0x428e4 -> ...
```

`win32u` is Wine's present path, so this is DXVK's present/frame-latency throttle sitting in
`vkWaitSemaphores`, not a game logic wait. The same symbol table resolves the other stacks:
`vn_ring_wait_seqno` under `wsi_common_queue_present`, and `vn_WaitForFences`.

**The GPU is not the bottleneck.** The host ring is busy only **2.2-2.9 s of every 10 s** while the
game waits 73% of wall. A semaphore that takes 80 ms/frame to signal, on a queue that is three
quarters idle, is a *feedback/pacing* problem, not a throughput one -- the guest is waiting for a
signal the host is not late in producing. The knobs for that path already exist
(`no_semaphore_feedback`, `no_fence_feedback`, `no_event_feedback`).

**What the collapse looks like on the host**, from the same ring, healthy vs collapsed windows:

| op | healthy (/window) | collapsed (/window) |
|---|---|---|
| `vkAllocateDescriptorSets` | 7,807 | **69,034** |
| `vkUpdateDescriptorSets` | 4,497 | **49,755** |
| `vkCmdUpdateBuffer` | 0 | **10,244** |
| `vkCmdBeginRendering` | 300 | **0** |

So in the collapse the game stops drawing and starts churning -- ~4,000 descriptor sets allocated
*per draw*, plus 10k buffer updates a window. `corr(ring busy, vkAllocateDescriptorSets) = +0.79`.
`vkCmdUpdateBuffer` and `vkResetDescriptorPool` appear *only* in collapsed windows.

**A real bug alongside it: a descriptor retry storm.** The guest logs **1,480
`VK_ERROR_OUT_OF_POOL_MEMORY`** from `vn_AllocateDescriptorSets` in this one bundle (9,630 in
10-29-07), and `0023` fired to defer to the real pool 3 times. DXVK retries a pool-full answer, so
each false one is a reset/recreate loop. This is not the collapse -- it is present in 10-26-12 with
2,501 errors and `0023` not yet written -- but it is a real defect on its own.

**Do not read the BC decode as cleared.** `462a7d1` said to clear it; the new bundle has the game
decoding **up to 2.5 GB per 10 s window, 4,396 decodes on the GPU, at 6.6 fps**. It is real load.
But it is not the collapse either: in `10-29-02` the decode line is *constant* at 106 images /
255 ms while fps swings 50 -> 3 within the same session. Decode is a load the collapse makes worse;
it is not what starts it.

**Reading the totals.** The same window line is duplicated across the `steam` and `server` logs of
one bundle, so per-bundle sums double-count. Compare per-session, and only across `Lake.exe] waits
over` lines.

## 0026: the BC decode flushed the tiler twice per upload

The wait is attributed (0025) and it is the game's own present path, but the cost it waits on is
the GPU, not the host: the ring is idle three quarters of the time while the frame waits. The one
thing in this driver that puts heavy, texture-shaped work on the GPU is the BC decode (0024), and
the per-format line 0024 added says where it lands:

```
BC textures by format: BC1 205/234M, BC3 88/130M, BC7 7/4M
BC textures by format: BC1 217/224M, BC3 205/178M, BC7 16/14M
```

BC1 and BC3 are the whole decode; BC7 is ~2%.  Both are S3TC, so they take the GPU path
(`bc_gpu_kind` returns 0-3) and BC7 does not by default.

**The defect.** `bc_gpu_copy` ordered its dispatches with
`VK_PIPELINE_STAGE_ALL_COMMANDS_BIT -> ALL_COMMANDS`, twice per upload, with
`VK_ACCESS_MEMORY_WRITE|HOST_WRITE -> MEMORY_READ|MEMORY_WRITE`. On a tile-based GPU a dependency
whose *source* scope includes the vertex/tiling stages forces the tiler to finish and flush before
the dependency is satisfied. The decode touches only three buffers -- the compressed source and
parameters, written by host and transfer and read by the shader; the decoded destination, written
by the shader and read by the caller's transfer -- so none of that scope was needed. 1,000-4,500
uploads in a single 10 s window, two barriers each.

**The fix.** `bc_gpu_barrier_before_dispatch` is TRANSFER|HOST -> COMPUTE_SHADER with
TRANSFER_WRITE|HOST_WRITE -> SHADER_READ; `bc_gpu_barrier_after_dispatch` is COMPUTE_SHADER ->
TRANSFER with SHADER_WRITE -> TRANSFER_READ. `DROIDDECK_VN_BC_COARSE_BARRIER=1` restores the old
barrier so the change can be A/B'd on a device -- a fix that cannot be turned off cannot be shown
to be the cause.

**Also checked and rejected, so the next session does not redo it.** The host driver has no native
BC (`android-vulkan.txt` reports `BC1=0x0 BC3=0x0 BC5=0x0 BC7=0x0`), so the decode cannot be
removed by advertising the formats. The 1,480 `VK_ERROR_OUT_OF_POOL_MEMORY` are **not** 0023's
false positives: they occur *after* 0023 flips the pool to synchronous real-driver mode, so they
are genuine exhaustion, and `vkResetDescriptorPool` appears only in collapsed windows -- a real
bug, but `corr(fps, allocDescriptorSets) = +0.08`, so not this collapse. Descriptor churn is a
consequence of the stall, not the cause.

**Corrections to earlier notes.** `462a7d1` said the BC decode was cleared: it is not. My first
pass in the previous entry said decode is *constant* while fps swings -- that came from pairing
each fps line with the nearest preceding BC line, which repeats one BC line across several fps
readings. Aligned properly against the host ring in `app.log`, `corr(fps, bcDecodeMs) = -0.69` and
`corr(fps, allocDescriptorSets) = +0.08`. Decode is the cost; descriptor churn is not.

**Do not read `vkCmdBeginRendering: 0` in a collapsed window as "the game stopped drawing."** The
guest batches command buffers, so whole batches of rendering are absent from the host's per-window
count; `vkExecuteCommandStreamsMESA` keeps climbing (300 -> 893) while `vkCmdBeginRendering` reads
zero. Counts of draw-level commands are only comparable within the same batching behaviour.

## 0027: the 2026-10-30-04 bundle re-read -- round trips, not the GPU and not the semaphore

The previous entry read this bundle as "the game waits 73% of wall on semaphores, so the cost is
the GPU". Three things in the same bundle say otherwise, and they are what 0027 acts on.

**The semaphore bucket is DXVK's background thread, not the frame.** The healthy menu window
(20:02:21, 53 fps on screen) has `semaphore 5451x 5057.5 ms` -- more than any collapsed window.
Every semaphore stack is the same single call from winevulkan (`vn_WaitSemaphores`), i.e. DXVK's
submission-finisher thread, which spends its life waiting for the GPU by design. Summed wait time
across threads is not frame time. 0027 puts the thread name in every wait stack
(`in thread '...'`) so the frame's own thread can be told from the finisher.

**The steady state was never in the guest's log.** `droiddeck_vulkan.txt` stops at 600 lines per
process, and 492 of Lake's were one `vn_AllocateDescriptorSets: VK_ERROR_OUT_OF_POOL_MEMORY` line
per pool DXVK filled, so the file ends at 20:03:21, 80 s into the game. Everything after it --
the 20:03:30-20:05:40 steady state at 8-13 fps -- is only in the server's ring lines in `app.log`.
0027 drops the pool-full lines from the file (they are counted instead) and raises the cap to 4000.

**What the server shows in that steady state** (ring `206160587136`, per 10 s, 20:03:30-20:05:40):
`vkAllocateDescriptorSets` 150-215k, `vkUpdateDescriptorSets` 115-165k, `vkResetDescriptorPool`
440-660, and **430-690 `vkAllocateDescriptorSets failed` warnings** -- one per reset, ~55 a frame.
That is 0023 at work: when the guest's accounting says a pool is full, 0023 makes the allocation a
*synchronous* call to the real pool, the real pool refuses it too (the warnings), and only then
does DXVK move on. A synchronous call returns only after the server has run everything queued
before it, so each of the ~55 per frame parks the recording thread until the server catches up:
guest and server CPU work in series instead of overlapped. The load-time stacks show the cost
directly: `vn_AllocateDescriptorSets -> vn_call -> ring wait`, 8x 267 ms with a 215 ms worst.
The guest's accounting was right every time the real pool was asked, so upstream's answer --
refuse on the spot -- is correct. 0027 restores it; `DROIDDECK_VN_POOL_DEFER=1` brings 0023 back
for an A/B, and the new `descriptor pools over 10 s:` line counts both outcomes.

**The GPU is not shown to be saturated, either.** The compositor's own composite in the steady
state waits 0.45-0.9 ms on its fence (`fence wait 0.45/1.74` at 20:04:11), which it would not if
the game kept the Mali busy for 100 ms a frame. That is not proof (Mali time-slices contexts), so
0027 measures it: timestamps around every submission, reported as `gpu over 10 s: busy X ms (Y%)`
(the union of the spans, so overlapping submissions are not double-counted), plus a
`cpu over 10 s:` line naming this program's busiest threads. Next bundle: if `gpu busy` is well
under the frame time and one game thread is near 100%, the remaining cost is CPU (DXVK under FEX
or the driver's encoding); if `gpu busy` is near 100%, it is the GPU.

**The BC decode's "device-local" buffers were dma-heap memory.** `bc_buffer_new` took the first
device-local type, and on this device that is type 0 (`DEVICE_LOCAL | HOST_VISIBLE | COHERENT |
CACHED`), which the server allocates from the dma-heap, zeroes and maps. With a 4-entry pool and
exact per-copy sizes most chunks were new, so a level load allocated 6.5 GB of dma-heap in 10 s
(`vkAllocateMemory 617x 1683.8 ms (max 85.8)`, every call blocking the ring the game's commands
queue behind). 0027 takes a device-local type that is not host-visible, searched from the last
type (the server's native alias, 0009), rounds GPU chunk sizes to a power of two, and pools up to
32 chunks within `DROIDDECK_VN_BC_POOL_MB` (384). A `BC staging chunks:` line reports reused /
allocated / freed. `DROIDDECK_VN_BC_NATIVE=0` restores the old type for an A/B. Lake's own texture
memory is DXVK's choice (type 0 as well) and cannot be moved from the guest without a server change.

**Wait stacks are symbolized in-process now.** The library keeps `.symtab`; 0027 reads it once and
prints `function+offset` for the driver's own frames, so a stack no longer needs the exact CI
artifact to read (the artifacts are not downloadable from these sessions, and the release asset is
overwritten by the next build -- offsets symbolized against a later build are wrong).

CI (`run-venus.sh`): `vkstats` fills a 4-set pool 50 times and checks every fifth allocation is
refused with no round trip (and that `DROIDDECK_VN_POOL_DEFER=1` still asks the real pool), that
GPU busy is measured, that this program's threads are reported, and that wait stacks name the
thread and the driver's functions; `bc_native_case` runs `vkbc` against the phone's memory types
and checks the decode allocates native memory (and that `DROIDDECK_VN_BC_NATIVE=0` does not).
