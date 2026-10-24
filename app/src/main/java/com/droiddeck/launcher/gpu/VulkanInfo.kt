package com.droiddeck.launcher.gpu

import android.util.Log

object VulkanInfo {
    @Volatile private var cached: Map<String, String>? = null

    @JvmStatic private external fun nativeQuery(): String
    @JvmStatic private external fun nativeReport(): String

    /** Android's own Vulkan driver in full (extensions, features, memory, external memory), as a file in [out]. */
    fun report(out: java.io.File): java.io.File? = try {
        System.loadLibrary("deviceinfo")
        java.io.File(out, "android-vulkan.txt").apply { writeText(nativeReport()) }
    } catch (t: Throwable) {
        Log.w("VulkanInfo", "report failed", t)
        null
    }

    @Synchronized
    fun query(): Map<String, String> {
        cached?.let { return it }
        val text = try {
            System.loadLibrary("deviceinfo")
            nativeQuery()
        } catch (t: Throwable) {
            Log.w("VulkanInfo", "query failed", t)
            "error=" + (t.message ?: t.javaClass.simpleName)
        }
        val map = text.lineSequence().mapNotNull { line ->
            val eq = line.indexOf('=')
            if (eq <= 0) null else line.substring(0, eq) to line.substring(eq + 1)
        }.toMap(LinkedHashMap())
        cached = map
        return map
    }

    fun cachedOrNull(): Map<String, String>? = cached
}
