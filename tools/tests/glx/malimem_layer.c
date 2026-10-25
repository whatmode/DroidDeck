/* Test-only Vulkan layer for the bridge server: lavapipe reports the phone's (Mali-G715's)
 * memory types instead of its own single one, so the server and the guest driver see the
 * phone's layout:
 *   0 device-local | host-visible | coherent      (lavapipe's type)
 *   1 device-local | host-visible | cached        (the venus guest hides host-visible from it)
 *   2 device-local | lazily allocated             (never in a memoryTypeBits here)
 * Allocations of any type are lavapipe's type 0; memoryTypeBits with it get types 0 and 1. */
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkGetPhysicalDeviceMemoryProperties next_mp;
static PFN_vkGetPhysicalDeviceMemoryProperties2 next_mp2;
static PFN_vkAllocateMemory next_alloc;
static PFN_vkGetBufferMemoryRequirements next_bmr;
static PFN_vkGetBufferMemoryRequirements2 next_bmr2;
static PFN_vkGetImageMemoryRequirements next_imr;
static PFN_vkGetImageMemoryRequirements2 next_imr2;
static PFN_vkGetDeviceBufferMemoryRequirements next_dbmr;
static PFN_vkGetDeviceImageMemoryRequirements next_dimr;
static PFN_vkGetMemoryFdPropertiesKHR next_fdp;

static void fake_props(VkPhysicalDeviceMemoryProperties *p) {
  VkMemoryType t0 = p->memoryTypes[0];
  p->memoryTypeCount = 3;
  p->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  p->memoryTypes[0].heapIndex = t0.heapIndex;
  p->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  p->memoryTypes[1].heapIndex = t0.heapIndex;
  p->memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
  p->memoryTypes[2].heapIndex = t0.heapIndex;
}

static uint32_t fake_bits(uint32_t bits) { return (bits & 1u) ? 3u : 0u; }

