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
- `mali-gl/scripts/build.sh` builds GL only (`-Dvulkan-drivers=`). The PanVK ICD is built by
  `tools/panvk/build.sh` (`.github/workflows/panvk-icd-build.yml`) and imported into the app as a
  Linux Vulkan driver zip.

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

- A per-game `null` *cancels* the shared value; it does not inherit it. `apply_config` in
  `droiddeck-game-env` does `result.pop(name, None)` for a null entry, and the per-game map is
  applied after `shared`, so `"games": {"1118240": {"PANVK_DEBUG": null}}` removes a shared
  `PANVK_DEBUG=no_crc`. That is the UI's "unset" action; "restore" is the one that drops the row
  and lets the shared value through. A diagnostic env var set in `shared` is therefore not
  evidence of anything unless the same game has no null row for it.
- The zip's top-level directory is the session, and one session is one launch. A zip with a single
  session dir covers only that launch, whatever the tester says it contains. `device.txt` names the
  selected driver from `meta.json` ("Linux driver") and `session.log` names the imported
  `linux_vulkan_drivers/<id>`; read both to tell the patched ICD from the baseline one.
- `d3d11.zeroWorkgroupMemory` is a no-op in DXVK 3.1.1 (see above), so its presence in a run's
  `DXVK_CONFIG` is a red herring, not a variable under test.
