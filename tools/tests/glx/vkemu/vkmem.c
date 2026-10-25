/* vkmem: the server's device-local-only memory type (native GPU memory for what the guest never
 * maps, VKR_NATIVE_MEMORY) through the bridge driver.
 * 1. the memory types are listed; with the server's alias on, one is DEVICE_LOCAL only and an
 *    image's memoryTypeBits include it;
 * 2. images in it work: a 256x256 image is cleared in a render-pass-free way
 *    (vkCmdClearColorImage), copied to a second image in the same type, then to a host-visible
 *    buffer, and every pixel is checked;
 * 3. many allocations and frees in it (as a UI makes) work.
 * Usage: vkmem [expect-alias] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("vkmem: FAIL %s = %d\n", #x, r_); exit(1); } } while (0)

static VkPhysicalDeviceMemoryProperties mp;

static int plain_type(uint32_t bits) {
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && mp.memoryTypes[i].propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) return (int)i;
  return -1;
}

static uint32_t find_memory(uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
  printf("vkmem: FAIL no memory type\n"); exit(1);
}

int main(int argc, char **argv) {
  int expect_alias = argc > 1 && atoi(argv[1]);
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkmem", 1, "vkmem", 1, VK_API_VERSION_1_1};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL};
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, NULL, &inst));
  uint32_t n = 1;
  VkPhysicalDevice pd;
  if (vkEnumeratePhysicalDevices(inst, &n, &pd) < 0 || !n) { printf("vkmem: FAIL no GPU\n"); return 1; }
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    printf("vkmem: memory type %u heap %u flags 0x%x\n", i, mp.memoryTypes[i].heapIndex, mp.memoryTypes[i].propertyFlags);
  VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
  VkPhysicalDeviceMemoryProperties2 mp2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
  vkGetPhysicalDeviceMemoryProperties2(pd, &mp2);
  printf("vkmem: %s memory properties the same with a budget query (%u types)\n",
         mp2.memoryProperties.memoryTypeCount == mp.memoryTypeCount ? "PASS" : "FAIL", mp2.memoryProperties.memoryTypeCount);

  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL, NULL};
  VkDevice dev;
  CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
  VkQueue q;
  vkGetDeviceQueue(dev, 0, 0, &q);

  const uint32_t W = 256, H = 256;
  VkImageCreateInfo imci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                            {W, H, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
  VkImage img[2];
  VkDeviceMemory imem[2];
  int alias = -1;
  for (int i = 0; i < 2; i++) {
    CHECK(vkCreateImage(dev, &imci, NULL, &img[i]));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img[i], &mr);
    VkImageMemoryRequirementsInfo2 ri = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, NULL, img[i]};
    VkMemoryRequirements2 mr2 = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    vkGetImageMemoryRequirements2(dev, &ri, &mr2);
    if (i == 0)
      printf("vkmem: image memoryTypeBits 0x%x (2: 0x%x)\n", mr.memoryTypeBits, mr2.memoryRequirements.memoryTypeBits);
    alias = plain_type(mr.memoryTypeBits);
    if (i == 0) {
      if (expect_alias)
        printf("vkmem: %s an image can use a device-local-only type (%d)\n", alias >= 0 ? "PASS" : "FAIL", alias);
      printf("vkmem: %s memoryTypeBits the same from both queries\n",
             mr.memoryTypeBits == mr2.memoryRequirements.memoryTypeBits ? "PASS" : "FAIL");
    }
    uint32_t type = alias >= 0 ? (uint32_t)alias : find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, type};
    CHECK(vkAllocateMemory(dev, &mai, NULL, &imem[i]));
    CHECK(vkBindImageMemory(dev, img[i], imem[i], 0));
  }

  VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
  VkBuffer buf;
  CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
  VkMemoryRequirements bmr;
  vkGetBufferMemoryRequirements(dev, buf, &bmr);
  VkMemoryAllocateInfo bmai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, bmr.size,
                               find_memory(bmr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  VkDeviceMemory bmem;
  CHECK(vkAllocateMemory(dev, &bmai, NULL, &bmem));
  CHECK(vkBindBufferMemory(dev, buf, bmem, 0));

  VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0};
  VkCommandPool pool;
  CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
  VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
  VkCommandBuffer cb;
  CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));
  VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
  CHECK(vkBeginCommandBuffer(cb, &cbbi));
  VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageMemoryBarrier b[2];
  for (int i = 0; i < 2; i++)
    b[i] = (VkImageMemoryBarrier){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, img[i], range};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, b);
  VkClearColorValue color = {.float32 = {0.0f, 1.0f, 0.0f, 1.0f}};
  vkCmdClearColorImage(cb, img[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
  b[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, b);
  VkImageCopy ic = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
  vkCmdCopyImage(cb, img[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
  b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  b[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b[1]);
  VkBufferImageCopy bic = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
  vkCmdCopyImageToBuffer(cb, img[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &bic);
  VkMemoryBarrier hb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb, 0, NULL};
  CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(q));
  uint32_t *px;
  CHECK(vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, (void **)&px));
  uint32_t bad = 0;
  for (uint32_t i = 0; i < W * H; i++)
    if (px[i] != 0xff00ff00u) bad++;
  printf("vkmem: %s images in %s memory hold what the GPU wrote (%u of %u pixels wrong, first 0x%08x)\n",
         bad ? "FAIL" : "PASS", alias >= 0 ? "device-local-only" : "device-local", bad, W * H, px[0]);
  vkUnmapMemory(dev, bmem);

  /* churn: as a UI allocates and frees */
  int churn_ok = 1;
  for (int i = 0; i < 200 && churn_ok; i++) {
    VkImageCreateInfo c = imci;
    c.extent.width = 64 + (i % 7) * 32;
    c.extent.height = 64 + (i % 5) * 32;
    VkImage im;
    if (vkCreateImage(dev, &c, NULL, &im) != VK_SUCCESS) { churn_ok = 0; break; }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, im, &mr);
    int t = plain_type(mr.memoryTypeBits);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                t >= 0 ? (uint32_t)t : find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory m;
    if (vkAllocateMemory(dev, &mai, NULL, &m) != VK_SUCCESS || vkBindImageMemory(dev, im, m, 0) != VK_SUCCESS) churn_ok = 0;
    vkDestroyImage(dev, im, NULL);
    vkFreeMemory(dev, m, NULL);
  }
  printf("vkmem: %s 200 image allocations and frees\n", churn_ok ? "PASS" : "FAIL");

  vkDestroyCommandPool(dev, pool, NULL);
  for (int i = 0; i < 2; i++) { vkDestroyImage(dev, img[i], NULL); vkFreeMemory(dev, imem[i], NULL); }
  vkDestroyBuffer(dev, buf, NULL);
  vkFreeMemory(dev, bmem, NULL);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(inst, NULL);
  return bad || !churn_ok;
}
