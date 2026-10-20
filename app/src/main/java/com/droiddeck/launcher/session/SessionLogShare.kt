package com.droiddeck.launcher.session

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.FileProvider
import com.droiddeck.launcher.R
import com.droiddeck.launcher.core.LogRedactor
import com.droiddeck.launcher.runtime.LinuxRuntime
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object SessionLogShare {
    private val main = Handler(Looper.getMainLooper())

    var progress by mutableStateOf<Float?>(null)
        private set

    fun prepare(context: Context, folder: () -> File?, done: (File?) -> Unit) {
        if (progress != null) return
        progress = 0f
        Thread({
            val zip = runCatching { folder()?.let { dir -> zipFolder(context, dir) { p -> main.post { if (progress != null) progress = p } } } }
                .onFailure { Log.w("SessionLogShare", "could not package session logs", it) }
                .getOrNull()
            main.post { progress = null; done(zip) }
        }, "share-logs").start()
    }

    fun latest(context: Context): File? = SessionPaths.sessionFolders(context).lastOrNull()


    fun zipFolder(context: Context, folder: File, onProgress: ((Float) -> Unit)? = null): File? {
        if (!folder.isDirectory) return null
        val files = folder.walkTopDown().filter { it.isFile }
            .filterNot { it.parentFile?.name == "steam" && it.length() > STEAM_LOG_MAX_BYTES }.toList()
        if (files.isEmpty()) return null
        val out = shareDir(context).apply { deleteRecursively(); mkdirs() }
        val zip = File(out, "DroidDeck-${folder.name}.zip")
        LogRedactor.learnFromRuntime(LinuxRuntime.rootDir(context))
        val live = liveSteamLogs(context, folder)
        val proton = if (File(folder, "proton").isDirectory) emptyList() else protonLogs(context, out)
        val logs = LinuxRuntime.logDir(context)
        val stores = recent(File(logs, STORES_DIR))
        val tools = recent(File(logs, SessionPaths.TOOLS_DIR))
        val total = (files + live + proton + stores + tools).sumOf { it.length() }.coerceAtLeast(1L)
        var done = 0L
        var shown = -1
        fun advance(f: File) {
            done += f.length()
            val percent = (done * 100 / total).toInt()
            if (percent != shown) { shown = percent; onProgress?.invoke(percent / 100f) }
        }
        ZipOutputStream(zip.outputStream().buffered()).use { z ->
            files.forEach { f -> addEntry(z, folder.name + "/" + f.relativeTo(folder).invariantSeparatorsPath, f); advance(f) }
            live.forEach { f -> addEntry(z, folder.name + "/steam/" + f.name, f); advance(f) }
            proton.forEach { f -> addEntry(z, folder.name + "/proton/" + f.name, f); advance(f) }
            stores.forEach { f -> addEntry(z, "$STORES_DIR/" + f.name, f); advance(f) }
            tools.forEach { f -> addEntry(z, SessionPaths.TOOLS_DIR + "/" + f.name, f); advance(f) }
        }
        return zip
    }

    private fun addEntry(z: ZipOutputStream, name: String, f: File) {
        z.putNextEntry(ZipEntry(name))
        if (LogRedactor.isText(f)) {
            val w = z.bufferedWriter()
            LogRedactor.scrubForShare(f, w)
            w.flush()
        } else {
            f.inputStream().use { it.copyTo(z) }
        }
        z.closeEntry()
    }

    private fun liveSteamLogs(context: Context, folder: File): List<File> {
        if (folder != SessionPaths.current() || File(folder, "steam").exists()) return emptyList()
        val logs = File(SessionPrefs.activeSteamRoot(context), "logs")
        return logs.listFiles()?.filter { it.isFile && it.length() <= STEAM_LOG_MAX_BYTES }.orEmpty()
    }

    /**
     * Proton's own logs (PROTON_LOG=1: steam-<appid>.log in the guest's home) from the last hours,
     * plus the game environment they ran with. A long log is cut to its end.
     */
    private fun protonLogs(context: Context, out: File): List<File> {
        val home = File(LinuxRuntime.rootDir(context), "root")
        val since = System.currentTimeMillis() - 6L * 60 * 60 * 1000
        val found = home.listFiles { f -> f.isFile && f.name.matches(Regex("steam-\\d+\\.log")) && f.lastModified() >= since }.orEmpty().toMutableList()
        File(home, ".config/droiddeck/game-environment.json").takeIf { it.isFile }?.let { found += it }
        found += unityLogs(context, out, since)
        val keep = 8L * 1024 * 1024
        return found.mapNotNull { src ->
            if (src.length() <= keep) return@mapNotNull src
            runCatching {
                val tail = File(out, "end-of-" + src.name)
                java.io.RandomAccessFile(src, "r").use { raf ->
                    raf.seek(raf.length() - keep)
                    tail.outputStream().use { o ->
                        o.write("[... first ${raf.length() - keep} bytes left out ...]\n".toByteArray())
                        val buf = ByteArray(65536)
                        while (true) { val n = raf.read(buf); if (n < 0) break; o.write(buf, 0, n) }
                    }
                }
                tail
            }.getOrNull()
        }
    }

    /** Unity games write their own Player.log under AppData/LocalLow/<company>/<game>/ in the prefix. */
    private fun unityLogs(context: Context, out: File, since: Long): List<File> {
        val compat = File(SessionPrefs.activeSteamRoot(context), "steamapps/compatdata")
        val result = ArrayList<File>()
        compat.listFiles { f -> f.isDirectory }.orEmpty().forEach { game ->
            val low = File(game, "pfx/drive_c/users/steamuser/AppData/LocalLow")
            low.listFiles { f -> f.isDirectory }.orEmpty().forEach { company ->
                company.listFiles { f -> f.isDirectory }.orEmpty().forEach { title ->
                    listOf("Player.log", "Player-prev.log").map { File(title, it) }
                        .filter { it.isFile && it.lastModified() >= since - 48L * 60 * 60 * 1000 }
                        .forEach { log ->
                            runCatching {
                                val copy = File(out, "${game.name}-${title.name}-${log.name}".replace(Regex("[^A-Za-z0-9._-]"), "_"))
                                log.copyTo(copy, overwrite = true)
                                result += copy
                            }
                        }
                }
            }
        }
        return result
    }

    private const val STEAM_LOG_MAX_BYTES = 8L * 1024 * 1024

    const val STORES_DIR = "stores"
    private const val RECENT_DAYS = 7

    private fun shareDir(context: Context): File = File(context.cacheDir, "share")

    private fun recent(dir: File): List<File> {
        val since = System.currentTimeMillis() - RECENT_DAYS * 86_400_000L
        return dir.listFiles { f -> f.isFile && f.lastModified() >= since }?.sortedBy { it.name }.orEmpty()
    }

    fun clear(context: Context) {
        shareDir(context).deleteRecursively()
        File(context.cacheDir, "shared-logs").deleteRecursively()
    }

    fun shareIntent(context: Context, zip: File): Intent {
        val uri = FileProvider.getUriForFile(context, context.packageName + ".logs", zip)
        val send = Intent(Intent.ACTION_SEND)
            .setType("application/zip")
            .putExtra(Intent.EXTRA_STREAM, uri)
            .putExtra(Intent.EXTRA_SUBJECT, zip.nameWithoutExtension)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        send.clipData = ClipData.newRawUri(zip.name, uri)
        return Intent.createChooser(send, context.getString(R.string.logshare_chooser)).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
}
