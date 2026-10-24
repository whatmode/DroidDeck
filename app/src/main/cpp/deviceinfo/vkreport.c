/*
 * Full report of Android's own Vulkan driver (the one DroidDeck's compositor uses): instance and
 * device extensions, every core feature, memory types, queue families and which external memory
 * handle types it can export and import. Written into the shared logs zip as android-vulkan.txt.
 *
 * It answers whether that driver could carry the Linux side's Vulkan through a bridge (what DXVK
 * needs, and whether memory can be shared as dma-buf / AHardwareBuffer between processes).
 */
#include <dlfcn.h>
#include <jni.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

struct buf { char *p; size_t used, size; };

static void put(struct buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(struct buf *b, const char *fmt, ...) {
    if (!b->p || b->used >= b->size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->p + b->used, b->size - b->used, fmt, ap);
    va_end(ap);
    if (n > 0) b->used += (size_t) n < b->size - b->used ? (size_t) n : b->size - b->used - 1;
}

static const char *const CORE[] = {
    "robustBufferAccess", "fullDrawIndexUint32", "imageCubeArray", "independentBlend", "geometryShader",
    "tessellationShader", "sampleRateShading", "dualSrcBlend", "logicOp", "multiDrawIndirect",
    "drawIndirectFirstInstance", "depthClamp", "depthBiasClamp", "fillModeNonSolid", "depthBounds",
    "wideLines", "largePoints", "alphaToOne", "multiViewport", "samplerAnisotropy",
    "textureCompressionETC2", "textureCompressionASTC_LDR", "textureCompressionBC", "occlusionQueryPrecise",
    "pipelineStatisticsQuery", "vertexPipelineStoresAndAtomics", "fragmentStoresAndAtomics",
    "shaderTessellationAndGeometryPointSize", "shaderImageGatherExtended", "shaderStorageImageExtendedFormats",
    "shaderStorageImageMultisample", "shaderStorageImageReadWithoutFormat", "shaderStorageImageWriteWithoutFormat",
    "shaderUniformBufferArrayDynamicIndexing", "shaderSampledImageArrayDynamicIndexing",
    "shaderStorageBufferArrayDynamicIndexing", "shaderStorageImageArrayDynamicIndexing", "shaderClipDistance",
    "shaderCullDistance", "shaderFloat64", "shaderInt64", "shaderInt16", "shaderResourceResidency",
    "shaderResourceMinLod", "sparseBinding", "sparseResidencyBuffer", "sparseResidencyImage2D",
    "sparseResidencyImage3D", "sparseResidency2Samples", "sparseResidency4Samples", "sparseResidency8Samples",
    "sparseResidency16Samples", "sparseResidencyAliased", "variableMultisampleRate", "inheritedQueries",
};
_Static_assert(sizeof(CORE) / sizeof(*CORE) == sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32), "core features");

static const char *const V11[] = {
    "storageBuffer16BitAccess", "uniformAndStorageBuffer16BitAccess", "storagePushConstant16",
    "storageInputOutput16", "multiview", "multiviewGeometryShader", "multiviewTessellationShader",
    "variablePointersStorageBuffer", "variablePointers", "protectedMemory", "samplerYcbcrConversion",
    "shaderDrawParameters",
};
_Static_assert(sizeof(V11) / sizeof(*V11) == (offsetof(VkPhysicalDeviceVulkan11Features, shaderDrawParameters) - offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess)) / sizeof(VkBool32) + 1, "1.1 features");

