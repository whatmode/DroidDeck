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

## Reading a log zip: three traps that silently invalidate an A/B run

All three were hit on a real session (2026-10-22-03 for the first two, 2026-10-23-05 for the
third), so check for them before trusting any result.

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
  - Do not run `check-guest.sh` itself for this: with no `pkg-config` it dies in meson's
    `regenerate` step with "Unhandled python OSError", after the mako check has already sent you
    chasing a Python module. Go straight to the stub-`-I` + `compile_commands.json` route.
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
- **The drain is now off by default.** 0021 added `DROIDDECK_VN_SYNC_PRESENT` with the old
  behaviour as the default (a no-op unless set to 0) because correctness had not been shown. The
  on-device numbers settled it: on Lake with FEX defaults the drain was **89% of the frame**
  (present GPU wait 17.6-18.0 ms of a 19.8 ms frame) and the game's own wait report agreed
  (`fence 1558x 5952.8 ms`, `semaphore 1560x 5838.1 ms` in one 10 s window). The default is now
  `false`; `DROIDDECK_VN_SYNC_PRESENT=1` restores the drain, so a title that tears or flickers can
  be A/B'd against the old behaviour without a rebuild.
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
white or garbled frame. `DROIDDECK_VN_SYNC_PRESENT=1` puts the drain back.

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

## The venus session now sets `DROIDDECK_VN_SYNC_PRESENT=1` itself

`SessionService.addClientEnvironment` adds `DROIDDECK_VN_SYNC_PRESENT=1` on the same branch that
adds `BL_VENUS` and `VN_DEBUG`, so a venus session gets the present drain back without any file on
the device. `extraEnv()` runs after that branch, so a line in the user's `droiddeck-env` file still
wins: `DROIDDECK_VN_SYNC_PRESENT=0` returns to the fast path without another APK, and no line at all
now means "drain on" rather than "drain off".

This flips the default that guest patch 0021 chose. 0021 turned the drain off to win the frame time
and left `DROIDDECK_VN_SYNC_PRESENT` as the way back; the white/garbled frames are the symptom that
switch exists for, so the build the user is testing has it on. The env var is still the only thing
that decides it, so the A/B is unchanged -- only the default moved.

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
