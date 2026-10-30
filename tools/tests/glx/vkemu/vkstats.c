/* vkstats: the driver's game-session reports, against a real venus guest and server.
 *  - a descriptor pool DXVK-style: fill it, and the next allocation must come back
 *    VK_ERROR_OUT_OF_POOL_MEMORY (from the guest's accounting unless DROIDDECK_VN_POOL_DEFER=1);
 *  - submissions for a while, each waited on, so the 10 s reports (shortened by the test's
 *    DROIDDECK_VN_STATS_MS / DROIDDECK_VN_WAIT_STACKS_MS) have GPU spans and wait stacks.
 * Prints "vkstats: ..." lines; exit 0 when every Vulkan call did what it should. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define CHECK(x)                                                                                   \
   do {                                                                                            \
      VkResult r_ = (x);                                                                           \
      if (r_ != VK_SUCCESS) {                                                                      \
         printf("vkstats: %s failed: %d\n", #x, r_);                                               \
         return 1;                                                                                 \
      }                                                                                            \
   } while (0)

static double
now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int
main(int argc, char **argv)
{
   const double seconds = argc > 1 ? atof(argv[1]) : 2.0;
   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vkstats",
      .pEngineName = "DXVK",
      .apiVersion = VK_API_VERSION_1_1,
   };
   const VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
   uint32_t n = 1;
   VkPhysicalDevice pd;
   VkResult er = vkEnumeratePhysicalDevices(inst, &n, &pd);
   if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !n) {
      printf("vkstats: no GPU\n");
      return 1;
   }
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(pd, &props);
   printf("vkstats: GPU %s\n", props.deviceName);
   const float prio = 1.0f;
   const VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio,
   };
   const VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
   };
   VkDevice dev;
   CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
   VkQueue queue;
   vkGetDeviceQueue(dev, 0, 0, &queue);

   /* which memory types an optimal-tiling texture may use, and a linear one (0028: the optimal one
    * must not be offered host-visible types when the server has native ones) */
   {
      VkPhysicalDeviceMemoryProperties mp0;
      vkGetPhysicalDeviceMemoryProperties(pd, &mp0);
      for (int linear = 0; linear < 2; linear++) {
         const VkImageCreateInfo ici2 = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { 256, 256, 1 }, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         };
         VkImage img;
         CHECK(vkCreateImage(dev, &ici2, NULL, &img));
         VkMemoryRequirements r;
         vkGetImageMemoryRequirements(dev, img, &r);
         uint32_t hv = 0;
         for (uint32_t i = 0; i < mp0.memoryTypeCount; i++)
            if ((r.memoryTypeBits & (1u << i)) && (mp0.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
               hv |= 1u << i;
         printf("vkstats: %s image memory types 0x%x, host-visible among them 0x%x\n", linear ? "linear" : "optimal",
                r.memoryTypeBits, hv);
         vkDestroyImage(dev, img, NULL);
      }
   }

   /* a compute pipeline with no cache of the program's own (as DXVK makes them): the driver's
    * on-disk cache (0030) takes it */
   if (argc > 2) {
      FILE *f = fopen(argv[2], "rb");
      if (!f) {
         printf("vkstats: no shader %s\n", argv[2]);
         return 1;
      }
      static uint32_t code[16384];
      const size_t bytes = fread(code, 1, sizeof(code), f);
      fclose(f);
      const VkShaderModuleCreateInfo smci = {
         .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = bytes, .pCode = code,
      };
      VkShaderModule sm;
      CHECK(vkCreateShaderModule(dev, &smci, NULL, &sm));
      const VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      VkPipelineLayout pl;
      CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
      const VkComputePipelineCreateInfo cpci2 = {
         .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" },
         .layout = pl,
      };
      VkPipeline pipe;
      const double t0 = now_ms();
      CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci2, NULL, &pipe));
      printf("vkstats: compute pipeline made in %.1f ms\n", now_ms() - t0);
      vkDestroyPipeline(dev, pipe, NULL);
      vkDestroyPipelineLayout(dev, pl, NULL);
      vkDestroyShaderModule(dev, sm, NULL);
   }

   /* the pool: room for 4 sets of one uniform buffer each, no FREE_DESCRIPTOR_SET (DXVK's) */
   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_ALL,
   };
   const VkDescriptorSetLayoutCreateInfo lci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding,
   };
   VkDescriptorSetLayout layout;
   CHECK(vkCreateDescriptorSetLayout(dev, &lci, NULL, &layout));
   const VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 };
   const VkDescriptorPoolCreateInfo pci = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 4, .poolSizeCount = 1, .pPoolSizes = &ps,
   };
   VkDescriptorPool pool;
   CHECK(vkCreateDescriptorPool(dev, &pci, NULL, &pool));
   int refusals = 0, rounds = 0;
   double refuse_ms = 0;
   for (int round = 0; round < 50; round++, rounds++) {
      for (int i = 0; i < 4; i++) {
         VkDescriptorSet set;
         const VkDescriptorSetAllocateInfo ai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
            .descriptorSetCount = 1, .pSetLayouts = &layout,
         };
         CHECK(vkAllocateDescriptorSets(dev, &ai, &set));
      }
      VkDescriptorSet extra;
      const VkDescriptorSetAllocateInfo ai = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
         .descriptorSetCount = 1, .pSetLayouts = &layout,
      };
      const double t0 = now_ms();
      VkResult r = vkAllocateDescriptorSets(dev, &ai, &extra);
      refuse_ms += now_ms() - t0;
      if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL)
         refusals++;
      else
         printf("vkstats: a fifth set from a pool of four came back %d\n", r);
      CHECK(vkResetDescriptorPool(dev, pool, 0));
   }
   printf("vkstats: full pool refused %d of %d times (%.3f ms per refusal)\n", refusals, rounds,
          refusals ? refuse_ms / refusals : 0.0);

   /* submissions: a fill of 4 MiB each, waited on with a fence */
   const VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 4 << 20, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
   };
   VkBuffer buf;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(dev, buf, &req);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(pd, &mp);
   uint32_t type = 0;
   while (type < mp.memoryTypeCount && !(req.memoryTypeBits & (1u << type)))
      type++;
   const VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type,
   };
   VkDeviceMemory mem;
   CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
   CHECK(vkBindBufferMemory(dev, buf, mem, 0));
   const VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
   };
   VkCommandPool cpool;
   CHECK(vkCreateCommandPool(dev, &cpci, NULL, &cpool));
   const VkCommandBufferAllocateInfo cbai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cpool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
   };
   VkCommandBuffer cmd;
   CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
   const VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   VkFence fence;
   CHECK(vkCreateFence(dev, &fci, NULL, &fence));
   const double start = now_ms();
   int submits = 0;
   while (now_ms() - start < seconds * 1000) {
      const VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      CHECK(vkBeginCommandBuffer(cmd, &bi));
      vkCmdFillBuffer(cmd, buf, 0, VK_WHOLE_SIZE, (uint32_t)submits);
      CHECK(vkEndCommandBuffer(cmd));
      const VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
      CHECK(vkQueueSubmit(queue, 1, &si, fence));
      CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
      CHECK(vkResetFences(dev, 1, &fence));
      submits++;
   }
   printf("vkstats: %d submissions in %.1f s\n", submits, (now_ms() - start) / 1e3);
   CHECK(vkDeviceWaitIdle(dev));
   vkDestroyFence(dev, fence, NULL);
   vkDestroyCommandPool(dev, cpool, NULL);
   vkDestroyBuffer(dev, buf, NULL);
   vkFreeMemory(dev, mem, NULL);
   vkDestroyDescriptorPool(dev, pool, NULL);
   vkDestroyDescriptorSetLayout(dev, layout, NULL);
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   printf("vkstats: done\n");
   return refusals == rounds ? 0 : 2;
}
