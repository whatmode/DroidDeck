/*
 * Harness for VK_LAYER_DROIDDECK_hideexts. The layer clears a hardcoded number of VkBool32
 * fields per feature struct, so this drives the layer's own code with the real Vulkan struct
 * types and checks that exactly the header's field region is cleared: a stale sType leaves a
 * field set, and a stale count either leaves one set or writes past the struct into a canary.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "../vklayers/vk_layer_droiddeck_hideexts.c"

static struct fake_dispatch { void *key; } g_obj;
/* A second object, so the physical device can carry a dispatch key of its own. */
static struct fake_dispatch g_pd_obj;

static const char *g_extensions[] = {
    "VK_KHR_maintenance5",
    "VK_KHR_maintenance6",
    "VK_KHR_load_store_op_none",
    "VK_KHR_swapchain",
    "VK_EXT_robustness2",
    "VK_EXT_depth_clip_enable",
    "VK_KHR_unified_image_layouts",
    "VK_EXT_transform_feedback",
};
#define EXTENSION_COUNT (sizeof(g_extensions) / sizeof(g_extensions[0]))

static VkResult VKAPI_CALL mock_enumerate(VkPhysicalDevice device, const char *layer_name,
        uint32_t *count, VkExtensionProperties *properties)
{
    (void)device;
    (void)layer_name;
    if (!properties) {
        *count = (uint32_t)EXTENSION_COUNT;
        return VK_SUCCESS;
    }
    uint32_t written = *count < EXTENSION_COUNT ? *count : (uint32_t)EXTENSION_COUNT;
    for (uint32_t i = 0; i < written; i++) {
        memset(properties[i].extensionName, 0, sizeof(properties[i].extensionName));
        strncpy(properties[i].extensionName, g_extensions[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
        properties[i].specVersion = 1;
    }
    *count = written;
    return written < EXTENSION_COUNT ? VK_INCOMPLETE : VK_SUCCESS;
}

static void VKAPI_CALL mock_features2(VkPhysicalDevice device, VkPhysicalDeviceFeatures2 *features)
{
    (void)device;
    (void)features;
}

static VkResult VKAPI_CALL mock_create_instance(const VkInstanceCreateInfo *create_info,
        const VkAllocationCallbacks *allocator, VkInstance *instance)
{
    (void)create_info;
    (void)allocator;
    *instance = (VkInstance)&g_obj;
    return VK_SUCCESS;
}

static PFN_vkVoidFunction VKAPI_CALL mock_gipa(VkInstance instance, const char *name)
{
    (void)instance;
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)mock_enumerate;
    if (!strcmp(name, "vkGetPhysicalDeviceFeatures2"))
        return (PFN_vkVoidFunction)mock_features2;
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction)mock_create_instance;
    return NULL;
}

/* Drives the layer's real vkCreateInstance, so the registered instance is set up exactly as a
 * loader would leave it. */
static void register_instance_through_create_instance(void)
{
    VkLayerInstanceLink link;
    memset(&link, 0, sizeof(link));
    link.pfnNextGetInstanceProcAddr = mock_gipa;

    VkLayerInstanceCreateInfo chain;
    memset(&chain, 0, sizeof(chain));
    chain.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    chain.function = VK_LAYER_LINK_INFO;
    chain.u.pLayerInfo = &link;

    VkInstanceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pNext = &chain;
    VkInstance instance = VK_NULL_HANDLE;
    assert(create_instance(&info, NULL, &instance) == VK_SUCCESS);
    assert(instance == (VkInstance)&g_obj);
}

static const struct feature_shape *shape_for(VkStructureType s_type)
{
    for (size_t i = 0; i < sizeof(feature_shapes) / sizeof(feature_shapes[0]); i++)
        if (feature_shapes[i].sType == s_type)
            return &feature_shapes[i];
    return NULL;
}

/* The layer's count must cover every VkBool32 field of the header struct; the only slack the
 * struct can have after the last field is tail padding up to its alignment. */
#define CASE(TYPE, STYPE, FIRST, EXTENSION)                                          \
    do {                                                                             \
        const struct feature_shape *shape = shape_for(STYPE);                         \
        assert(shape && !strcmp(shape->extension, EXTENSION));                        \
        assert(shape->bool_count * sizeof(VkBool32) <=                                \
               sizeof(TYPE) - offsetof(TYPE, FIRST));                                 \
        assert(sizeof(TYPE) - offsetof(TYPE, FIRST) - shape->bool_count * sizeof(VkBool32) \
               < sizeof(void *));                                                     \
        unsigned char buffer[sizeof(TYPE) + 32];                                      \
        memset(buffer, 0xA5, sizeof(buffer));                                         \
        TYPE *feature = (TYPE *)(buffer + 16);                                        \
        memset(feature, 0, sizeof(*feature));                                         \
        feature->sType = STYPE;                                                       \
        memset((char *)feature + offsetof(TYPE, FIRST), 0xFF,                         \
               sizeof(TYPE) - offsetof(TYPE, FIRST));                                 \
        VkPhysicalDeviceFeatures2 chain;                                              \
        memset(&chain, 0, sizeof(chain));                                             \
        chain.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;                   \
        chain.pNext = (void *)feature;                                                \
        get_physical_device_features2((VkPhysicalDevice)&g_obj, &chain);              \
        const unsigned char *fields = (const unsigned char *)feature + offsetof(TYPE, FIRST); \
        for (size_t i = 0; i < shape->bool_count * sizeof(VkBool32); i++)             \
            assert(fields[i] == 0);                                                   \
        for (size_t i = 0; i < 16; i++)                                               \
            assert(buffer[i] == 0xA5);                                                \
        for (size_t i = 16 + sizeof(TYPE); i < sizeof(buffer); i++)                   \
            assert(buffer[i] == 0xA5);                                                \
    } while (0)

