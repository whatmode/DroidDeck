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
