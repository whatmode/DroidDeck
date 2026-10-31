/*
 * VK_LAYER_DROIDDECK_malimem: keeps GPU-only resources out of CPU-visible memory on Mali/kbase.
 *
 * PanVK on a Mali (kbase) phone reports all three of its memory types DEVICE_LOCAL: plain GPU
 * memory, HOST_VISIBLE|HOST_COHERENT and HOST_VISIBLE|HOST_CACHED. DXVK treats such a device
 * as UMA and may place DEFAULT buffers -- UAVs that shaders use atomics on -- in the coherent
 * type. On Tensor G4 that memory is IO-coherent, and GPU atomics on it raise
 * GPU_SHAREABILITY_FAULT (CSF exception 0x88): the context is lost and the GPU resets, taking
 * the app's compositor and the Android UI with it (Lake, while loading a save).
 *
 * Report the host-visible types as not DEVICE_LOCAL when a pure device-local type exists, so
 * DXVK lays memory out as on a discrete GPU: GPU resources in GPU memory, CPU-visible memory
 * only for staging and dynamic data. Loaded only while DROIDDECK_MALI_MEMORY=1
 * (droiddeck-game-env sets it for games on Mali).
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

static void rewrite(VkPhysicalDeviceMemoryProperties *p) {
    int pure = 0;
    for (uint32_t i = 0; i < p->memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = p->memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) pure = 1;
    }
    if (!pure) return;
    int changed = 0;
    for (uint32_t i = 0; i < p->memoryTypeCount; i++) {
        VkMemoryPropertyFlags *f = &p->memoryTypes[i].propertyFlags;
        if ((*f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (*f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            *f &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            changed = 1;
        }
    }
    if (changed && !g_said) {
        g_said = 1;
        fprintf(stderr, "VK_LAYER_DROIDDECK_malimem: host-visible memory types reported as not device-local\n");
    }
}

#define NEXT_I(pd, fn) ((PFN_vk##fn)(inst_for(pd) ? inst_for(pd)->gipa(inst_for(pd)->handle, "vk" #fn) : NULL))

static void VKAPI_CALL GetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties *p) {
    PFN_vkGetPhysicalDeviceMemoryProperties next = NEXT_I(pd, GetPhysicalDeviceMemoryProperties);
    if (next) next(pd, p);
    rewrite(p);
}

static void VKAPI_CALL GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2 *p) {
    PFN_vkGetPhysicalDeviceMemoryProperties2 next = NEXT_I(pd, GetPhysicalDeviceMemoryProperties2);
    if (!next) next = (PFN_vkGetPhysicalDeviceMemoryProperties2)NEXT_I(pd, GetPhysicalDeviceMemoryProperties2KHR);
    if (next) next(pd, p);
    rewrite(&p->memoryProperties);
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

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckMaliMem_GetDeviceProcAddr(VkDevice device, const char *name);

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckMaliMem_GetInstanceProcAddr(VkInstance instance, const char *name) {
#define HOOK(str, fn) if (!strcmp(name, str)) return (PFN_vkVoidFunction)fn;
    HOOK("vkGetInstanceProcAddr", DroidDeckMaliMem_GetInstanceProcAddr)
    HOOK("vkGetDeviceProcAddr", DroidDeckMaliMem_GetDeviceProcAddr)
    HOOK("vkCreateInstance", CreateInstance)
    HOOK("vkDestroyInstance", DestroyInstance)
    HOOK("vkCreateDevice", CreateDevice)
    HOOK("vkDestroyDevice", DestroyDevice)
    HOOK("vkGetPhysicalDeviceMemoryProperties", GetPhysicalDeviceMemoryProperties)
    HOOK("vkGetPhysicalDeviceMemoryProperties2", GetPhysicalDeviceMemoryProperties2)
    HOOK("vkGetPhysicalDeviceMemoryProperties2KHR", GetPhysicalDeviceMemoryProperties2)
#undef HOOK
    if (!instance) return NULL;
    struct inst *in = inst_for(instance);
    return in ? in->gipa(instance, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL DroidDeckMaliMem_GetDeviceProcAddr(VkDevice device, const char *name) {
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)DroidDeckMaliMem_GetDeviceProcAddr;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)DestroyDevice;
    struct dev *d = dev_for(device);
    return d ? d->gdpa(device, name) : NULL;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL DroidDeckMaliMem_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v) {
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = DroidDeckMaliMem_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = DroidDeckMaliMem_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
