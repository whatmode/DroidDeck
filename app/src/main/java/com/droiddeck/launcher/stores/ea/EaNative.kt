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

    fun ready(context: Context): Boolean = File(LinuxRuntime.rootDir(context.applicationContext), PROGRAM.removePrefix("/")).exists()

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
