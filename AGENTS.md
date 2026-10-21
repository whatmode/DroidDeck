# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

## PanVK/DXVK white frame (`mali-gl/patches/0002`-`0005`)

- The shipped ICD is the zenithblue `g615-v11-csf` series at `sources.lock`'s Mesa pin (`5a07217`).
  That pin predates four value-correctness fixes, all ported as `mali-gl/patches/0002`-`0005`:
  - `0002` (`81db71cf`): compute jobs copy the *shared* TLS size into their TSD instead of their own
    stack size. The buggy block appears three times in the CSF tree (`cmd_dispatch`, `cmd_precomp`,
    and DroidDeck's `gpu_prerast_launch_cs_desc`); all three need the size word copied, not just the
    pointer.
  - `0003` (`33e67876`): the register allocator's TLS spills/refills (`bi_load_tl`/`bi_store_tl`)
    dropped the forced-TLS memory-access hint that `43ffcf06` moved into a per-instruction field.
  - `0004` (`45ea3ed8`): TLS stores were not treated as helper-generating in the helper analysis.
  - `0005` (`cf98950a`): `allow_merging_workgroups` was written before `pan_postprocess_nir`, which
    memsets the shader info, so it was always false. This *is* a correctness fix; an earlier note
    here calling it perf-only was wrong.
- Do not "fix" this with DXVK config: `d3d11.zeroWorkgroupMemory` does not exist in DXVK 3.1.1
  (the keys are in `src/d3d11/d3d11_options.cpp`), so setting it does nothing. DXVK's compute
  shaders come from dxbc-spirv, which always emits `OpConstantNull` initializers for LDS variables;
  Mesa then always runs `nir_zero_initialize_shared_memory`, so `VK_KHR_zero_initialize_workgroup_memory`
  is already satisfied on this path.
- `mali-gl/scripts/build.sh` builds GL only (`-Dvulkan-drivers=`). The PanVK ICD is built by
  `tools/panvk/build.sh` (`.github/workflows/panvk-icd-build.yml`) and imported into the app as a
  Linux Vulkan driver zip.
