# DroidDeck Vulkan diagnostic layers

`VK_LAYER_DROIDDECK_noycbcr` is the existing Mali video workaround. It is enabled by `DROIDDECK_HIDE_YCBCR=1`.

`VK_LAYER_DROIDDECK_hideexts` is an opt-in diagnostic layer for PanVK/DXVK extension bisection. Set a comma-separated list in the per-game environment, for example:

```text
DROIDDECK_HIDE_EXTS=VK_KHR_unified_image_layouts,VK_EXT_extended_dynamic_state3
```

`droiddeck-game-env` adds the exact-value marker required by Vulkan implicit-layer manifests and explicitly adds `VK_LAYER_DROIDDECK_hideexts` to `VK_INSTANCE_LAYERS` for Android loaders that do not auto-enable from the marker. The layer then removes matching device extensions from `vkEnumerateDeviceExtensionProperties` and clears matching `VkPhysicalDeviceFeatures2` feature structs. With an empty value or `DROIDDECK_HIDE_EXTS=0`, it is not loaded.

## Extensions that must not be hidden

DXVK 3.1.1 marks these as required (`ENABLE_EXT`/`ENABLE_EXT_FEATURE` with `require = true` in
`src/dxvk/dxvk_device_info.cpp`):

```text
VK_KHR_load_store_op_none
VK_KHR_maintenance5
VK_KHR_maintenance6
VK_EXT_robustness2        (robustBufferAccess2 and nullDescriptor)
```

If one of them is missing, DXVK's `checkDeviceCompatibility()` fails, `DxvkAdapter::isCompatible()`
returns false, no adapter is created, and the game exits during startup instead of rendering. The
layer therefore refuses to hide them and logs `keeping <name>: DXVK requires it ...`. A launch that
dies at startup is not a bisection result, so leave those four out of every round.

## Bisection rounds

Start from the candidates that DXVK enables but does not require, and split them in halves. Each
round hides only its half, so a white result means one of the hidden names is implicated and a
correct result means the culprit is in the other half.

```text
Round 1: VK_EXT_extended_dynamic_state3,VK_KHR_maintenance8,VK_KHR_maintenance9,VK_KHR_shader_float_controls2,VK_KHR_dynamic_rendering_local_read,VK_KHR_shader_untyped_pointers,VK_KHR_unified_image_layouts
Round 2: VK_EXT_attachment_feedback_loop_layout,VK_EXT_transform_feedback,VK_EXT_multi_draw,VK_EXT_depth_bias_control,VK_EXT_non_seamless_cube_map,VK_EXT_custom_border_color
```

Use one controlled launch per hypothesis, record whether Lake is still white, and then bisect the
half that changes the result. This does not alter the PanVK binary and is intended to identify a
driver path before writing a driver patch.
