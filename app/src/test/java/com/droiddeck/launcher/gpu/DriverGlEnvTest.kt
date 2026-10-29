package com.droiddeck.launcher.gpu

import android.content.Context
import android.net.Uri
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

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

    @Test fun driverEnvIsReadAtTheDriverRoot() {
        // The zip root env file: the same syntax as the GL part's, resolved against the driver
        // directory rather than gl/. This is what lets the driver carry its own settings.
        val out = LinuxVulkanDriverManager.parseGlEnv(
            "DROIDDECK_VN_SYNC_PRESENT=1\nBL_MALI_REFRESH=120\n# comment\n",
            "/d/venus", listOf("HOME=/root"))
        assertEquals(listOf("DROIDDECK_VN_SYNC_PRESENT=1", "BL_MALI_REFRESH=120"), out)
    }

    @Test fun driverEnvIgnoresUnparsableLines() {
        val out = LinuxVulkanDriverManager.parseGlEnv(
            "bad key=1\n=nothing\nDROIDDECK_VN_BC_GPU=1\n", "/d/venus", emptyList())
        assertEquals(listOf("DROIDDECK_VN_BC_GPU=1"), out)
    }

    @Test fun onlyPlainFileNames() {
        assertTrue(LinuxVulkanDriverManager.isSafeFileName("libGLX_droiddeck.so.0"))
        assertTrue(LinuxVulkanDriverManager.isSafeFileName("50_droiddeck.json"))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName(".."))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName("a/b"))
        assertFalse(LinuxVulkanDriverManager.isSafeFileName(""))
    }

    /** A driver zip whose root carries the driver's own env file, as build-guest.sh writes it. */
    private fun installDriverZip(env: String?): Pair<LinuxVulkanDriverManager, String> {
        val context: Context = RuntimeEnvironment.getApplication()
        File(context.filesDir, LinuxVulkanDriverManager.DIR_NAME).deleteRecursively()
        val lib = ByteArray(64).also {
            byteArrayOf(0x7f, 'E'.code.toByte(), 'L'.code.toByte(), 'F'.code.toByte(), 2, 1).copyInto(it)
            it[18] = 0xB7.toByte()
        } + "libc.so.6".toByteArray() + ByteArray(1)
        val zip = File.createTempFile("venus", ".zip", context.cacheDir)
        ZipOutputStream(zip.outputStream()).use { z ->
            fun put(name: String, data: ByteArray) { z.putNextEntry(ZipEntry(name)); z.write(data); z.closeEntry() }
            put("libvulkan_panvk.so", lib)
            put("virtio_icd.aarch64.json", JSONObject().put("ICD", JSONObject().put("api_version", "1.4.303")).toString().toByteArray())
            put("meta.json", JSONObject().put("name", "DroidDeck-venus-bridge").put("backend", "venus-vtest").toString().toByteArray())
            if (env != null) put("env", env.toByteArray())
        }
        val manager = LinuxVulkanDriverManager(context)
        return manager to manager.installDriver(Uri.fromFile(zip), "venus.zip")
    }

    @Test fun aDriverCarriesItsOwnSettings() {
        // The point of the root env file: the driver states the settings it needs, so nothing on
        // the phone is edited to change one.
        val (manager, id) = installDriverZip("DROIDDECK_VN_SYNC_PRESENT=1\nDROIDDECK_VN_BC_GPU=1\n")
        assertEquals(listOf("DROIDDECK_VN_SYNC_PRESENT=1", "DROIDDECK_VN_BC_GPU=1"),
            manager.driverEnvironment(id, listOf("HOME=/root")))
    }

    @Test fun aDriverWithoutOneAddsNothing() {
        val (manager, id) = installDriverZip(null)
        assertTrue(manager.driverEnvironment(id, listOf("HOME=/root")).isEmpty())
    }
}
