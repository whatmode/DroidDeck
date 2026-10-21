# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

## PanVK/DXVK white frame (`mali-gl/patches/0002-panvk-csf-compute-tls-size.patch`)

- The shipped ICD is the zenithblue `g615-v11-csf` series at `sources.lock`'s Mesa pin. That pin
  predates upstream `81db71cf`, which makes compute jobs copy the *shared* TLS size into their TSD
  instead of their own stack size. The buggy block appears three times in the CSF tree
  (`cmd_dispatch`, `cmd_precomp`, and DroidDeck's `gpu_prerast_launch_cs_desc`); all three need the
  size word copied, not just the pointer. Upstream's `cf98950a` (`allow_merging_workgroups`) is a
  perf fix and is not the cause.
- Do not "fix" this with DXVK config: `d3d11.zeroWorkgroupMemory` does not exist in DXVK 3.1.1
  (the keys are in `src/d3d11/d3d11_options.cpp`), so setting it does nothing. DXVK's compute
  shaders come from dxbc-spirv, which always emits `OpConstantNull` initializers for LDS variables;
  Mesa then always runs `nir_zero_initialize_shared_memory`, so `VK_KHR_zero_initialize_workgroup_memory`
  is already satisfied on this path.
- `mali-gl/scripts/build.sh` builds GL only (`-Dvulkan-drivers=`). The PanVK ICD is built by
  `tools/panvk/build.sh` (`.github/workflows/panvk-icd-build.yml`) and imported into the app as a
  Linux Vulkan driver zip.
