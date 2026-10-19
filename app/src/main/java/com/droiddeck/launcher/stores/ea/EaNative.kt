package com.droiddeck.launcher.stores.ea

import android.content.Context
import com.droiddeck.launcher.runtime.GuestCommand
import com.droiddeck.launcher.runtime.LinuxRuntime
import org.json.JSONObject
import java.io.File

/** droiddeck-ea (Maxima-based EA client) in the guest; it answers with "@ea {json}" lines. */
object EaNative {
    const val DIR = "/usr/local/lib/droiddeck-ea"
    const val PROGRAM = "$DIR/droiddeck-ea"
    const val ENGINE_MISSING = "EA app: the EA client is not in this build"
    private const val PREFIX = "@ea "

    private val FILES = listOf("droiddeck-ea", "maxima-bootstrap", "droiddeck-ea-wine")

    /**
     * True when the client is in the Linux system. The session normally copies it there when it
     * starts; the store tab can be opened before any session ran on this version, so it copies
     * the client over itself when the copy is missing or differs from the one in the app.
     */
    @Synchronized
    fun ready(context: Context): Boolean {
        val app = context.applicationContext
        val root = LinuxRuntime.rootDir(app)
        if (!root.isDirectory) return false
        for (name in FILES) {
            val asset = "linuxfs${DIR}/$name"
            val target = File(root, "${DIR.removePrefix("/")}/$name")
            val size = runCatching { app.assets.openFd(asset).use { it.length } }.getOrNull()
            if (size != null && target.isFile && target.length() == size) continue
            if (size == null && target.isFile) continue
            val staged = File(target.parentFile, "${target.name}.staged")
            try {
                target.parentFile?.mkdirs()
                app.assets.open(asset).use { input -> staged.outputStream().use { input.copyTo(it) } }
                if (!(staged.setExecutable(true, false) && staged.renameTo(target))) staged.delete()
            } catch (e: Exception) {
                android.util.Log.w("EaNative", "could not copy $name", e)
                staged.delete()
            }
        }
        return File(root, PROGRAM.removePrefix("/")).isFile
    }

    fun parse(line: String): JSONObject? =
        if (line.startsWith(PREFIX)) runCatching { JSONObject(line.substring(PREFIX.length)) }.getOrNull() else null

    class Outcome(val code: Int, val results: List<JSONObject>) {
        val last: JSONObject? get() = results.lastOrNull()
        val error: String? get() = results.lastOrNull { it.has("error") }?.optString("error")
        fun first(key: String): JSONObject? = results.firstOrNull { it.has(key) }
    }

    fun run(context: Context, args: List<String>, onResult: (JSONObject) -> Unit = {}): Outcome {
        val results = ArrayList<JSONObject>()
        val code = GuestCommand.run(context.applicationContext, listOf(PROGRAM) + args, logName = "ea") { line ->
            parse(line)?.let { results += it; onResult(it) }
        }
        return Outcome(code, results)
    }
}
