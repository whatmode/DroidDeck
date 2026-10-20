package com.droiddeck.launcher.session

import android.content.Context
import android.util.Log
import com.droiddeck.launcher.runtime.LinuxRuntime
import java.io.File
import java.io.RandomAccessFile

/**
 * Everything a game leaves behind that says why it failed, gathered for the shared logs zip:
 * Proton's own log (PROTON_LOG=1), the game environment it ran with, and from each Proton prefix
 * used recently the game's own logs (Unity Player.log, Unreal Saved/Logs, crash reports, logs in
 * My Games and similar), Unity's saved settings, a list of crash dumps, plus logs a game writes
 * next to its program. Long files are cut to their end, where a failure is.
 */
object GameLogs {
    private const val TAG = "GameLogs"
    private const val RECENT_MS = 48L * 60 * 60 * 1000
    private const val PER_FILE = 4L * 1024 * 1024
    private const val TOTAL = 48L * 1024 * 1024
    private const val PER_PREFIX_FILES = 60
    private val PROTON_LOG = Regex("steam-\\d+\\.log")
    private val LOG_NAME = Regex("(?i).*\\.(log|txt|json|ini|cfg|xml)$")

    class Entry(val path: String, val file: File)

    fun collect(context: Context, out: File): List<Entry> {
        val since = System.currentTimeMillis() - RECENT_MS
        val result = ArrayList<Entry>()
        var budget = TOTAL
        fun add(path: String, src: File) {
            if (budget <= 0 || !src.isFile) return
            val file = if (src.length() <= PER_FILE) src else tail(src, out, path) ?: return
            budget -= file.length()
            result += Entry(path, file)
        }
        try {
            val home = File(LinuxRuntime.rootDir(context), "root")
            home.listFiles { f -> f.isFile && f.name.matches(PROTON_LOG) && f.lastModified() >= since }
                .orEmpty().sortedByDescending { it.lastModified() }
                .forEach { add("proton/${it.name}", it) }
            add("proton/game-environment.json", File(home, ".config/droiddeck/game-environment.json"))
            protonMedia(context, out)?.let { add("proton/media-stack.txt", it) }

            val steamapps = File(SessionPrefs.activeSteamRoot(context), "steamapps")
            val prefixes = File(steamapps, "compatdata").listFiles { f -> f.isDirectory }.orEmpty()
                .filter { usedSince(it, since) }
                .sortedByDescending { lastUse(it) }
            for (prefix in prefixes) {
                val id = prefix.name
                val user = File(prefix, "pfx/drive_c/users/steamuser")
                var files = 0
                val dumps = StringBuilder()
                for (area in listOf("AppData/LocalLow", "AppData/Local", "AppData/Roaming", "Documents", "Saved Games")) {
                    val root = File(user, area)
                    if (!root.isDirectory) continue
                    root.walkTopDown().maxDepth(7)
                        .onEnter { d -> d.name !in SKIP_DIRS }
                        .filter { it.isFile && it.lastModified() >= since }
                        .forEach { f ->
                            val rel = f.relativeTo(user).invariantSeparatorsPath
                            when {
                                f.name.endsWith(".dmp", true) || f.name.endsWith(".mdmp", true) ->
                                    dumps.append("${f.length()}\t${java.util.Date(f.lastModified())}\t$rel\n")
                                files < PER_PREFIX_FILES && looksLikeLog(f, rel) -> { add("games/$id/$rel", f); files++ }
                            }
                        }
                }
                if (dumps.isNotEmpty()) writeText(out, "games-$id-crash-dumps.txt", dumps.toString())?.let { add("games/$id/crash-dumps.txt", it) }
                unitySettings(prefix, user)?.let { text -> writeText(out, "games-$id-unity-settings.reg", text)?.let { add("games/$id/unity-settings.reg", it) } }
                installDir(steamapps, id)?.let { dir ->
                    dir.walkTopDown().maxDepth(2).filter { it.isFile && it.lastModified() >= since && it.name.matches(Regex("(?i).*\\.log$")) }
                        .take(20).forEach { add("games/$id/install/${it.relativeTo(dir).invariantSeparatorsPath}", it) }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "collecting game logs", e)
        }
        return result
    }

    /**
     * What each installed Proton decodes video with: its FFmpeg and GStreamer libav libraries, the
     * FFmpeg configure line and codec names found in them, and what Wine's winedmo links against.
     */
    private fun protonMedia(context: Context, out: File): File? = runCatching {
        val steam = SessionPrefs.activeSteamRoot(context)
        val root = LinuxRuntime.rootDir(context)
        // Proton builds, plus the system and Steam runtime folders a Wine library could load FFmpeg from.
        val tools: List<File> = (File(steam, "steamapps/common").listFiles()?.toList().orEmpty()
            .filter { it.isDirectory && (it.name.startsWith("Proton") || it.name.startsWith("SteamLinuxRuntime")) } +
            File(steam, "compatibilitytools.d").listFiles()?.toList().orEmpty().filter { it.isDirectory } +
            listOf(File(root, "usr/lib"), File(root, "usr/lib/aarch64-linux-gnu"), File(steam, "steamrtarm64")))
            .distinct().filter { it.isDirectory }
        val text = StringBuilder("Proton media stack\n")
        val wanted = Regex("lib(avcodec|avformat|avutil|swresample|swscale|avfilter|dav1d|vpx|openh264|x264)\\.so.*|winedmo\\.so|libgstlibav\\.so|libgst(videoparsersbad|isomp4|vpx|dav1d|openh264|v4l2codecs|video4linux2)\\.so")
        for (tool in tools) {
            text.append("\n== ${tool.name}\n")
            val depth = if (tool.startsWith(root) && !tool.startsWith(steam)) 2 else 7
            val files = tool.walkTopDown().maxDepth(depth).filter { it.name.matches(wanted) }.toList()
            if (files.isEmpty()) text.append("  (no media libraries found)\n")
            for (f in files.sortedBy { it.path }) {
                val rel = f.relativeTo(tool).invariantSeparatorsPath
                val link = runCatching { java.nio.file.Files.readSymbolicLink(f.toPath()).toString() }.getOrNull()
                text.append("  $rel  ${if (link != null) "-> $link" else "${f.length()} bytes"}\n")
                if (link != null || !f.isFile || f.length() > 64L * 1024 * 1024) continue
                val strings = printable(f)
                when {
                    f.name.startsWith("libavcodec") -> {
                        strings.filter { "--" in it && ("enable" in it || "disable" in it) }.distinct().take(3)
                            .forEach { text.append("    configure: $it\n") }
                        val codecs = strings.filter { it.matches(CODEC_NAME) }.distinct().sorted()
                        text.append("    codec names: ${codecs.joinToString(" ")}\n")
                    }
                    f.name.startsWith("libavformat") -> {
                        strings.filter { "--" in it && ("enable" in it || "disable" in it) }.distinct().take(1)
                            .forEach { text.append("    configure: $it\n") }
                    }
                    else -> {
                        val needed = strings.filter { it.matches(Regex("lib[A-Za-z0-9_+.-]+\\.so(\\.[0-9]+)*")) }.distinct()
                        text.append("    libraries named: ${needed.joinToString(" ")}\n")
                    }
                }
            }
        }
        File(out, "proton-media-stack.txt").apply { writeText(text.toString()) }
    }.getOrNull()

    private val CODEC_NAME = Regex("(h264|hevc|vp8|vp9|av1|aac|mp3|opus|vorbis|theora|mpeg4|mpeg2video|wmv[0-9]|wmav[0-9]|libdav1d|libvpx[a-z0-9-]*|libopenh264)(_[a-z0-9]+)?")

    /** Runs of printable ASCII at least 4 long, like strings(1). */
    private fun printable(f: File): List<String> {
        val result = ArrayList<String>()
        val run = StringBuilder()
        f.inputStream().buffered(1 shl 16).use { input ->
            while (true) {
                val b = input.read()
                if (b < 0) break
                if (b in 0x20..0x7e) { if (run.length < 4096) run.append(b.toChar()) }
                else { if (run.length >= 4) result += run.toString(); run.setLength(0) }
            }
        }
        if (run.length >= 4) result += run.toString()
        return result
    }

    private val SKIP_DIRS = setOf("dxvk", "vkd3d-proton", "mesa_shader_cache", "Cache", "GPUCache", "Code Cache", "ShaderCache", "D3DSCache", "INetCache", "Microsoft", "cef", "htmlcache")

    private fun looksLikeLog(f: File, rel: String): Boolean {
        val lower = rel.lowercase()
        if (f.name.equals("Player.log", true) || f.name.equals("Player-prev.log", true)) return true
        if (f.name.endsWith(".log", true)) return true
        if (!f.name.matches(LOG_NAME)) return false
        // Settings and text files only where games keep logs, crash reports or settings.
        return "log" in lower || "crash" in lower || "/saved/config/" in lower || "my games" in lower
    }

    private fun usedSince(prefix: File, since: Long): Boolean = lastUse(prefix) >= since

    private fun lastUse(prefix: File): Long = maxOf(
        File(prefix, "pfx/user.reg").lastModified(),
        File(prefix, "pfx/system.reg").lastModified(),
        File(prefix, "pfx").lastModified(),
    )

    /** Unity keeps its settings (PlayerPrefs) in HKCU\Software\<company>\<game>, named after its LocalLow folder. */
    private fun unitySettings(prefix: File, user: File): String? {
        val low = File(user, "AppData/LocalLow")
        val keys = low.listFiles { f -> f.isDirectory }.orEmpty().flatMap { company ->
            company.listFiles { f -> f.isDirectory }.orEmpty().map { "[Software\\\\${company.name}\\\\${it.name}]".lowercase() }
        }.filterNot { it.startsWith("[software\\\\unity") }
        if (keys.isEmpty()) return null
        val reg = File(prefix, "pfx/user.reg")
        if (!reg.isFile) return null
        val text = StringBuilder()
        var inside = false
        reg.forEachLine(Charsets.UTF_8) { line ->
            if (line.startsWith("[")) inside = keys.any { line.lowercase().startsWith(it) }
            if (inside && text.length < 256 * 1024) text.append(line).append('\n')
        }
        return text.takeIf { it.isNotEmpty() }?.toString()
    }

    private fun installDir(steamapps: File, id: String): File? {
        val manifest = File(steamapps, "appmanifest_$id.acf").takeIf { it.isFile } ?: return null
        val name = Regex("\"installdir\"\\s+\"([^\"]+)\"").find(manifest.readText())?.groupValues?.get(1) ?: return null
        return File(steamapps, "common/$name").takeIf { it.isDirectory }
    }

    private fun writeText(out: File, name: String, text: String): File? =
        runCatching { File(out, name.replace(Regex("[^A-Za-z0-9._-]"), "_")).apply { writeText(text) } }.getOrNull()

    private fun tail(src: File, out: File, path: String): File? = runCatching {
        val dst = File(out, "end-of-" + path.replace(Regex("[^A-Za-z0-9._-]"), "_"))
        RandomAccessFile(src, "r").use { raf ->
            val skip = raf.length() - PER_FILE
            raf.seek(skip)
            dst.outputStream().use { o ->
                o.write("[... first $skip bytes left out ...]\n".toByteArray())
                val buf = ByteArray(65536)
                while (true) { val n = raf.read(buf); if (n < 0) break; o.write(buf, 0, n) }
            }
        }
        dst
    }.getOrNull()
}