static const char *const V12[] = {
    "samplerMirrorClampToEdge", "drawIndirectCount", "storageBuffer8BitAccess", "uniformAndStorageBuffer8BitAccess",
    "storagePushConstant8", "shaderBufferInt64Atomics", "shaderSharedInt64Atomics", "shaderFloat16", "shaderInt8",
    "descriptorIndexing", "shaderInputAttachmentArrayDynamicIndexing", "shaderUniformTexelBufferArrayDynamicIndexing",
    "shaderStorageTexelBufferArrayDynamicIndexing", "shaderUniformBufferArrayNonUniformIndexing",
    "shaderSampledImageArrayNonUniformIndexing", "shaderStorageBufferArrayNonUniformIndexing",
    "shaderStorageImageArrayNonUniformIndexing", "shaderInputAttachmentArrayNonUniformIndexing",
    "shaderUniformTexelBufferArrayNonUniformIndexing", "shaderStorageTexelBufferArrayNonUniformIndexing",
    "descriptorBindingUniformBufferUpdateAfterBind", "descriptorBindingSampledImageUpdateAfterBind",
    "descriptorBindingStorageImageUpdateAfterBind", "descriptorBindingStorageBufferUpdateAfterBind",
    "descriptorBindingUniformTexelBufferUpdateAfterBind", "descriptorBindingStorageTexelBufferUpdateAfterBind",
    "descriptorBindingUpdateUnusedWhilePending", "descriptorBindingPartiallyBound",
    "descriptorBindingVariableDescriptorCount", "runtimeDescriptorArray", "samplerFilterMinmax",
    "scalarBlockLayout", "imagelessFramebuffer", "uniformBufferStandardLayout", "shaderSubgroupExtendedTypes",
    "separateDepthStencilLayouts", "hostQueryReset", "timelineSemaphore", "bufferDeviceAddress",
    "bufferDeviceAddressCaptureReplay", "bufferDeviceAddressMultiDevice", "vulkanMemoryModel",
    "vulkanMemoryModelDeviceScope", "vulkanMemoryModelAvailabilityVisibilityChains", "shaderOutputViewportIndex",
    "shaderOutputLayer", "subgroupBroadcastDynamicId",
};
_Static_assert(sizeof(V12) / sizeof(*V12) == (offsetof(VkPhysicalDeviceVulkan12Features, subgroupBroadcastDynamicId) - offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge)) / sizeof(VkBool32) + 1, "1.2 features");

static const char *const V13[] = {
    "robustImageAccess", "inlineUniformBlock", "descriptorBindingInlineUniformBlockUpdateAfterBind",
    "pipelineCreationCacheControl", "privateData", "shaderDemoteToHelperInvocation", "shaderTerminateInvocation",
    "subgroupSizeControl", "computeFullSubgroups", "synchronization2", "textureCompressionASTC_HDR",
    "shaderZeroInitializeWorkgroupMemory", "dynamicRendering", "shaderIntegerDotProduct", "maintenance4",
};
_Static_assert(sizeof(V13) / sizeof(*V13) == (offsetof(VkPhysicalDeviceVulkan13Features, maintenance4) - offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess)) / sizeof(VkBool32) + 1, "1.3 features");

static void bools(struct buf *b, const char *group, const char *const *names, size_t count, const VkBool32 *values) {
    put(b, "\n[%s]\n", group);
    for (size_t i = 0; i < count; i++) put(b, "%s=%s\n", names[i], values[i] ? "yes" : "no");
}

static void flags(struct buf *b, VkFlags f, const char *const *names, size_t count) {
    int any = 0;
    for (size_t i = 0; i < count; i++)
        if (f & (1u << i)) { put(b, "%s%s", any ? "|" : "", names[i]); any = 1; }
    if (!any) put(b, "-");
}

