package com.droiddeck.launcher.gpu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class RuntimeDriverNameTest {
    @Test fun turnipPanvkAndLavapipeAreRuntimeDrivers() {
        assertTrue(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan_freedreno.so"))
        assertTrue(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan_panvk.so"))
        assertTrue(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan_panfrost.so"))
        assertTrue(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan_lvp.so"))
    }

    @Test fun otherFilesAreNot() {
        assertFalse(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan.so"))
        assertFalse(LinuxVulkanDriverManager.isRuntimeDriverName("libvulkan_panvk"))
        assertFalse(LinuxVulkanDriverManager.isRuntimeDriverName("panvk_icd.aarch64.json"))
        assertFalse(LinuxVulkanDriverManager.isRuntimeDriverName(null))
    }

    @Test fun apiVersionComesFromTheManifest() {
        assertEquals("1.4.335", LinuxVulkanDriverManager.apiVersionOf("""{"ICD":{"library_path":"x.so","api_version":"1.4.335"}}"""))
        assertNull(LinuxVulkanDriverManager.apiVersionOf("""{"ICD":{"library_path":"x.so"}}"""))
        assertNull(LinuxVulkanDriverManager.apiVersionOf("not json"))
    }
}
