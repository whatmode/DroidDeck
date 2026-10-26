/* vkbc: BC compressed textures through the bridge driver, as DXVK uploads and samples them.
 * Run with DROIDDECK_VN_EMULATE=mali so the driver emulates BC even on a GPU that has it.
 * For BC1 (6x6, 2 layers; and its sRGB form), BC3, BC4, BC5 and BC7: solid-colour blocks are
 * uploaded with vkCmdCopyBufferToImage(2) from a host buffer, the image is blitted to an
 * RGBA8 image and read back; every texel must be the block's colour. A copy of a BC image
 * to a buffer must be left out (the buffer keeps its fill). Prints "vkbc:" lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("vkbc: FAIL %s = %d\n", #x, r_); exit(1); } } while (0)

static VkPhysicalDevice pd;
static VkDevice dev;
static VkQueue q;
static VkCommandPool pool;
static VkPhysicalDeviceMemoryProperties mp;
static int failures;

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
  printf("vkbc: FAIL no memory type\n"); exit(1);
}

struct buf { VkBuffer b; VkDeviceMemory m; void *p; };
static struct buf make_buf(VkDeviceSize size, VkBufferUsageFlags usage) {
  struct buf r;
  VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
  CHECK(vkCreateBuffer(dev, &bci, NULL, &r.b));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(dev, r.b, &req);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size,
                              mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  CHECK(vkAllocateMemory(dev, &mai, NULL, &r.m));
  CHECK(vkBindBufferMemory(dev, r.b, r.m, 0));
  CHECK(vkMapMemory(dev, r.m, 0, VK_WHOLE_SIZE, 0, &r.p));
  return r;
}

static VkImage make_image(VkFormat f, uint32_t w, uint32_t h, uint32_t layers, VkImageUsageFlags usage, VkDeviceMemory *mem) {
  VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, f, {w, h, 1}, 1, layers,
                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
                           VK_IMAGE_LAYOUT_UNDEFINED};
  VkImage img;
  CHECK(vkCreateImage(dev, &ici, NULL, &img));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(dev, img, &req);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, mem_type(req.memoryTypeBits, 0)};
  CHECK(vkAllocateMemory(dev, &mai, NULL, mem));
  CHECK(vkBindImageMemory(dev, img, *mem, 0));
  return img;
}

static void barrier(VkCommandBuffer cb, VkImage img, uint32_t layers, VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, from, to,
                            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, img, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers}};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

/* one case: a w x h x layers BC image filled with `block`, blitted to RGBA8, every texel compared */
static void run_case(const char *name, VkFormat f, const unsigned char *block, unsigned block_bytes,
                     uint32_t w, uint32_t h, uint32_t layers, const int want[4], int use_copy2) {
  VkFormatProperties2 fp = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
  vkGetPhysicalDeviceFormatProperties2(pd, f, &fp);
  const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT;
  if ((fp.formatProperties.optimalTilingFeatures & need) != need) {
    printf("vkbc: FAIL %s: format features 0x%x\n", name, fp.formatProperties.optimalTilingFeatures);
    failures++;
    return;
  }
  VkPhysicalDeviceImageFormatInfo2 ifi = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, NULL, f, VK_IMAGE_TYPE_2D,
                                          VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0};
  VkImageFormatProperties2 ifp = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  if (vkGetPhysicalDeviceImageFormatProperties2(pd, &ifi, &ifp) != VK_SUCCESS) {
    printf("vkbc: FAIL %s: image format properties\n", name);
    failures++;
    return;
  }
  const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
  struct buf src = make_buf((VkDeviceSize)bw * bh * block_bytes * layers + 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  for (uint32_t i = 0; i < bw * bh * layers; i++) memcpy((char *)src.p + 256 + i * block_bytes, block, block_bytes);
  VkDeviceMemory m1, m2;
  VkImage bc = make_image(f, w, h, layers, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &m1);
  VkImage rgba = make_image(VK_FORMAT_R8G8B8A8_UNORM, w, h, layers, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &m2);
  struct buf out = make_buf((VkDeviceSize)w * h * 4 * layers, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  struct buf back = make_buf(4096, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  memset(back.p, 0xab, 4096);
  /* a view as DXVK makes one (sampling) */
  VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, bc, VK_IMAGE_VIEW_TYPE_2D_ARRAY, f, {0},
                               {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers}};
  VkImageView view;
  CHECK(vkCreateImageView(dev, &vci, NULL, &view));

  VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
  VkCommandBuffer cb;
  CHECK(vkAllocateCommandBuffers(dev, &cbai, &cb));
  VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
  CHECK(vkBeginCommandBuffer(cb, &bi));
  barrier(cb, bc, layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  barrier(cb, rgba, layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  if (use_copy2) {
    VkBufferImageCopy2 r2 = {VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, NULL, 256, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, {0, 0, 0}, {w, h, 1}};
    VkCopyBufferToImageInfo2 ci2 = {VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2, NULL, src.b, bc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r2};
    vkCmdCopyBufferToImage2(cb, &ci2);
  } else {
    VkBufferImageCopy r = {256, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, {0, 0, 0}, {w, h, 1}};
    vkCmdCopyBufferToImage(cb, src.b, bc, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
  }
  barrier(cb, bc, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkImageBlit blit = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, {{0, 0, 0}, {(int)w, (int)h, 1}},
                      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, {{0, 0, 0}, {(int)w, (int)h, 1}}};
  vkCmdBlitImage(cb, bc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rgba, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
  /* a BC image read back as compressed data: must be left out */
  VkBufferImageCopy rb = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {4, 4, 1}};
  vkCmdCopyImageToBuffer(cb, bc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, back.b, 1, &rb);
  barrier(cb, rgba, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy ro = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers}, {0, 0, 0}, {w, h, 1}};
  vkCmdCopyImageToBuffer(cb, rgba, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.b, 1, &ro);
  VkMemoryBarrier hb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb, 0, NULL};
  CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(q));

  const unsigned char *px = out.p;
  int bad = 0, first = -1;
  for (uint32_t i = 0; i < w * h * layers; i++) {
    for (int c = 0; c < 4; c++) {
      if (want[c] >= 0 && abs((int)px[i * 4 + c] - want[c]) > 3) { bad++; if (first < 0) first = (int)i; break; }
    }
  }
  int kept = 1;
  for (int i = 0; i < 4096; i++) if (((unsigned char *)back.p)[i] != 0xab) { kept = 0; break; }
  printf("vkbc: %s %s %ux%u x%u (%s): %d of %u texels wrong (first %d: %d,%d,%d,%d; want %d,%d,%d,%d), read-back %s\n",
         bad || !kept ? "FAIL" : "PASS", name, w, h, layers, use_copy2 ? "copy2" : "copy", bad, w * h * layers, first,
         first >= 0 ? px[first * 4] : px[0], first >= 0 ? px[first * 4 + 1] : px[1], first >= 0 ? px[first * 4 + 2] : px[2],
         first >= 0 ? px[first * 4 + 3] : px[3], want[0], want[1], want[2], want[3], kept ? "left out" : "WRITTEN");
  if (bad || !kept) failures++;
  vkFreeCommandBuffers(dev, pool, 1, &cb);
  vkDestroyImageView(dev, view, NULL);
  vkDestroyImage(dev, bc, NULL);
  vkDestroyImage(dev, rgba, NULL);
  vkFreeMemory(dev, m1, NULL);
  vkFreeMemory(dev, m2, NULL);
  vkDestroyBuffer(dev, src.b, NULL);
  vkFreeMemory(dev, src.m, NULL);
  vkDestroyBuffer(dev, out.b, NULL);
  vkFreeMemory(dev, out.m, NULL);
  vkDestroyBuffer(dev, back.b, NULL);
  vkFreeMemory(dev, back.m, NULL);
}

int main(void) {
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkbc", 1, "DXVK", 1, VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL};
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, NULL, &inst));
  uint32_t n = 1;
  if (vkEnumeratePhysicalDevices(inst, &n, &pd) < 0 || !n) { printf("vkbc: FAIL no GPU\n"); return 1; }
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  VkPhysicalDeviceFeatures feats;
  vkGetPhysicalDeviceFeatures(pd, &feats);
  printf("vkbc: %s textureCompressionBC listed (%d)\n", feats.textureCompressionBC ? "PASS" : "FAIL", feats.textureCompressionBC);
  if (!feats.textureCompressionBC) failures++;
  VkPhysicalDeviceFeatures on = {0};
  on.textureCompressionBC = VK_TRUE;
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 0, NULL, &on};
  CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
  vkGetDeviceQueue(dev, 0, 0, &q);
  VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0};
  CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));

  /* BC1: colour0 red (565 0xF800) > colour1, indices 0 */
  static const unsigned char bc1[8] = {0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0};
  /* BC3: alpha 128 (a0=128 > a1=0, indices 0), colour as BC1 */
  static const unsigned char bc3[16] = {0x80, 0x00, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0};
  /* BC4: red 200 */
  static const unsigned char bc4[8] = {200, 0, 0, 0, 0, 0, 0, 0};
  /* BC5: red 200, green 100 */
  static const unsigned char bc5[16] = {200, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
  /* BC7 mode 6, endpoints (255,1,1,255) both, indices 0 */
  static const unsigned char bc7[16] = {0xc0, 0xff, 0x1f, 0x00, 0x00, 0x00, 0xfe, 0xff, 0x01, 0, 0, 0, 0, 0, 0, 0};
  const int red[4] = {255, 0, 0, 255}, red_a128[4] = {255, 0, 0, 128}, r200[4] = {200, 0, 0, 255},
            rg[4] = {200, 100, 0, 255}, red7[4] = {255, 1, 1, 255};
  run_case("BC1", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, bc1, 8, 8, 8, 1, red, 0);
  run_case("BC1 odd size, 2 layers", VK_FORMAT_BC1_RGB_UNORM_BLOCK, bc1, 8, 6, 6, 2, red, 1);
  run_case("BC1 sRGB", VK_FORMAT_BC1_RGBA_SRGB_BLOCK, bc1, 8, 8, 8, 1, red, 0);
  run_case("BC3", VK_FORMAT_BC3_UNORM_BLOCK, bc3, 16, 8, 8, 1, red_a128, 0);
  run_case("BC4", VK_FORMAT_BC4_UNORM_BLOCK, bc4, 8, 8, 8, 1, r200, 0);
  run_case("BC5", VK_FORMAT_BC5_UNORM_BLOCK, bc5, 16, 8, 8, 1, rg, 1);
  run_case("BC7", VK_FORMAT_BC7_UNORM_BLOCK, bc7, 16, 16, 16, 1, red7, 0);
  printf("vkbc: done, %d failure(s)\n", failures);
  vkDestroyCommandPool(dev, pool, NULL);
  vkDestroyDevice(dev, NULL);
  vkDestroyInstance(inst, NULL);
  return failures ? 1 : 0;
}