JNIEXPORT jstring JNICALL
Java_com_droiddeck_launcher_gpu_VulkanInfo_nativeReport(JNIEnv *env, jclass cls) {
    (void) cls;
    struct buf b = { malloc(256 * 1024), 0, 256 * 1024 };
    if (!b.p) return (*env)->NewStringUTF(env, "error=out of memory\n");
    b.p[0] = 0;
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr gipa = lib ? (PFN_vkGetInstanceProcAddr) dlsym(lib, "vkGetInstanceProcAddr") : NULL;
    if (!gipa) {
        put(&b, "error=libvulkan.so not usable\n");
        goto done;
    }
    PFN_vkEnumerateInstanceVersion instanceVersion = (PFN_vkEnumerateInstanceVersion) gipa(NULL, "vkEnumerateInstanceVersion");
    PFN_vkEnumerateInstanceExtensionProperties instanceExts = (PFN_vkEnumerateInstanceExtensionProperties) gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance) gipa(NULL, "vkCreateInstance");
    uint32_t version = VK_API_VERSION_1_0;
    if (instanceVersion) instanceVersion(&version);
    put(&b, "Android Vulkan driver report\ninstance_api=%u.%u.%u\n", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));
    uint32_t n = 0;
    if (instanceExts) instanceExts(NULL, &n, NULL);
    VkExtensionProperties *ie = n ? calloc(n, sizeof(*ie)) : NULL;
    if (ie && instanceExts) instanceExts(NULL, &n, ie);
    put(&b, "\n[instance extensions] %u\n", n);
    for (uint32_t i = 0; ie && i < n; i++) put(&b, "%s %u\n", ie[i].extensionName, ie[i].specVersion);
    free(ie);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DroidDeck report",
                              .apiVersion = version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : version };
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = createInstance ? createInstance(&ci, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    if (r != VK_SUCCESS) {
        put(&b, "error=vkCreateInstance %d\n", r);
        goto done;
    }
#define GET(name) PFN_##name name = (PFN_##name) gipa(instance, #name)
#define GET2(name) PFN_##name name = (PFN_##name) gipa(instance, #name); if (!name) { put(&b, "missing " #name "\n"); goto probe_done; }
    GET(vkDestroyInstance); GET(vkEnumeratePhysicalDevices); GET(vkGetPhysicalDeviceProperties2);
    GET(vkGetPhysicalDeviceFeatures2); GET(vkEnumerateDeviceExtensionProperties);
    GET(vkGetPhysicalDeviceMemoryProperties); GET(vkGetPhysicalDeviceQueueFamilyProperties);
    GET(vkGetPhysicalDeviceExternalBufferProperties); GET(vkGetPhysicalDeviceFormatProperties);
    VkPhysicalDevice devices[4];
    uint32_t count = 4;
    if (!vkEnumeratePhysicalDevices || vkEnumeratePhysicalDevices(instance, &count, devices) < 0) count = 0;
    put(&b, "physical_devices=%u\n", count);
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDevice pd = devices[d];
        VkPhysicalDeviceDriverProperties driver = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver };
        if (vkGetPhysicalDeviceProperties2) vkGetPhysicalDeviceProperties2(pd, &p2);
        VkPhysicalDeviceProperties *p = &p2.properties;
        put(&b, "\n== device %u: %s\napi=%u.%u.%u\ndriver_version=0x%08x\ndriver_name=%s\ndriver_info=%s\nvendor=0x%04x device=0x%08x\n",
            d, p->deviceName, VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion),
            VK_API_VERSION_PATCH(p->apiVersion), p->driverVersion, driver.driverName, driver.driverInfo, p->vendorID, p->deviceID);
        put(&b, "maxImageDimension2D=%u maxStorageBufferRange=%u maxPushConstantsSize=%u maxBoundDescriptorSets=%u "
                "maxPerStageDescriptorSamplers=%u maxPerStageDescriptorSampledImages=%u maxPerStageDescriptorStorageBuffers=%u "
                "maxPerStageDescriptorUniformBuffers=%u maxVertexInputAttributes=%u maxColorAttachments=%u "
                "minMemoryMapAlignment=%zu nonCoherentAtomSize=%llu timestampPeriod=%f\n",
            p->limits.maxImageDimension2D, p->limits.maxStorageBufferRange, p->limits.maxPushConstantsSize,
            p->limits.maxBoundDescriptorSets, p->limits.maxPerStageDescriptorSamplers,
            p->limits.maxPerStageDescriptorSampledImages, p->limits.maxPerStageDescriptorStorageBuffers,
            p->limits.maxPerStageDescriptorUniformBuffers, p->limits.maxVertexInputAttributes, p->limits.maxColorAttachments,
            p->limits.minMemoryMapAlignment, (unsigned long long) p->limits.nonCoherentAtomSize, p->limits.timestampPeriod);

        uint32_t en = 0;
        if (vkEnumerateDeviceExtensionProperties) vkEnumerateDeviceExtensionProperties(pd, NULL, &en, NULL);
        VkExtensionProperties *de = en ? calloc(en, sizeof(*de)) : NULL;
        if (de && vkEnumerateDeviceExtensionProperties) vkEnumerateDeviceExtensionProperties(pd, NULL, &en, de);
        put(&b, "\n[device extensions] %u\n", en);
        for (uint32_t i = 0; de && i < en; i++) put(&b, "%s %u\n", de[i].extensionName, de[i].specVersion);
        free(de);

        VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        if (p->apiVersion >= VK_API_VERSION_1_2) { f2.pNext = &f11; f11.pNext = &f12; }
        if (p->apiVersion >= VK_API_VERSION_1_3) f12.pNext = &f13;
        if (vkGetPhysicalDeviceFeatures2) vkGetPhysicalDeviceFeatures2(pd, &f2);
        bools(&b, "features 1.0", CORE, sizeof(CORE) / sizeof(*CORE), (const VkBool32 *) &f2.features);
        if (p->apiVersion >= VK_API_VERSION_1_2) {
            bools(&b, "features 1.1", V11, sizeof(V11) / sizeof(*V11), &f11.storageBuffer16BitAccess);
            bools(&b, "features 1.2", V12, sizeof(V12) / sizeof(*V12), &f12.samplerMirrorClampToEdge);
        }
        if (p->apiVersion >= VK_API_VERSION_1_3)
            bools(&b, "features 1.3", V13, sizeof(V13) / sizeof(*V13), &f13.robustImageAccess);

        static const char *const MEMPROP[] = { "DEVICE_LOCAL", "HOST_VISIBLE", "HOST_COHERENT", "HOST_CACHED", "LAZILY_ALLOCATED", "PROTECTED" };
        if (vkGetPhysicalDeviceMemoryProperties) {
            VkPhysicalDeviceMemoryProperties m;
            vkGetPhysicalDeviceMemoryProperties(pd, &m);
            put(&b, "\n[memory]\n");
            for (uint32_t i = 0; i < m.memoryHeapCount; i++)
                put(&b, "heap %u: %llu MB%s\n", i, (unsigned long long) (m.memoryHeaps[i].size >> 20),
                    (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " DEVICE_LOCAL" : "");
            for (uint32_t i = 0; i < m.memoryTypeCount; i++) {
                put(&b, "type %u: heap %u ", i, m.memoryTypes[i].heapIndex);
                flags(&b, m.memoryTypes[i].propertyFlags, MEMPROP, 6);
                put(&b, "\n");
            }
        }
        static const char *const QUEUE[] = { "GRAPHICS", "COMPUTE", "TRANSFER", "SPARSE", "PROTECTED" };
        if (vkGetPhysicalDeviceQueueFamilyProperties) {
            VkQueueFamilyProperties q[16];
            uint32_t qn = 16;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, q);
            put(&b, "\n[queues]\n");
            for (uint32_t i = 0; i < qn; i++) {
                put(&b, "family %u: %u queues ", i, q[i].queueCount);
                flags(&b, q[i].queueFlags, QUEUE, 5);
                put(&b, " timestampValidBits=%u\n", q[i].timestampValidBits);
            }
        }
        static const struct { VkExternalMemoryHandleTypeFlagBits type; const char *name; } HANDLES[] = {
            { VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, "OPAQUE_FD" },
            { VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, "DMA_BUF" },
            { VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID, "ANDROID_HARDWARE_BUFFER" },
            { VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, "HOST_ALLOCATION" },
        };
        static const char *const EXTFEAT[] = { "DEDICATED_ONLY", "EXPORTABLE", "IMPORTABLE" };
        if (vkGetPhysicalDeviceExternalBufferProperties) {
            put(&b, "\n[external buffers] (usage transfer src/dst, storage, uniform, vertex, index)\n");
            for (size_t h = 0; h < sizeof(HANDLES) / sizeof(*HANDLES); h++) {
                VkPhysicalDeviceExternalBufferInfo info = {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    .handleType = HANDLES[h].type,
                };
                VkExternalBufferProperties props = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
                vkGetPhysicalDeviceExternalBufferProperties(pd, &info, &props);
                put(&b, "%s: ", HANDLES[h].name);
                flags(&b, props.externalMemoryProperties.externalMemoryFeatures, EXTFEAT, 3);
                put(&b, " compatible=0x%x\n", props.externalMemoryProperties.compatibleHandleTypes);
            }
        }
        static const struct { VkFormat format; const char *name; } FORMATS[] = {
            { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, "BC1" }, { VK_FORMAT_BC3_UNORM_BLOCK, "BC3" },
            { VK_FORMAT_BC5_UNORM_BLOCK, "BC5" }, { VK_FORMAT_BC7_UNORM_BLOCK, "BC7" },
            { VK_FORMAT_D24_UNORM_S8_UINT, "D24S8" }, { VK_FORMAT_D32_SFLOAT_S8_UINT, "D32S8" },
            { VK_FORMAT_B8G8R8A8_UNORM, "BGRA8" }, { VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16F" },
        };
        if (vkGetPhysicalDeviceFormatProperties) {
            put(&b, "\n[formats] optimal tiling features (hex)\n");
            for (size_t i = 0; i < sizeof(FORMATS) / sizeof(*FORMATS); i++) {
                VkFormatProperties fp;
                vkGetPhysicalDeviceFormatProperties(pd, FORMATS[i].format, &fp);
                put(&b, "%s=0x%x buffer=0x%x\n", FORMATS[i].name, fp.optimalTilingFeatures, fp.bufferFeatures);
            }
        }
    }
    if (count > 0) {
        /* Which linear row pitches the driver accepts for a dma_buf image with an explicit
         * DRM_FORMAT_MOD_LINEAR layout: what a compositor importing other programs'
         * buffers (gamescope through the Venus bridge) is held to. */
        GET2(vkCreateDevice); GET2(vkDestroyDevice); GET2(vkCreateImage); GET2(vkDestroyImage);
        GET2(vkGetImageSubresourceLayout);
        float prio = 1.0f;
        VkDeviceQueueCreateInfo q = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
        const char *dexts[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier", "VK_EXT_queue_family_foreign" };
        VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &q,
                                   .enabledExtensionCount = 4, .ppEnabledExtensionNames = dexts };
        VkDevice dev = VK_NULL_HANDLE;
        VkResult dr = vkCreateDevice ? vkCreateDevice(devices[0], &dci, NULL, &dev) : VK_ERROR_INITIALIZATION_FAILED;
        put(&b, "\n[linear image pitch probe] vkCreateDevice=%d\n", dr);
        if (dr == VK_SUCCESS) {
            static const uint32_t widths[] = { 1602, 1600, 1280, 333 };
            for (size_t w = 0; w < sizeof(widths) / sizeof(*widths); w++) {
                VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                    .format = VK_FORMAT_B8G8R8A8_UNORM, .extent = { widths[w], 720, 1 }, .mipLevels = 1, .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
                    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
                VkImage img = VK_NULL_HANDLE;
                VkResult r1 = vkCreateImage(dev, &ici, NULL, &img);
                VkSubresourceLayout lay = { 0 };
                VkImageSubresource sub = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT };
                if (r1 == VK_SUCCESS) { vkGetImageSubresourceLayout(dev, img, &sub, &lay); vkDestroyImage(dev, img, NULL); }
                put(&b, "width %u: linear image -> %d, driver rowPitch %llu\n", widths[w], r1, (unsigned long long) lay.rowPitch);
                const uint32_t tight = widths[w] * 4;
                const uint32_t pitches[] = { tight, (tight + 15) & ~15u, (tight + 63) & ~63u, (tight + 127) & ~127u, (tight + 255) & ~255u, (uint32_t) lay.rowPitch };
                for (size_t k = 0; k < sizeof(pitches) / sizeof(*pitches); k++) {
                    if (!pitches[k]) continue;
                    VkSubresourceLayout pl = { .offset = 0, .rowPitch = pitches[k] };
                    VkImageDrmFormatModifierExplicitCreateInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
                        .drmFormatModifier = 0, .drmFormatModifierPlaneCount = 1, .pPlaneLayouts = &pl };
                    VkExternalMemoryImageCreateInfo ei = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, .pNext = &mi,
                        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
                    VkImageCreateInfo ici2 = ici;
                    ici2.pNext = &ei;
                    ici2.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
                    VkImage img2 = VK_NULL_HANDLE;
                    VkResult r2 = vkCreateImage(dev, &ici2, NULL, &img2);
                    if (r2 == VK_SUCCESS) vkDestroyImage(dev, img2, NULL);
                    put(&b, "  explicit LINEAR dma_buf image, rowPitch %u (%%64=%u %%256=%u) -> %d\n", pitches[k], pitches[k] % 64, pitches[k] % 256, r2);
                }
            }
            vkDestroyDevice(dev, NULL);
        }
    }
probe_done:
    if (vkDestroyInstance) vkDestroyInstance(instance, NULL);
done:
    if (lib) dlclose(lib);
    jstring s = (*env)->NewStringUTF(env, b.p);
    free(b.p);
    return s;
}