static VKAPI_ATTR void VKAPI_CALL mp(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties *p) {
  next_mp(pd, p);
  fake_props(p);
}
static VKAPI_ATTR void VKAPI_CALL mp2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2 *p) {
  next_mp2(pd, p);
  fake_props(&p->memoryProperties);
}
static VKAPI_ATTR VkResult VKAPI_CALL alloc(VkDevice d, const VkMemoryAllocateInfo *ai, const VkAllocationCallbacks *a, VkDeviceMemory *m) {
  VkMemoryAllocateInfo copy = *ai;
  copy.memoryTypeIndex = 0;
  return next_alloc(d, &copy, a, m);
}
static VKAPI_ATTR void VKAPI_CALL bmr(VkDevice d, VkBuffer b, VkMemoryRequirements *r) {
  next_bmr(d, b, r);
  r->memoryTypeBits = fake_bits(r->memoryTypeBits);
}
static VKAPI_ATTR void VKAPI_CALL bmr2(VkDevice d, const VkBufferMemoryRequirementsInfo2 *i, VkMemoryRequirements2 *r) {
  next_bmr2(d, i, r);
  r->memoryRequirements.memoryTypeBits = fake_bits(r->memoryRequirements.memoryTypeBits);
}
static VKAPI_ATTR void VKAPI_CALL imr(VkDevice d, VkImage im, VkMemoryRequirements *r) {
  next_imr(d, im, r);
  r->memoryTypeBits = fake_bits(r->memoryTypeBits);
}
static VKAPI_ATTR void VKAPI_CALL imr2(VkDevice d, const VkImageMemoryRequirementsInfo2 *i, VkMemoryRequirements2 *r) {
  next_imr2(d, i, r);
  r->memoryRequirements.memoryTypeBits = fake_bits(r->memoryRequirements.memoryTypeBits);
}
static VKAPI_ATTR void VKAPI_CALL dbmr(VkDevice d, const VkDeviceBufferMemoryRequirements *i, VkMemoryRequirements2 *r) {
  next_dbmr(d, i, r);
  r->memoryRequirements.memoryTypeBits = fake_bits(r->memoryRequirements.memoryTypeBits);
}
static VKAPI_ATTR void VKAPI_CALL dimr(VkDevice d, const VkDeviceImageMemoryRequirements *i, VkMemoryRequirements2 *r) {
  next_dimr(d, i, r);
  r->memoryRequirements.memoryTypeBits = fake_bits(r->memoryRequirements.memoryTypeBits);
}
static VKAPI_ATTR VkResult VKAPI_CALL fdp(VkDevice d, VkExternalMemoryHandleTypeFlagBits t, int fd, VkMemoryFdPropertiesKHR *p) {
  VkResult r = next_fdp(d, t, fd, p);
  if (r == VK_SUCCESS) p->memoryTypeBits = fake_bits(p->memoryTypeBits);
  return r;
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
    next_mp = (PFN_vkGetPhysicalDeviceMemoryProperties)next_gipa(*out, "vkGetPhysicalDeviceMemoryProperties");
    next_mp2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)next_gipa(*out, "vkGetPhysicalDeviceMemoryProperties2");
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
  VkResult r = next(pd, ci, a, out);
  if (r == VK_SUCCESS) {
#define GET(var, type, name) var = (type)next_gdpa(*out, name)
    GET(next_alloc, PFN_vkAllocateMemory, "vkAllocateMemory");
    GET(next_bmr, PFN_vkGetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    GET(next_bmr2, PFN_vkGetBufferMemoryRequirements2, "vkGetBufferMemoryRequirements2");
    GET(next_imr, PFN_vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
    GET(next_imr2, PFN_vkGetImageMemoryRequirements2, "vkGetImageMemoryRequirements2");
    GET(next_dbmr, PFN_vkGetDeviceBufferMemoryRequirements, "vkGetDeviceBufferMemoryRequirements");
    GET(next_dimr, PFN_vkGetDeviceImageMemoryRequirements, "vkGetDeviceImageMemoryRequirements");
    GET(next_fdp, PFN_vkGetMemoryFdPropertiesKHR, "vkGetMemoryFdPropertiesKHR");
  }
  return r;
}

static PFN_vkVoidFunction device_fn(const char *n) {
  if (!strcmp(n, "vkAllocateMemory")) return (PFN_vkVoidFunction)alloc;
  if (!strcmp(n, "vkGetBufferMemoryRequirements")) return (PFN_vkVoidFunction)bmr;
  if (!strcmp(n, "vkGetBufferMemoryRequirements2") || !strcmp(n, "vkGetBufferMemoryRequirements2KHR")) return (PFN_vkVoidFunction)bmr2;
  if (!strcmp(n, "vkGetImageMemoryRequirements")) return (PFN_vkVoidFunction)imr;
  if (!strcmp(n, "vkGetImageMemoryRequirements2") || !strcmp(n, "vkGetImageMemoryRequirements2KHR")) return (PFN_vkVoidFunction)imr2;
  if (!strcmp(n, "vkGetDeviceBufferMemoryRequirements") || !strcmp(n, "vkGetDeviceBufferMemoryRequirementsKHR")) return (PFN_vkVoidFunction)dbmr;
  if (!strcmp(n, "vkGetDeviceImageMemoryRequirements") || !strcmp(n, "vkGetDeviceImageMemoryRequirementsKHR")) return (PFN_vkVoidFunction)dimr;
  if (!strcmp(n, "vkGetMemoryFdPropertiesKHR")) return (PFN_vkVoidFunction)fdp;
  return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL malimem_GetDeviceProcAddr(VkDevice d, const char *n) {
  if (!strcmp(n, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)malimem_GetDeviceProcAddr;
  PFN_vkVoidFunction f = device_fn(n);
  if (f && next_gdpa && next_gdpa(d, n)) return f;
  return next_gdpa ? next_gdpa(d, n) : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL malimem_GetInstanceProcAddr(VkInstance i, const char *n) {
  if (!strcmp(n, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)malimem_GetInstanceProcAddr;
  if (!strcmp(n, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)malimem_GetDeviceProcAddr;
  if (!strcmp(n, "vkCreateInstance")) return (PFN_vkVoidFunction)create_instance;
  if (!strcmp(n, "vkCreateDevice")) return (PFN_vkVoidFunction)create_device;
  if (!strcmp(n, "vkGetPhysicalDeviceMemoryProperties")) return (PFN_vkVoidFunction)mp;
  if (!strcmp(n, "vkGetPhysicalDeviceMemoryProperties2") || !strcmp(n, "vkGetPhysicalDeviceMemoryProperties2KHR")) return (PFN_vkVoidFunction)mp2;
  PFN_vkVoidFunction f = device_fn(n);
  if (f) return f;
  return next_gipa ? next_gipa(i, n) : NULL;
}
