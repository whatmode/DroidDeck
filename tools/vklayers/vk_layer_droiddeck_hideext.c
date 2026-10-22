/*
 * VK_LAYER_DROIDDECK_hideext: hides the device extensions named in DROIDDECK_HIDE_EXTS
 * (comma or space separated) from vkEnumerateDeviceExtensionProperties.
 *
 * DXVK gates most of its optional features on whether the extension was enumerated:
 * dxvk_device_info.cpp chainFeature() leaves the feature false unless the extension's
 * specVersion is set, and checkDeviceCompatibility() only fails on the features marked
 * require=true. Hiding an optional extension therefore makes DXVK take its fallback path
 * without the game knowing, which bisects "an extension DXVK uses is broken on PanVK"
 * without touching the game.
 *
 * An extension that supplies a require=true feature cannot be hidden: hiding it fails
 * checkDeviceCompatibility() and the game dies at startup instead of rendering. Those are
 * refused here so a bisection step fails loudly rather than as a launch failure.
 *
 * Loaded only while DROIDDECK_HIDE_EXTS_ENABLE=1; droiddeck-game-env derives that from
 * DROIDDECK_HIDE_EXTS so a tester sets one variable.
 */
#define _GNU_SOURCE
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_OBJS 64
#define MAX_HIDDEN 64

struct inst { VkInstance handle; PFN_vkGetInstanceProcAddr gipa; PFN_vkDestroyInstance destroy; };
struct dev { VkDevice handle; PFN_vkGetDeviceProcAddr gdpa; PFN_vkDestroyDevice destroy; };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct inst g_inst[MAX_OBJS];
static struct dev g_dev[MAX_OBJS];

static char g_hidden[MAX_HIDDEN][VK_MAX_EXTENSION_NAME_SIZE];
static int g_hidden_count = -1;
static int g_said;

static void *key(const void *obj) { return *(void **)obj; }

static struct inst *inst_for(const void *obj) {
    struct inst *r = NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_inst[i].handle && key(g_inst[i].handle) == key(obj)) { r = &g_inst[i]; break; }
    pthread_mutex_unlock(&g_lock);
    return r;
}

static struct dev *dev_for(const void *obj) {
    struct dev *r = NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_dev[i].handle && key(g_dev[i].handle) == key(obj)) { r = &g_dev[i]; break; }
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* Extensions carrying a feature DXVK marks require=true in dxvk_device_info.cpp. Hiding one
 * makes DXVK refuse the device, so a bisection step naming one is a mistake, not a result. */
static int required_by_dxvk(const char *name) {
    static const char *const required[] = {
        "VK_EXT_depth_clip_enable",
        "VK_EXT_robustness2",
        "VK_KHR_load_store_op_none",
        "VK_KHR_maintenance5",
        "VK_KHR_maintenance6",
        "VK_KHR_swapchain",
    };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++)
        if (!strcmp(required[i], name)) return 1;
    return 0;
}

/* Caller holds g_lock. */
static void parse_hidden(void) {
    g_hidden_count = 0;
    const char *value = getenv("DROIDDECK_HIDE_EXTS");
    if (!value || !*value) return;
    char *copy = strdup(value);
    if (!copy) return;
    for (char *tok = strtok(copy, ", \t"); tok && g_hidden_count < MAX_HIDDEN; tok = strtok(NULL, ", \t")) {
        if (required_by_dxvk(tok)) {
            fprintf(stderr, "VK_LAYER_DROIDDECK_hideext: refusing %s: DXVK requires it, the game would not start\n", tok);
            continue;
        }
        snprintf(g_hidden[g_hidden_count], VK_MAX_EXTENSION_NAME_SIZE, "%s", tok);
        g_hidden_count++;
    }
    free(copy);
}

static int is_hidden(const char *name) {
    pthread_mutex_lock(&g_lock);
    if (g_hidden_count < 0) parse_hidden();
    int found = 0;
    for (int i = 0; i < g_hidden_count; i++)
        if (!strcmp(g_hidden[i], name)) { found = 1; break; }
    if (found && !g_said) {
        g_said = 1;
        fprintf(stderr, "VK_LAYER_DROIDDECK_hideext: hiding %d extension(s):", g_hidden_count);
        for (int i = 0; i < g_hidden_count; i++) fprintf(stderr, " %s", g_hidden[i]);
        fprintf(stderr, "\n");
    }
    pthread_mutex_unlock(&g_lock);
    return found;
}

