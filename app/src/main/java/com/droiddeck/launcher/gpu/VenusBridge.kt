package com.droiddeck.launcher.gpu

import android.content.Context
import android.util.Log
import com.droiddeck.launcher.core.HostProcess
import org.json.JSONObject
import java.io.File

/**
 * The Venus Vulkan bridge: when the selected Linux driver is Mesa's venus ICD, a vtest server
 * (virglrenderer, shipped in the APK as libvenus_server.so) runs on the Android side and executes
 * the Linux side's Vulkan on Android's own driver. The guest ICD reaches it through a Unix socket
 * bound into the runtime at vtest's default path.
 */
object VenusBridge {
    private const val TAG = "VenusBridge"
    const val GUEST_SOCKET = "/tmp/.virgl_test"
    private const val SERVER = "libvenus_server.so"
    private const val RENDER_SERVER = "libvenus_render_server.so"

    @Volatile private var pid = -1

    val running: Boolean get() = pid > 0

    fun socket(context: Context) = File(context.filesDir, "venus/vtest.sock")

    /** Whether the imported Linux driver [id] is the venus ICD (its zip shipped libvulkan_virtio.so). */
    fun isVenus(context: Context, id: String?): Boolean {
        if (id.isNullOrEmpty()) return false
        val meta = File(LinuxVulkanDriverManager(context).getDriverDir(id), LinuxVulkanDriverManager.META_NAME)
        return runCatching {
            val json = JSONObject(meta.readText())
            json.optString("sourceLibraryName") == "libvulkan_virtio.so" || json.optString("backend") == "venus-vtest"
        }.getOrDefault(false)
    }

    /** Starts the server and waits for its socket; false if it is not in this APK or did not come up. */
    @Synchronized
    fun start(context: Context, logFile: File?): Boolean {
        stop()
        val dir = context.applicationInfo.nativeLibraryDir
        val server = File(dir, SERVER)
        if (!server.isFile) {
            Log.w(TAG, "$SERVER is not in this APK; the venus driver cannot reach Android's Vulkan")
            return false
        }
        val sock = socket(context)
        sock.parentFile?.mkdirs()
        sock.delete()
        val log = logFile?.let { runCatching { it.parentFile?.mkdirs(); it.printWriter() }.getOrNull() }
        val env = arrayOf(
            "LD_LIBRARY_PATH=$dir",
            "RENDER_SERVER_EXEC_PATH=" + File(dir, RENDER_SERVER).path,
            "TMPDIR=" + context.cacheDir.path,
            // Memory the Linux side maps comes from a dma-heap. Its CPU mapping is never
            // synced against the GPU (venus does no dma-buf cache maintenance), so with the
            // cached heap the Linux side read stale GPU-written values: semaphore feedback
            // never changed and gamescope waited forever. The uncached heap has no such cache.
            "VKR_DMA_HEAP=" + (System.getenv("DROIDDECK_VENUS_HEAP") ?: "/dev/dma_heap/system-uncached"),
        )
        pid = HostProcess.start(
            "${server.path} --venus --no-virgl --multi-clients --socket-path ${sock.path}",
            env, context.filesDir,
            { status -> Log.i(TAG, "server exited with $status"); synchronized(this) { pid = -1 }; log?.apply { println("== server exited with $status"); flush() } },
            { line -> Log.i(TAG, line); log?.apply { println(line); flush() } },
        )
        if (pid <= 0) return false
        val deadline = System.currentTimeMillis() + 3000
        while (!sock.exists() && System.currentTimeMillis() < deadline && pid > 0) Thread.sleep(20)
        val up = sock.exists()
        Log.i(TAG, if (up) "server pid $pid listening on ${sock.path}" else "server pid $pid did not create ${sock.path}")
        return up
    }

    @Synchronized
    fun stop() {
        if (pid > 0) {
            Log.i(TAG, "stopping server pid $pid")
            runCatching { android.os.Process.killProcess(pid) }
            pid = -1
        }
    }
}
