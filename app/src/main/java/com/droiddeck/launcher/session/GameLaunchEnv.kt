package com.droiddeck.launcher.session

import android.content.Context
import com.droiddeck.launcher.core.GameEnvironment

/**
 * Environment variables set for one game, keyed like the no-Steam setting (the Steam app id, or a
 * non-Steam shortcut's id). Stored as the text the user typed ("NAME=value NAME2=value"), applied
 * by droiddeck-game-env when that game starts, so nothing has to be typed into Steam's launch
 * options.
 */
object GameLaunchEnv {
    private const val PREFS = "game_launch_env"
    private val ID = Regex("[1-9][0-9]*")

    /** Ready-made switches offered in the game's card, as (variable, value). */
    val presets = listOf(
        "PROTON_USE_WINED3D" to "1",
        "PANVK_KBASE_USER_CACHE_SYNC" to "0",
        "PANVK_KBASE_DMA_HEAP" to "/dev/dma_heap/system-uncached",
    )

    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun text(context: Context, appId: String): String = prefs(context).getString(appId, "").orEmpty()

    fun setText(context: Context, appId: String, text: String) {
        require(ID.matches(appId))
        val clean = format(parse(text))
        prefs(context).edit().apply { if (clean.isEmpty()) remove(appId) else putString(appId, clean) }.apply()
        runCatching { GameEnvironmentStore.publish(context) }
    }

    fun entries(context: Context, appId: String): Map<String, String> = parse(text(context, appId))

    fun toggle(context: Context, appId: String, name: String, value: String) {
        val now = entries(context, appId).toMutableMap()
        if (now[name] == value) now.remove(name) else now[name] = value
        setText(context, appId, format(now))
    }

    fun all(context: Context): Map<String, Map<String, String>> =
        prefs(context).all.mapNotNull { (id, value) ->
            val parsed = (value as? String)?.let(::parse).orEmpty()
            if (ID.matches(id) && parsed.isNotEmpty()) id to parsed else null
        }.toMap()

    /** "A=1 B=two" into a map; anything that is not NAME=value is dropped. */
    fun parse(text: String): Map<String, String> {
        val out = LinkedHashMap<String, String>()
        for (token in text.trim().split(Regex("\\s+"))) {
            if (token.isEmpty() || token == "%command%") continue
            val name = token.substringBefore('=', "")
            val value = token.substringAfter('=', "")
            if (!token.contains('=') || !GameEnvironment.validName(name) || !GameEnvironment.validValue(value)) continue
            out[name] = value
        }
        return out
    }

    fun format(entries: Map<String, String>): String = entries.entries.joinToString(" ") { "${it.key}=${it.value}" }
}