#define NEXT_I(pd, fn) ((PFN_vk##fn)(inst_for(pd) ? inst_for(pd)->gipa(inst_for(pd)->handle, "vk" #fn) : NULL))

static VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *layerName,
        uint32_t *count, VkExtensionProperties *props) {
    PFN_vkEnumerateDeviceExtensionProperties next = NEXT_I(pd, EnumerateDeviceExtensionProperties);
    if (!next) return VK_ERROR_INITIALIZATION_FAILED;
    /* A named layer's own list is not the driver's; only the driver list is filtered. */
    if (layerName) return next(pd, layerName, count, props);
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t n = 0;
    VkResult r = next(pd, NULL, &n, NULL);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return r;
    if (n == 0) { *count = 0; return VK_SUCCESS; }

    VkExtensionProperties *all = calloc(n, sizeof(*all));
    if (!all) return next(pd, NULL, count, props);
    uint32_t got = n;
    r = next(pd, NULL, &got, all);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) { free(all); return r; }

    uint32_t kept = 0;
    for (uint32_t i = 0; i < got; i++) {
        if (is_hidden(all[i].extensionName)) continue;
        if (props && kept < *count) props[kept] = all[i];
        kept++;
    }
    free(all);

    if (!props) { *count = kept; return VK_SUCCESS; }
    if (kept > *count) return VK_INCOMPLETE;
    *count = kept;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *alloc, VkInstance *out) {
    VkLayerInstanceCreateInfo *chain = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance next_create = (PFN_vkCreateInstance)next_gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = next_create(ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++)
        if (!g_inst[i].handle) {
            g_inst[i] = (struct inst){*out, next_gipa, (PFN_vkDestroyInstance)next_gipa(*out, "vkDestroyInstance")};
            break;
        }
    pthread_mutex_unlock(&g_lock);
    return VK_SUCCESS;
}

static void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks *alloc) {
    struct inst *in = inst_for(instance);
    if (!in) return;
    PFN_vkDestroyInstance d = in->destroy;
    pthread_mutex_lock(&g_lock);
    memset(in, 0, sizeof(*in));
    pthread_mutex_unlock(&g_lock);
    if (d) d(instance, alloc);
}

static VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
                                        const VkAllocationCallbacks *alloc, VkDevice *out) {
    VkLayerDeviceCreateInfo *chain = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    struct inst *in = inst_for(pd);
    PFN_vkCreateDevice next_create = (PFN_vkCreateDevice)next_gipa(in ? in->handle : VK_NULL_HANDLE, "vkCreateDevice");
    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = next_create(pd, ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_OBJS; i++)
        if (!g_dev[i].handle) {
            g_dev[i] = (struct dev){*out, next_gdpa, (PFN_vkDestroyDevice)next_gdpa(*out, "vkDestroyDevice")};
            break;
        }
    pthread_mutex_unlock(&g_lock);
    return VK_SUCCESS;
}

static void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks *alloc) {
    struct dev *d = dev_for(device);
    if (!d) return;
    PFN_vkDestroyDevice destroy = d->destroy;
    pthread_mutex_lock(&g_lock);
    memset(d, 0, sizeof(*d));
    pthread_mutex_unlock(&g_lock);
    if (destroy) destroy(device, alloc);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExt_GetDeviceProcAddr(VkDevice device, const char *name);

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExt_GetInstanceProcAddr(VkInstance instance, const char *name) {
#define HOOK(str, fn) if (!strcmp(name, str)) return (PFN_vkVoidFunction)fn;
    HOOK("vkGetInstanceProcAddr", DroidDeckHideExt_GetInstanceProcAddr)
    HOOK("vkGetDeviceProcAddr", DroidDeckHideExt_GetDeviceProcAddr)
    HOOK("vkCreateInstance", CreateInstance)
    HOOK("vkDestroyInstance", DestroyInstance)
    HOOK("vkCreateDevice", CreateDevice)
    HOOK("vkDestroyDevice", DestroyDevice)
    HOOK("vkEnumerateDeviceExtensionProperties", EnumerateDeviceExtensionProperties)
#undef HOOK
    if (!instance) return NULL;
    struct inst *in = inst_for(instance);
    return in ? in->gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckHideExt_GetDeviceProcAddr(VkDevice device, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)DroidDeckHideExt_GetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)DestroyDevice;
    struct dev *d = dev_for(device);
    return d ? d->gdpa(device, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL DroidDeckHideExt_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = DroidDeckHideExt_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = DroidDeckHideExt_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
