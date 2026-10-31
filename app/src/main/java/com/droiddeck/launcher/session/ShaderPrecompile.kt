package com.droiddeck.launcher.session

import android.content.Context
import com.droiddeck.launcher.runtime.LinuxRuntime
import org.json.JSONObject
import java.io.File

/**
 * The launched game's shaders being compiled before it starts (the guest's
 * droiddeck-shader-precompile, run by droiddeck-game-env on the Venus bridge).
 *
 * The guest writes <session root>/shader-precompile.json while it runs -- the session root is the
 * same path on both sides (BL_LAUNCH_DIR) -- and removes it when done. Skip writes
 * shader-precompile.skip, which stops the replay; the game then starts and compiles as it goes.
 */
object ShaderPrecompile {
    const val FILE = "shader-precompile.json"
    const val SKIP = "shader-precompile.skip"

    data class Progress(
        val game: String,
        val state: String,
        val done: Int,
        val total: Int,
        val etaSeconds: Int,
    ) {
        val fraction: Float get() = if (total > 0) (done.toFloat() / total).coerceIn(0f, 1f) else 0f
        val finished: Boolean get() = state == "done" || state == "skipped" || state == "failed"
    }

    fun file(context: Context) = File(LinuxRuntime.sessionRoot(context), FILE)

    fun read(context: Context): Progress? = parse(file(context))

    fun parse(file: File): Progress? {
        if (!file.isFile) return null
        return try {
            parse(file.readText())
        } catch (e: Exception) {
            null
        }
    }

    fun parse(text: String): Progress? = try {
        val json = JSONObject(text)
        Progress(
            game = json.optString("game", ""),
            state = json.optString("state", "compiling"),
            done = json.optInt("done", 0),
            total = json.optInt("total", 0),
            etaSeconds = json.optInt("eta", -1),
        )
    } catch (e: Exception) {
        null
    }

    fun skip(context: Context) {
        try {
            File(LinuxRuntime.sessionRoot(context), SKIP).writeText("")
        } catch (e: Exception) {
        }
    }
}
