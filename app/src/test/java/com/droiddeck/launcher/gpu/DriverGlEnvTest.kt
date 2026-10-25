package com.droiddeck.launcher.gpu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class DriverGlEnvTest {
    private val text = """
        # comment
        __GLX_VENDOR_LIBRARY_NAME=droiddeck
        __EGL_VENDOR_LIBRARY_FILENAMES=@GL@/50_droiddeck.json
        LD_LIBRARY_PATH+=@GL@

        bad key=1
        =nothing
        BL_DRIVER_GL=@GL@
    """.trimIndent()

    @Test fun setsAndSubstitutes() {
        val out = LinuxVulkanDriverManager.parseGlEnv(text, "/d/gl", listOf("HOME=/root"))
        assertEquals(listOf(
            "__GLX_VENDOR_LIBRARY_NAME=droiddeck",
            "__EGL_VENDOR_LIBRARY_FILENAMES=/d/gl/50_droiddeck.json",
            "LD_LIBRARY_PATH=/d/gl",
            "BL_DRIVER_GL=/d/gl",
        ), out)
    }

    @Test fun appendsToTheCurrentValue() {
        val out = LinuxVulkanDriverManager.parseGlEnv("LD_LIBRARY_PATH+=@GL@\nLD_LIBRARY_PATH+=/x", "/d/gl",
            listOf("LD_LIBRARY_PATH=/a", "LD_LIBRARY_PATH=/b"))
        assertEquals(listOf("LD_LIBRARY_PATH=/b:/d/gl", "LD_LIBRARY_PATH=/b:/d/gl:/x"), out)
    }

    @Test fun nothingWithoutAFile() {
        assertTrue(LinuxVulkanDriverManager.parseGlEnv(null, "/d/gl", emptyList()).isEmpty())
    }

    @Test fun onlyPlainFileNames() {
        assertTrue(LinuxVulkanDriverManager.isSafeFileName("libGLX_droiddeck.so.0"))
        assertTrue(LinuxVulkanDriverManager.isSafeFileName("50_droiddeck.json"))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName(".."))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName("a/b"))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName(""))
    }
}
