package com.droiddeck.launcher.core

import com.droiddeck.launcher.session.SessionPrefs

object GameEnvironment {
    data class Config(
        val shared: Map<String, String?> = emptyMap(),
    ) {
        fun entries() = shared

        fun withEntries(entries: Map<String, String?>): Config = copy(shared = entries.toMap())
    }

    private val namePattern = Regex("[A-Za-z_][A-Za-z0-9_]*")

    fun validName(name: String) = namePattern.matches(name)
    fun validValue(value: String) = '\u0000' !in value && value.length <= 8192

    fun defaults(preset: String, forceSsbs: Boolean = false): Map<String, String?> = linkedMapOf<String, String?>(
        "MESA_SHADER_CACHE_DISABLE" to "false",
        "VKD3D_FEATURE_LEVEL" to "12_2",
        "VKD3D_SHADER_MODEL" to "6_9",
        "DOTNET_GCRegionRange" to "800000000",
        "DOTNET_EnableWriteXorExecute" to "0",
    ).apply {
        FexPreset.env(preset).forEach { put(it.substringBefore('='), it.substringAfter('=')) }
        if (forceSsbs) put(FORCE_SSBS, "1")
    }

    const val FORCE_SSBS = "DROIDDECK_FORCE_SSBS"

    /* The per-game DXVK option editor that wrote `dxvk.maxFrameRate = 60` was removed, but the
     * value it saved survives in this file and is republished to every launch, and nothing in the
     * UI can change it any more. A limiter pinned to the panel rate is also the worst case for
     * DXVK's limiter, which yields to vsync and then lands on every second vblank: a flat 2x
     * vblank frame time (33.37 ms on a 60 Hz panel) that reads like a vsync or driver fault
     * rather than a limiter. The session's own frame limiter is the supported knob, so a stale
     * one is dropped here. Only the limiter key goes; other DXVK options are kept. */
    private val FRAME_LIMITER = Regex("""(?:dxvk|dxgi|d3d9)\.maxFrameRate\s*=\s*[^;]*;?""")

    fun withoutFrameLimiter(dxvk: String): String =
        dxvk.replace(FRAME_LIMITER, "")
            .split(';')
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .joinToString("; ")

    private fun withoutFrameLimiter(shared: Map<String, String?>): Map<String, String?> {
        val dxvk = shared["DXVK_CONFIG"] ?: return shared
        val cleaned = withoutFrameLimiter(dxvk)
        if (cleaned == dxvk) return shared
        val entries = shared.toMutableMap()
        if (cleaned.isEmpty()) entries.remove("DXVK_CONFIG") else entries["DXVK_CONFIG"] = cleaned
        return entries
    }

    fun withoutFrameLimiter(config: Config): Config =
        config.copy(shared = withoutFrameLimiter(config.shared))

    fun effective(config: Config, preset: String, forceSsbs: Boolean = false): Map<String, String?> =
        defaults(preset, forceSsbs) + config.shared

    fun withValue(config: Config, name: String, value: String?): Config {
        require(validName(name))
        val entries = config.shared.toMutableMap()
        if (value == null) entries.remove(name) else entries[name] = value
        return config.withEntries(entries)
    }

    val syncBackends = listOf(SessionPrefs.SYNC_NTSYNC, SessionPrefs.SYNC_FSYNC, SessionPrefs.SYNC_ESYNC, SessionPrefs.SYNC_WINESERVER)

    val syncKeys = listOf("BL_SYNC", "BL_FSYNC_FIRST", "BL_SYNC_FALLBACK", "PROTON_NO_ESYNC", "PROTON_NO_FSYNC", "PROTON_NO_NTSYNC", "WINEESYNC")

    fun syncEnvironment(backend: String): Map<String, String> {
        require(backend in syncBackends) { "unknown sync $backend" }
        return buildMap {
            put("BL_SYNC", if (backend == SessionPrefs.SYNC_NTSYNC) "1" else "0")
            put("BL_FSYNC_FIRST", if (backend == SessionPrefs.SYNC_FSYNC) "1" else "0")
            put("BL_SYNC_FALLBACK", if (backend == SessionPrefs.SYNC_WINESERVER) "0" else "1")
            if (backend == SessionPrefs.SYNC_FSYNC) put("PROTON_NO_ESYNC", "1")
        }
    }
}
