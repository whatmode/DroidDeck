/* Test-only Vulkan layer: lavapipe reports itself as an integrated GPU. Zink skips CPU devices
 * unless software GL is asked for, and asking for it (LIBGL_ALWAYS_SOFTWARE) also changes the
 * GLX path. The phone's bridge device is a GPU, so this lets CI take the same path: DRI3 first,
 * then Kopper (or nothing). */
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkGetPhysicalDeviceProperties next_props;
static PFN_vkGetPhysicalDeviceProperties2 next_props2;

static void fix(VkPhysicalDeviceProperties *p) {
  if (p->deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) p->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
}

static VKAPI_ATTR void VKAPI_CALL props(VkPhysicalDevice pd, VkPhysicalDeviceProperties *p) {
  next_props(pd, p);
  fix(p);
}

static VKAPI_ATTR void VKAPI_CALL props2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *p) {
  next_props2(pd, p);
  fix(&p->properties);
}

static VKAPI_ATTR VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *out) {
  VkLayerInstanceCreateInfo *li = (VkLayerInstanceCreateInfo *)ci->pNext;
  while (li && !(li->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && li->function == VK_LAYER_LINK_INFO))
    li = (VkLayerInstanceCreateInfo *)li->pNext;
  if (!li) return VK_ERROR_INITIALIZATION_FAILED;
  next_gipa = li->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  li->u.pLayerInfo = li->u.pLayerInfo->pNext;
  PFN_vkCreateInstance next = (PFN_vkCreateInstance)next_gipa(NULL, "vkCreateInstance");
  VkResult r = next(ci, a, out);
  if (r == VK_SUCCESS) {
    next_props = (PFN_vkGetPhysicalDeviceProperties)next_gipa(*out, "vkGetPhysicalDeviceProperties");
    next_props2 = (PFN_vkGetPhysicalDeviceProperties2)next_gipa(*out, "vkGetPhysicalDeviceProperties2");
    if (!next_props2) next_props2 = (PFN_vkGetPhysicalDeviceProperties2)next_gipa(*out, "vkGetPhysicalDeviceProperties2KHR");
  }
  return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out) {
  VkLayerDeviceCreateInfo *li = (VkLayerDeviceCreateInfo *)ci->pNext;
  while (li && !(li->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && li->function == VK_LAYER_LINK_INFO))
    li = (VkLayerDeviceCreateInfo *)li->pNext;
  if (!li) return VK_ERROR_INITIALIZATION_FAILED;
  PFN_vkGetInstanceProcAddr gipa = li->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  next_gdpa = li->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  li->u.pLayerInfo = li->u.pLayerInfo->pNext;
  PFN_vkCreateDevice next = (PFN_vkCreateDevice)gipa(NULL, "vkCreateDevice");
  return next(pd, ci, a, out);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gputype_GetDeviceProcAddr(VkDevice d, const char *n) {
  return next_gdpa ? next_gdpa(d, n) : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gputype_GetInstanceProcAddr(VkInstance i, const char *n) {
  if (!strcmp(n, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)gputype_GetInstanceProcAddr;
  if (!strcmp(n, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)gputype_GetDeviceProcAddr;
  if (!strcmp(n, "vkCreateInstance")) return (PFN_vkVoidFunction)create_instance;
  if (!strcmp(n, "vkCreateDevice")) return (PFN_vkVoidFunction)create_device;
  if (!strcmp(n, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)props;
  if (!strcmp(n, "vkGetPhysicalDeviceProperties2") || !strcmp(n, "vkGetPhysicalDeviceProperties2KHR")) return (PFN_vkVoidFunction)props2;
  return next_gipa ? next_gipa(i, n) : NULL;
}
