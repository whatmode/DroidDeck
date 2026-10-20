/*
 * VK_LAYER_DROIDDECK_noycbcr: reports multi-planar / YCbCr image formats (NV12 and friends) as
 * unsupported. On PanVK over kbase, D3D11 NV12 textures that DXVK builds on these formats come
 * out white (Unity's video player: Lake's menu video). Without them, D3D11 reports NV12 as
 * unsupported and games use their ordinary texture path for video frames.
 * Loaded only while DROIDDECK_HIDE_YCBCR=1 (droiddeck-game-env sets it for games on Mali).
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

struct inst { VkInstance handle; PFN_vkGetInstanceProcAddr gipa; PFN_vkDestroyInstance destroy; };
struct dev { VkDevice handle; PFN_vkGetDeviceProcAddr gdpa; PFN_vkDestroyDevice destroy; };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct inst g_inst[MAX_OBJS];
static struct dev g_dev[MAX_OBJS];
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

static int hidden(VkFormat f) {
    int v = (int)f;
    int yes = (v >= 1000156000 && v <= 1000156033) || (v >= 1000330000 && v <= 1000330003);
    if (yes && !g_said) {
        g_said = 1;
        fprintf(stderr, "VK_LAYER_DROIDDECK_noycbcr: reporting YCbCr formats (NV12 ...) as unsupported\n");
    }
    return yes;
}

#define NEXT_I(pd, fn) ((PFN_vk##fn)(inst_for(pd) ? inst_for(pd)->gipa(inst_for(pd)->handle, "vk" #fn) : NULL))

static void VKAPI_CALL GetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, VkFormat f, VkFormatProperties *p) {
    PFN_vkGetPhysicalDeviceFormatProperties next = NEXT_I(pd, GetPhysicalDeviceFormatProperties);
    if (next) next(pd, f, p);
    if (hidden(f)) memset(p, 0, sizeof(*p));
}

static void clear_props2(VkFormatProperties2 *p) {
    memset(&p->formatProperties, 0, sizeof(p->formatProperties));
    for (VkBaseOutStructure *s = (VkBaseOutStructure *)p->pNext; s; s = s->pNext) {
        if (s->sType == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
            VkFormatProperties3 *p3 = (VkFormatProperties3 *)s;
            p3->linearTilingFeatures = p3->optimalTilingFeatures = p3->bufferFeatures = 0;
        } else if (s->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT) {
            ((VkDrmFormatModifierPropertiesListEXT *)s)->drmFormatModifierCount = 0;
        } else if (s->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT) {
            ((VkDrmFormatModifierPropertiesList2EXT *)s)->drmFormatModifierCount = 0;
        }
    }
}

static void VKAPI_CALL GetPhysicalDeviceFormatProperties2(VkPhysicalDevice pd, VkFormat f, VkFormatProperties2 *p) {
    PFN_vkGetPhysicalDeviceFormatProperties2 next = NEXT_I(pd, GetPhysicalDeviceFormatProperties2);
    if (!next) next = (PFN_vkGetPhysicalDeviceFormatProperties2)NEXT_I(pd, GetPhysicalDeviceFormatProperties2KHR);
    if (next) next(pd, f, p);
    if (hidden(f)) clear_props2(p);
}

static VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pd, VkFormat f, VkImageType t,
        VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *p) {
    if (hidden(f)) { memset(p, 0, sizeof(*p)); return VK_ERROR_FORMAT_NOT_SUPPORTED; }
    PFN_vkGetPhysicalDeviceImageFormatProperties next = NEXT_I(pd, GetPhysicalDeviceImageFormatProperties);
    return next ? next(pd, f, t, tiling, usage, flags, p) : VK_ERROR_INITIALIZATION_FAILED;
}

static VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice pd,
        const VkPhysicalDeviceImageFormatInfo2 *info, VkImageFormatProperties2 *p) {
    if (info && hidden(info->format)) {
        memset(&p->imageFormatProperties, 0, sizeof(p->imageFormatProperties));
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    PFN_vkGetPhysicalDeviceImageFormatProperties2 next = NEXT_I(pd, GetPhysicalDeviceImageFormatProperties2);
    if (!next) next = (PFN_vkGetPhysicalDeviceImageFormatProperties2)NEXT_I(pd, GetPhysicalDeviceImageFormatProperties2KHR);
    return next ? next(pd, info, p) : VK_ERROR_INITIALIZATION_FAILED;
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

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckNoYcbcr_GetDeviceProcAddr(VkDevice device, const char *name);

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckNoYcbcr_GetInstanceProcAddr(VkInstance instance, const char *name) {
#define HOOK(str, fn) if (!strcmp(name, str)) return (PFN_vkVoidFunction)fn;
    HOOK("vkGetInstanceProcAddr", DroidDeckNoYcbcr_GetInstanceProcAddr)
    HOOK("vkGetDeviceProcAddr", DroidDeckNoYcbcr_GetDeviceProcAddr)
    HOOK("vkCreateInstance", CreateInstance)
    HOOK("vkDestroyInstance", DestroyInstance)
    HOOK("vkCreateDevice", CreateDevice)
    HOOK("vkDestroyDevice", DestroyDevice)
    HOOK("vkGetPhysicalDeviceFormatProperties", GetPhysicalDeviceFormatProperties)
    HOOK("vkGetPhysicalDeviceFormatProperties2", GetPhysicalDeviceFormatProperties2)
    HOOK("vkGetPhysicalDeviceFormatProperties2KHR", GetPhysicalDeviceFormatProperties2)
    HOOK("vkGetPhysicalDeviceImageFormatProperties", GetPhysicalDeviceImageFormatProperties)
    HOOK("vkGetPhysicalDeviceImageFormatProperties2", GetPhysicalDeviceImageFormatProperties2)
    HOOK("vkGetPhysicalDeviceImageFormatProperties2KHR", GetPhysicalDeviceImageFormatProperties2)
#undef HOOK
    if (!instance) return NULL;
    struct inst *in = inst_for(instance);
    return in ? in->gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckNoYcbcr_GetDeviceProcAddr(VkDevice device, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)DroidDeckNoYcbcr_GetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)DestroyDevice;
    struct dev *d = dev_for(device);
    return d ? d->gdpa(device, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL DroidDeckNoYcbcr_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = DroidDeckNoYcbcr_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = DroidDeckNoYcbcr_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
