package com.droiddeck.launcher.gpu

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LinuxVulkanDriverLibraryTest {
    @Test fun acceptsTurnip() {
        assertTrue(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_freedreno.so"))
        assertTrue(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_freedreno_v26.so"))
    }

    @Test fun acceptsPanVk() {
        assertTrue(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_panfrost.so"))
        assertTrue(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_panfrost.so.1"))
    }

    @Test fun rejectsOtherFiles() {
        assertFalse(LinuxVulkanDriverManager.isDriverLibrary("libvulkan.so"))
        assertFalse(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_lvp.so"))
        assertFalse(LinuxVulkanDriverManager.isDriverLibrary("libvulkan_panfrost.json"))
        assertFalse(LinuxVulkanDriverManager.isDriverLibrary("meta.json"))
    }
}
