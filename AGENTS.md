# Rules

- Comments should be rare, factual, and explain only non-obvious external constraints or business invariants.

## PanVK/DXVK extension bisection (`VK_LAYER_DROIDDECK_hideexts`)

- The layer's own hooks must never fail a call they can pass through. A physical device may carry a
  dispatch key no instance has, so `inst_for()` misses for every `vkGetPhysicalDevice*` and
  `vkCreateDevice`; `instance_for()` falls back to the last created instance. Failing there returns
  `VK_ERROR_INITIALIZATION_FAILED` and the game never starts, which looks like "the hide list broke
  the game" rather than a driver finding.
- DXVK 3.1.1 requires `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`, `VK_KHR_maintenance6` and
  `VK_EXT_robustness2` (`ENABLE_EXT`/`ENABLE_EXT_FEATURE` with `require = true` in
  `src/dxvk/dxvk_device_info.cpp`). Hiding one makes DXVK reject the adapter, so the game exits at
  startup. The layer keeps them visible; do not add them to a bisection round.
- A launch that dies at startup is not a rendering result. Check that the game still reaches the
  white frame before reading anything into a round.
