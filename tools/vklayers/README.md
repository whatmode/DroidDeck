# DroidDeck Vulkan diagnostic layers

`VK_LAYER_DROIDDECK_noycbcr` is the existing Mali video workaround. It is enabled by `DROIDDECK_HIDE_YCBCR=1`.

`VK_LAYER_DROIDDECK_hideexts` is an opt-in diagnostic layer for PanVK/DXVK extension bisection. Set a comma-separated list in the per-game environment, for example:

```text
DROIDDECK_HIDE_EXTS=VK_KHR_unified_image_layouts,VK_EXT_extended_dynamic_state3
```

`droiddeck-game-env` adds the exact-value marker required by Vulkan implicit-layer manifests and explicitly adds `VK_LAYER_DROIDDECK_hideexts` to `VK_INSTANCE_LAYERS` for Android loaders that do not auto-enable from the marker. The layer then removes matching device extensions from `vkEnumerateDeviceExtensionProperties` and clears matching `VkPhysicalDeviceFeatures2` feature structs. With an empty value or `DROIDDECK_HIDE_EXTS=0`, it is not loaded.

Use one controlled launch per hypothesis. Start with halves of the candidate list, record whether Lake is still white, and then bisect the half that changes the result. This does not alter the PanVK binary and is intended to identify a driver path before writing a driver patch.
