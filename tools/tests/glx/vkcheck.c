/* vkcheck: the bridge driver's own log of what a program asks of Vulkan (droiddeck_vulkan.txt).
 * An instance with an extension the driver lacks (as a Windows game's would, through Wine),
 * then one it can make; its GPUs (none here: no bridge server). */
#include <stdio.h>
#include <vulkan/vulkan.h>

int main(void) {
  const char *bad[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
  const char *good[] = {"VK_KHR_surface", "VK_KHR_xlib_surface"};
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkcheck", 1, "DXVK", 1, VK_API_VERSION_1_3};
  VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 2, bad};
  VkInstance inst;
  VkResult r = vkCreateInstance(&ci, NULL, &inst);
  printf("vkcheck: instance with win32 surface: %d\n", r);
  ci.ppEnabledExtensionNames = good;
  r = vkCreateInstance(&ci, NULL, &inst);
  printf("vkcheck: instance with xlib surface: %d\n", r);
  if (r == VK_SUCCESS) {
    uint32_t n = 0;
    r = vkEnumeratePhysicalDevices(inst, &n, NULL);
    printf("vkcheck: %u GPU(s), %d\n", n, r);
    vkDestroyInstance(inst, NULL);
  }
  printf("vkcheck: done\n");
  return 0;
}