static void check_masking(void)
{
    setenv("DROIDDECK_HIDE_EXTS",
           "VK_EXT_extended_dynamic_state3,VK_KHR_maintenance5,VK_KHR_maintenance6,"
           "VK_KHR_maintenance8,VK_KHR_maintenance9,VK_KHR_shader_float_controls2,"
           "VK_KHR_dynamic_rendering_local_read,VK_KHR_shader_untyped_pointers,"
           "VK_KHR_unified_image_layouts,VK_EXT_attachment_feedback_loop_layout,"
           "VK_EXT_robustness2,VK_EXT_transform_feedback,VK_EXT_multi_draw,"
           "VK_EXT_depth_bias_control,VK_EXT_non_seamless_cube_map,"
           "VK_EXT_custom_border_color", 1);
    CASE(VkPhysicalDeviceExtendedDynamicState3FeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
         extendedDynamicState3TessellationDomainOrigin, "VK_EXT_extended_dynamic_state3");
    /* VK_KHR_maintenance5, VK_KHR_maintenance6 and VK_EXT_robustness2 are DXVK-required and must
     * survive even when listed; check_dxvk_required_extensions_are_kept covers them. */
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR
    CASE(VkPhysicalDeviceMaintenance8FeaturesKHR,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR, maintenance8,
         "VK_KHR_maintenance8");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES
    CASE(VkPhysicalDeviceShaderFloatControls2Features,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT_CONTROLS_2_FEATURES, shaderFloatControls2,
         "VK_KHR_shader_float_controls2");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES
    CASE(VkPhysicalDeviceDynamicRenderingLocalReadFeatures,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES,
         dynamicRenderingLocalRead, "VK_KHR_dynamic_rendering_local_read");
#endif
    CASE(VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT,
         attachmentFeedbackLoopLayout, "VK_EXT_attachment_feedback_loop_layout");
    CASE(VkPhysicalDeviceTransformFeedbackFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, transformFeedback,
         "VK_EXT_transform_feedback");
    CASE(VkPhysicalDeviceMultiDrawFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT, multiDraw, "VK_EXT_multi_draw");
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT
    CASE(VkPhysicalDeviceDepthBiasControlFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT, depthBiasControl,
         "VK_EXT_depth_bias_control");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT
    CASE(VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT, nonSeamlessCubeMap,
         "VK_EXT_non_seamless_cube_map");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT
    CASE(VkPhysicalDeviceCustomBorderColorFeaturesEXT,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, customBorderColors,
         "VK_EXT_custom_border_color");
#endif
/* Extensions newer than the Vulkan headers in use still need their registry numbers checked. */
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR
    CASE(VkPhysicalDeviceMaintenance9FeaturesKHR,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR, maintenance9,
         "VK_KHR_maintenance9");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR
    CASE(VkPhysicalDeviceShaderUntypedPointersFeaturesKHR,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR,
         shaderUntypedPointers, "VK_KHR_shader_untyped_pointers");
#endif
#ifdef VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR
    CASE(VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR,
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR, unifiedImageLayouts,
         "VK_KHR_unified_image_layouts");
#endif
}

static void check_unlisted_extension_keeps_its_features(void)
{
    setenv("DROIDDECK_HIDE_EXTS", "VK_EXT_transform_feedback", 1);
    VkPhysicalDeviceRobustness2FeaturesEXT feature;
    memset(&feature, 0, sizeof(feature));
    feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    feature.robustBufferAccess2 = VK_TRUE;
    feature.robustImageAccess2 = VK_TRUE;
    feature.nullDescriptor = VK_TRUE;
    VkPhysicalDeviceFeatures2 chain;
    memset(&chain, 0, sizeof(chain));
    chain.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    chain.pNext = &feature;
    get_physical_device_features2((VkPhysicalDevice)&g_obj, &chain);
    assert(feature.robustBufferAccess2 == VK_TRUE);
    assert(feature.robustImageAccess2 == VK_TRUE);
    assert(feature.nullDescriptor == VK_TRUE);
}

