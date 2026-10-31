/* vkemu: what DXVK asks of the GPU that Mali lacks, through the bridge driver (venus guest).
 * 1. the six features DXVK requires are listed as supported;
 * 2. a device with them enabled is made;
 * 3. a pipeline with a wireframe polygon mode, two viewports and a vertex shader writing clip
 *    and cull distances (as DXVK does) is made and draws: the picture must be green.
 * Usage: vkemu <clip vertex shader .spv>... <fragment shader .spv> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("vkemu: FAIL %s = %d\n", #x, r_); exit(1); } } while (0)

static uint32_t *load(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) { printf("vkemu: FAIL cannot open %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
  uint32_t *code = malloc(*size);
  if (fread(code, 1, *size, f) != *size) exit(1);
  fclose(f);
  return code;
}

static uint32_t find_memory(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
  printf("vkemu: FAIL no memory type\n"); exit(1);
}

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkemu", 1, "DXVK", 1, VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 0, NULL};
  VkInstance inst;
  CHECK(vkCreateInstance(&ici, NULL, &inst));
  uint32_t n = 1;
  VkPhysicalDevice pd;
  CHECK(vkEnumeratePhysicalDevices(inst, &n, &pd) < 0 ? VK_ERROR_UNKNOWN : VK_SUCCESS);
  if (!n) { printf("vkemu: FAIL no GPU\n"); return 1; }
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(pd, &props);
  printf("vkemu: GPU %s\n", props.deviceName);

  VkPhysicalDeviceRobustness2FeaturesEXT rob = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
  VkPhysicalDeviceVulkan13Features v13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &rob};
  VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13};
  vkGetPhysicalDeviceFeatures2(pd, &f2);
  printf("vkemu: features robustBufferAccess2 %d nullDescriptor %d fillModeNonSolid %d multiViewport %d "
         "shaderClipDistance %d shaderCullDistance %d dynamicRendering %d\n",
         rob.robustBufferAccess2, rob.nullDescriptor, f2.features.fillModeNonSolid, f2.features.multiViewport,
         f2.features.shaderClipDistance, f2.features.shaderCullDistance, v13.dynamicRendering);
  int listed = rob.robustBufferAccess2 && f2.features.fillModeNonSolid && f2.features.multiViewport &&
               f2.features.shaderClipDistance && f2.features.shaderCullDistance;
  printf("vkemu: %s DXVK's features listed\n", listed ? "PASS" : "FAIL");

  /* a device with them on, as DXVK makes it */
  VkPhysicalDeviceRobustness2FeaturesEXT rob_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, NULL,
                                                   VK_TRUE, VK_FALSE, rob.nullDescriptor};
  VkPhysicalDeviceVulkan13Features v13_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &rob_on};
  v13_on.dynamicRendering = VK_TRUE;
  v13_on.synchronization2 = VK_TRUE;
  VkPhysicalDeviceFeatures2 f2_on = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v13_on};
  f2_on.features.robustBufferAccess = VK_TRUE;
  f2_on.features.fillModeNonSolid = VK_TRUE;
  f2_on.features.multiViewport = VK_TRUE;
  f2_on.features.shaderClipDistance = VK_TRUE;
  f2_on.features.shaderCullDistance = VK_TRUE;
  float prio = 1;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio};
  const char *exts[] = {"VK_EXT_robustness2"};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2_on, 0, 1, &qci, 0, NULL, 1, exts, NULL};
  VkDevice dev;
  VkResult dr = vkCreateDevice(pd, &dci, NULL, &dev);
  printf("vkemu: %s device with DXVK's features (%d)\n", dr == VK_SUCCESS ? "PASS" : "FAIL", dr);
  if (dr != VK_SUCCESS) return 1;
  /* the program's structs are as it made them */
  printf("vkemu: %s create info left as the program made it\n",
         f2_on.features.shaderClipDistance && rob_on.robustBufferAccess2 ? "PASS" : "FAIL");
  VkQueue q;
  vkGetDeviceQueue(dev, 0, 0, &q);

  /* target image and readback buffer */
  const uint32_t W = 64, H = 64;
  VkImageCreateInfo imci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                            {W, H, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
  VkImage img;
  CHECK(vkCreateImage(dev, &imci, NULL, &img));
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(dev, img, &mr);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                              find_memory(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
  VkDeviceMemory imem;
  CHECK(vkAllocateMemory(dev, &mai, NULL, &imem));
  CHECK(vkBindImageMemory(dev, img, imem, 0));
  VkImageViewCreateInfo ivci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D,
                                VK_FORMAT_R8G8B8A8_UNORM, {0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  VkImageView view;
  CHECK(vkCreateImageView(dev, &ivci, NULL, &view));
  VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
  VkBuffer buf;
  CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
  vkGetBufferMemoryRequirements(dev, buf, &mr);
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = find_memory(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory bmem;
  CHECK(vkAllocateMemory(dev, &mai, NULL, &bmem));
  CHECK(vkBindBufferMemory(dev, buf, bmem, 0));

  size_t fs_size;
  uint32_t *fs_code = load(argv[argc - 1], &fs_size);
  VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, fs_size, fs_code};
  VkShaderModule fs;
  CHECK(vkCreateShaderModule(dev, &smci, NULL, &fs));
  VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkPipelineLayout layout;
  CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));
  VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
                                  VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0};
  VkCommandPool pool;
  CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));

  int fails = !listed;
  for (int s = 1; s < argc - 1; s++) {
    size_t vs_size;
    uint32_t *vs_code = load(argv[s], &vs_size);
    smci.codeSize = vs_size;
    smci.pCode = vs_code;
    VkShaderModule vs;
    CHECK(vkCreateShaderModule(dev, &smci, NULL, &vs));
    VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                 VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkViewport vps[2] = {{0, 0, W, H, 0, 1}, {0, 0, W / 2, H / 2, 0, 1}};
    VkRect2D scs[2] = {{{0, 0}, {W, H}}, {{0, 0}, {W / 2, H / 2}}};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 2, vps, 2, scs};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_LINE;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkFormat cf = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, 1, &cf};
    VkGraphicsPipelineCreateInfo gpci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rci, 0, 2, stages, &vi, &ia,
                                         NULL, &vp, &rs, &ms, NULL, &cb, NULL, layout};
    VkPipeline pipe;
    VkResult pr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
    printf("vkemu: %s pipeline (wireframe, 2 viewports, clip/cull distances) from %s (%d)\n",
           pr == VK_SUCCESS ? "PASS" : "FAIL", argv[s], pr);
    if (pr != VK_SUCCESS) { fails++; continue; }
    if (rs.polygonMode != VK_POLYGON_MODE_LINE || vp.viewportCount != 2) {
      printf("vkemu: FAIL the pipeline's create info was left changed\n");
      fails++;
    }

    VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
                                        VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
                                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    VkImageMemoryBarrier toatt = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0,
                                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 0, img,
                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, NULL, 0, NULL, 1, &toatt);
    VkRenderingAttachmentInfo att = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, NULL, view,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.color.float32[0] = 1;
    att.clearValue.color.float32[3] = 1;
    VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO, NULL, 0, {{0, 0}, {W, H}}, 1, 0, 1, &att};
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    VkImageMemoryBarrier tosrc = toatt;
    tosrc.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    tosrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    tosrc.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    tosrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         NULL, 0, NULL, 1, &tosrc);
    VkBufferImageCopy region = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cmd, 0, NULL};
    CHECK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(q));
    uint8_t *px;
    CHECK(vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, (void **)&px));
    const uint8_t *c = px + ((H / 2) * W + W / 2) * 4, *corner = px + ((H - 4) * W + (W - 4)) * 4;
    int green = c[0] < 40 && c[1] > 200 && c[2] < 40 && corner[1] > 200 && corner[0] < 40;
    printf("vkemu: %s drawn filled and whole: center %u,%u,%u corner %u,%u,%u (want green, the triangle filled)\n",
           green ? "PASS" : "FAIL", c[0], c[1], c[2], corner[0], corner[1], corner[2]);
    fails += !green;
    vkUnmapMemory(dev, bmem);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, vs, NULL);
  }
  printf("vkemu: done, %d failure(s)\n", fails);
  return fails ? 1 : 0;
}
