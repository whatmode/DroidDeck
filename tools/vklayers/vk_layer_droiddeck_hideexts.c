/*
 * VK_LAYER_DROIDDECK_hideexts: diagnostic extension bisection for PanVK/DXVK.
 *
 * DROIDDECK_HIDE_EXTS is a comma-separated list of device extension names. The
 * layer removes those names from vkEnumerateDeviceExtensionProperties and
 * clears their VkPhysicalDeviceFeatures2 feature structs when present. This
 * makes DXVK take its normal extension-missing fallback path without changing
 * the driver or the game.
 */
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_OBJS 64
#define FEATURE_HEADER_SIZE 16u

struct inst {
    VkInstance handle;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance destroy;
};
struct dev {
    VkDevice handle;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkDestroyDevice destroy;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct inst g_inst[MAX_OBJS];
static struct dev g_dev[MAX_OBJS];
static struct inst *g_active;
static int g_reported;

static void *dispatch_key(const void *obj)
{
    return *(void *const *)obj;
}

static struct inst *inst_for(const void *obj)
{
    struct inst *result = NULL;
    if (!obj)
        return NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++) {
        if (g_inst[i].handle && dispatch_key(g_inst[i].handle) == dispatch_key(obj)) {
            result = &g_inst[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return result;
}

/*
 * A physical device is not an instance: loaders are free to give it a dispatch key of its own, so
 * inst_for() legitimately misses for every vkGetPhysicalDevice* and vkCreateDevice call. Those
 * hooks still need an instance to reach the next layer through, so fall back to the instance that
 * was created last. Without this the hooks resolve no next function and fail the call, which stops
 * the application from starting at all.
 */
static struct inst *instance_for(const void *obj)
{
    struct inst *result = inst_for(obj);
    if (result)
        return result;
    pthread_mutex_lock(&g_lock);
    result = g_active;
    pthread_mutex_unlock(&g_lock);
    return result;
}

static struct dev *dev_for(const void *obj)
{
    struct dev *result = NULL;
    if (!obj)
        return NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++) {
        if (g_dev[i].handle && dispatch_key(g_dev[i].handle) == dispatch_key(obj)) {
            result = &g_dev[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return result;
}

static int token_equals(const char *start, size_t length, const char *name)
{
    const char *end;
    while (length && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
        length--;
    }
    end = start + length;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    return strlen(name) == (size_t)(end - start) && !memcmp(start, name, (size_t)(end - start));
}

/*
 * Extensions DXVK 3.1.1 treats as required (ENABLE_EXT/ENABLE_EXT_FEATURE with require=true):
 * hiding one makes DXVK reject the device, so the game never starts and the launch says nothing
 * about the rendering bug. They are never hidden, which keeps every bisection launch meaningful.
 */
static const char *const dxvk_required_extensions[] = {
    "VK_KHR_load_store_op_none",
    "VK_KHR_maintenance5",
    "VK_KHR_maintenance6",
    "VK_EXT_robustness2",
};

static int dxvk_required_index(const char *name)
{
    for (size_t i = 0; i < sizeof(dxvk_required_extensions) / sizeof(dxvk_required_extensions[0]); i++) {
        if (!strcmp(dxvk_required_extensions[i], name))
            return (int)i;
    }
    return -1;
}

static void report_required(const char *name)
{
    static unsigned reported;
    int index = dxvk_required_index(name);
    if (index < 0 || (reported & (1u << index)))
        return;
    reported |= 1u << index;
    fprintf(stderr, "VK_LAYER_DROIDDECK_hideexts: keeping %s: DXVK requires it, hiding it would stop the "
            "game from starting instead of testing rendering\n", name);
}

static int hides_extension(const char *name)
{
    const char *list = getenv("DROIDDECK_HIDE_EXTS");
    if (!list || !*list || !strcmp(list, "0"))
        return 0;
    const char *p = list;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t length = comma ? (size_t)(comma - p) : strlen(p);
        if (token_equals(p, length, name)) {
            if (dxvk_required_index(name) >= 0) {
                report_required(name);
                return 0;
            }
            return 1;
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return 0;
}

static void report_hidden(const char *name)
{
    if (g_reported)
        return;
    g_reported = 1;
    fprintf(stderr, "VK_LAYER_DROIDDECK_hideexts: hiding extensions from DROIDDECK_HIDE_EXTS=%s (first: %s)\n",
            getenv("DROIDDECK_HIDE_EXTS") ?: "", name);
}

/*
 * These values and field counts are stable Vulkan registry definitions. Using
 * numeric sType values keeps this diagnostic layer buildable with older NDK
 * Vulkan headers while still handling extensions introduced after them.
 */
struct feature_shape {
    VkStructureType sType;
    unsigned bool_count;
    const char *extension;
};

static const struct feature_shape feature_shapes[] = {
    { (VkStructureType)1000455000, 31, "VK_EXT_extended_dynamic_state3" },
    { (VkStructureType)1000470000, 1,  "VK_KHR_maintenance5" },
    { (VkStructureType)1000545000, 1,  "VK_KHR_maintenance6" },
    { (VkStructureType)1000574000, 1,  "VK_KHR_maintenance8" },
    { (VkStructureType)1000584000, 1,  "VK_KHR_maintenance9" },
    { (VkStructureType)1000528000, 1,  "VK_KHR_shader_float_controls2" },
    { (VkStructureType)1000232000, 1,  "VK_KHR_dynamic_rendering_local_read" },
    { (VkStructureType)1000387000, 1,  "VK_KHR_shader_untyped_pointers" },
    { (VkStructureType)1000527000, 2,  "VK_KHR_unified_image_layouts" },
    { (VkStructureType)1000339000, 1,  "VK_EXT_attachment_feedback_loop_layout" },
    { (VkStructureType)1000286000, 3,  "VK_EXT_robustness2" },
    { (VkStructureType)1000028000, 2,  "VK_EXT_transform_feedback" },
    { (VkStructureType)1000392000, 1,  "VK_EXT_multi_draw" },
    { (VkStructureType)1000283000, 4,  "VK_EXT_depth_bias_control" },
    { (VkStructureType)1000422000, 1,  "VK_EXT_non_seamless_cube_map" },
    { (VkStructureType)1000287002, 2,  "VK_EXT_custom_border_color" },
};

static void mask_feature_chain(VkPhysicalDeviceFeatures2 *features)
{
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)features->pNext; s; s = s->pNext) {
        for (size_t i = 0; i < sizeof(feature_shapes) / sizeof(feature_shapes[0]); i++) {
            const struct feature_shape *shape = &feature_shapes[i];
            if (s->sType != shape->sType || !hides_extension(shape->extension))
                continue;
            memset((char *)s + FEATURE_HEADER_SIZE, 0, shape->bool_count * sizeof(VkBool32));
            report_hidden(shape->extension);
            break;
        }
    }
}

static PFN_vkVoidFunction next_instance_proc(const void *obj, const char *name)
{
    struct inst *in = instance_for(obj);
    return in ? in->gipa(in->handle, name) : NULL;
}

#define NEXT_I(pd, fn) ((PFN_vk##fn)next_instance_proc(pd, "vk" #fn))

static VkResult VKAPI_CALL enumerate_device_extensions(VkPhysicalDevice physical_device,
        const char *layer_name, uint32_t *property_count, VkExtensionProperties *properties)
{
    PFN_vkEnumerateDeviceExtensionProperties next = NEXT_I(physical_device, EnumerateDeviceExtensionProperties);
    if (!next || !property_count)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (layer_name || !getenv("DROIDDECK_HIDE_EXTS") || !*getenv("DROIDDECK_HIDE_EXTS") ||
            !strcmp(getenv("DROIDDECK_HIDE_EXTS"), "0"))
        return next(physical_device, layer_name, property_count, properties);

    uint32_t count = 0;
    VkResult result = next(physical_device, layer_name, &count, NULL);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        return result;
    VkExtensionProperties *all = count ? calloc(count, sizeof(*all)) : NULL;
    if (count && !all)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t fetched = count;
    result = next(physical_device, layer_name, &fetched, all);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        free(all);
        return result;
    }

    uint32_t kept = 0;
    for (uint32_t i = 0; i < fetched; i++) {
        if (hides_extension(all[i].extensionName)) {
            report_hidden(all[i].extensionName);
            continue;
        }
        if (properties && kept < *property_count)
            properties[kept] = all[i];
        kept++;
    }
    uint32_t capacity = *property_count;
    *property_count = properties && capacity < kept ? capacity : kept;
    free(all);
    return properties && capacity < kept ? VK_INCOMPLETE : VK_SUCCESS;
}

static void VKAPI_CALL get_physical_device_features2(VkPhysicalDevice physical_device,
        VkPhysicalDeviceFeatures2 *features)
{
    PFN_vkGetPhysicalDeviceFeatures2 next = NEXT_I(physical_device, GetPhysicalDeviceFeatures2);
    if (!next)
        next = (PFN_vkGetPhysicalDeviceFeatures2)NEXT_I(physical_device, GetPhysicalDeviceFeatures2KHR);
    if (next)
        next(physical_device, features);
    if (features)
        mask_feature_chain(features);
}

static void VKAPI_CALL get_physical_device_features2_khr(VkPhysicalDevice physical_device,
        VkPhysicalDeviceFeatures2 *features)
{
    PFN_vkGetPhysicalDeviceFeatures2KHR next = NEXT_I(physical_device, GetPhysicalDeviceFeatures2KHR);
    if (!next)
        next = (PFN_vkGetPhysicalDeviceFeatures2KHR)NEXT_I(physical_device, GetPhysicalDeviceFeatures2);
    if (next)
        next(physical_device, features);
    if (features)
        mask_feature_chain(features);
}

static VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo *create_info,
        const VkAllocationCallbacks *allocator, VkInstance *instance)
{
    VkLayerInstanceCreateInfo *chain = (VkLayerInstanceCreateInfo *)create_info->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    if (!chain)
        return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance next_create = (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!next_create)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = next_create(create_info, allocator, instance);
    if (result != VK_SUCCESS)
        return result;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++) {
        if (!g_inst[i].handle) {
            g_inst[i] = (struct inst){*instance, next_gipa,
                (PFN_vkDestroyInstance)next_gipa(*instance, "vkDestroyInstance")};
            g_active = &g_inst[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return VK_SUCCESS;
}

static void VKAPI_CALL destroy_instance(VkInstance instance, const VkAllocationCallbacks *allocator)
{
    struct inst *in = inst_for(instance);
    if (!in)
        return;
    PFN_vkDestroyInstance destroy = in->destroy;
    pthread_mutex_lock(&g_lock);
    if (g_active == in)
        g_active = NULL;
    memset(in, 0, sizeof(*in));
    pthread_mutex_unlock(&g_lock);
    if (destroy)
        destroy(instance, allocator);
}

static VkResult VKAPI_CALL create_device(VkPhysicalDevice physical_device,
        const VkDeviceCreateInfo *create_info, const VkAllocationCallbacks *allocator, VkDevice *device)
{
    VkLayerDeviceCreateInfo *chain = (VkLayerDeviceCreateInfo *)create_info->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    if (!chain)
        return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    struct inst *in = instance_for(physical_device);
    PFN_vkCreateDevice next_create = (PFN_vkCreateDevice)next_gipa(
        in ? in->handle : VK_NULL_HANDLE, "vkCreateDevice");
    if (!next_create)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = next_create(physical_device, create_info, allocator, device);
    if (result != VK_SUCCESS)
        return result;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++) {
        if (!g_dev[i].handle) {
            g_dev[i] = (struct dev){*device, next_gdpa,
                (PFN_vkDestroyDevice)next_gdpa(*device, "vkDestroyDevice")};
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return VK_SUCCESS;
}

static void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks *allocator)
{
    struct dev *dev = dev_for(device);
    if (!dev)
        return;
    PFN_vkDestroyDevice destroy = dev->destroy;
    pthread_mutex_lock(&g_lock);
    memset(dev, 0, sizeof(*dev));
    pthread_mutex_unlock(&g_lock);
    if (destroy)
        destroy(device, allocator);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExts_GetDeviceProcAddr(
        VkDevice device, const char *name);
EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExts_GetInstanceProcAddr(
        VkInstance instance, const char *name)
{
#define HOOK(str, fn) if (!strcmp(name, str)) return (PFN_vkVoidFunction)fn
    HOOK("vkGetInstanceProcAddr", DroidDeckHideExts_GetInstanceProcAddr);
    HOOK("vkGetDeviceProcAddr", DroidDeckHideExts_GetDeviceProcAddr);
    HOOK("vkCreateInstance", create_instance);
    HOOK("vkDestroyInstance", destroy_instance);
    HOOK("vkCreateDevice", create_device);
    HOOK("vkDestroyDevice", destroy_device);
    HOOK("vkEnumerateDeviceExtensionProperties", enumerate_device_extensions);
    HOOK("vkGetPhysicalDeviceFeatures2", get_physical_device_features2);
    HOOK("vkGetPhysicalDeviceFeatures2KHR", get_physical_device_features2_khr);
#undef HOOK
    if (!instance)
        return NULL;
    struct inst *in = inst_for(instance);
    return in ? in->gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExts_GetDeviceProcAddr(
        VkDevice device, const char *name)
{
    if (!strcmp(name, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)DroidDeckHideExts_GetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)destroy_device;
    struct dev *dev = dev_for(device);
    return dev ? dev->gdpa(device, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL DroidDeckHideExts_NegotiateLoaderLayerInterfaceVersion(
        VkNegotiateLayerInterface *version)
{
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (version->loaderLayerInterfaceVersion > 2)
        version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = DroidDeckHideExts_GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = DroidDeckHideExts_GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