static void check_enumeration_filters_only_listed_names(void)
{
    setenv("DROIDDECK_HIDE_EXTS", "VK_KHR_maintenance5, VK_EXT_transform_feedback", 1);
    uint32_t count = 0;
    assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, NULL, &count, NULL) == VK_SUCCESS);
    assert(count == EXTENSION_COUNT - 1);
    VkExtensionProperties properties[16];
    uint32_t filled = 16;
    assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, NULL, &filled, properties) == VK_SUCCESS);
    assert(filled == EXTENSION_COUNT - 1);
    /* maintenance5 is listed but DXVK-required, so it stays first; transform_feedback is gone. */
    assert(!strcmp(properties[0].extensionName, "VK_KHR_maintenance5"));
    assert(!strcmp(properties[1].extensionName, "VK_KHR_maintenance6"));
    assert(!strcmp(properties[EXTENSION_COUNT - 2].extensionName, "VK_KHR_unified_image_layouts"));

    /* A short buffer is truncated with VK_INCOMPLETE, as the loader expects. */
    filled = 1;
    assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, NULL, &filled, properties) == VK_INCOMPLETE);
    assert(filled == 1);
    assert(!strcmp(properties[0].extensionName, "VK_KHR_maintenance5"));

    /* An explicit layer name asks for the layer's own extensions and passes through. */
    count = 0;
    assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, "VK_LAYER_DROIDDECK_hideexts", &count, NULL) == VK_SUCCESS);
    assert(count == EXTENSION_COUNT);
}

/*
 * DXVK 3.1.1 marks these as required, so hiding one would make DXVK reject the device and the game
 * would not start at all. Listing every one of them must leave the whole list, and the features
 * behind them, untouched.
 */
static void check_dxvk_required_extensions_are_kept(void)
{
    setenv("DROIDDECK_HIDE_EXTS",
           "VK_KHR_load_store_op_none,VK_KHR_maintenance5,VK_KHR_maintenance6,VK_KHR_swapchain,"
           "VK_EXT_robustness2,VK_EXT_depth_clip_enable", 1);

    uint32_t count = 0;
    assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, NULL, &count, NULL) == VK_SUCCESS);
    assert(count == EXTENSION_COUNT);

    VkPhysicalDeviceRobustness2FeaturesEXT feature;
    memset(&feature, 0, sizeof(feature));
    feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    feature.robustBufferAccess2 = VK_TRUE;
    feature.nullDescriptor = VK_TRUE;
    VkPhysicalDeviceFeatures2 chain;
    memset(&chain, 0, sizeof(chain));
    chain.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    chain.pNext = &feature;
    get_physical_device_features2((VkPhysicalDevice)&g_obj, &chain);
    assert(feature.robustBufferAccess2 == VK_TRUE);
    assert(feature.nullDescriptor == VK_TRUE);
}

static void check_unset_or_zero_hides_nothing(void)
{
    const char *values[] = { NULL, "", "0" };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        if (values[i])
            setenv("DROIDDECK_HIDE_EXTS", values[i], 1);
        else
            unsetenv("DROIDDECK_HIDE_EXTS");
        uint32_t count = 0;
        assert(enumerate_device_extensions((VkPhysicalDevice)&g_obj, NULL, &count, NULL) == VK_SUCCESS);
        assert(count == EXTENSION_COUNT);
    }
}

/*
 * A loader may give a physical device a dispatch key that no instance has. The layer must still
 * reach the next layer for vkGetPhysicalDevice* and vkCreateDevice, rather than failing the call:
 * returning VK_ERROR_INITIALIZATION_FAILED here stops the application from starting at all.
 */
static void check_physical_device_with_its_own_dispatch_key(void)
{
    setenv("DROIDDECK_HIDE_EXTS", "VK_EXT_transform_feedback,VK_KHR_unified_image_layouts", 1);
    g_obj.key = &g_obj;
    g_pd_obj.key = &g_pd_obj;
    register_instance_through_create_instance();

    VkPhysicalDevice physical_device = (VkPhysicalDevice)&g_pd_obj;
    assert(dispatch_key(physical_device) != dispatch_key(g_inst[0].handle));

    uint32_t count = 0;
    assert(enumerate_device_extensions(physical_device, NULL, &count, NULL) == VK_SUCCESS);
    assert(count == EXTENSION_COUNT - 2);

    VkPhysicalDeviceTransformFeedbackFeaturesEXT feature;
    memset(&feature, 0, sizeof(feature));
    feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
    feature.transformFeedback = VK_TRUE;
    feature.geometryStreams = VK_TRUE;
    VkPhysicalDeviceFeatures2 chain;
    memset(&chain, 0, sizeof(chain));
    chain.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    chain.pNext = &feature;
    get_physical_device_features2(physical_device, &chain);
    assert(feature.transformFeedback == VK_FALSE);
    assert(feature.geometryStreams == VK_FALSE);
}

int main(void)
{
    g_inst[0].handle = (VkInstance)&g_obj;
    g_inst[0].gipa = mock_gipa;

    check_masking();
    check_unlisted_extension_keeps_its_features();
    check_enumeration_filters_only_listed_names();
    check_dxvk_required_extensions_are_kept();
    check_unset_or_zero_hides_nothing();
    check_physical_device_with_its_own_dispatch_key();
    printf("hideexts layer passed\n");
    return 0;
}
