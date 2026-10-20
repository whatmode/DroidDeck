package com.droiddeck.launcher.session

import android.content.Context
import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/**
 * Where the app's storage goes: every folder at least [MIN] large, down to [DEPTH] levels under
 * the app's own folders, for the shared logs zip. Symbolic links are not followed, so nothing is
 * counted twice through a link; hard links are counted where they are found.
 */
object DiskUsage {
    private const val DEPTH = 6
    private const val MIN = 150L * 1024 * 1024

    fun report(context: Context, out: File): File? = runCatching {
        val roots = listOfNotNull(
            context.filesDir.parentFile?.let { "app data" to it },
            context.getExternalFilesDir(null)?.parentFile?.let { "app external" to it },
        )
        val text = StringBuilder()
        text.append("DroidDeck storage, folders of ${MIN / (1024 * 1024)} MB or more\n")
        text.append("phone free: ${gb(context.filesDir.usableSpace)}\n\n")
        for ((label, root) in roots) {
            if (!root.isDirectory) continue
            val sizes = HashMap<Path, Long>()
            val base = root.toPath()
            Files.walkFileTree(base, object : SimpleFileVisitor<Path>() {
                override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                    if (attrs.isRegularFile) {
                        var dir = file.parent
                        val size = attrs.size()
                        while (dir != null && dir.startsWith(base)) {
                            if (base.relativize(dir).nameCount <= DEPTH) sizes.merge(dir, size, Long::plus)
                            if (dir == base) break
                            dir = dir.parent
                        }
                    }
                    return FileVisitResult.CONTINUE
                }
                override fun visitFileFailed(file: Path, exc: IOException): FileVisitResult = FileVisitResult.CONTINUE
            })
            text.append("== $label (${root.path}): ${gb(sizes[base] ?: 0)}\n")
            sizes.filter { (p, s) -> p != base && s >= MIN }.toSortedMap(compareBy { base.relativize(it).toString() })
                .forEach { (p, s) ->
                    val rel = base.relativize(p)
                    text.append("  ".repeat(rel.nameCount - 1)).append(String.format("%9s  ", gb(s))).append(rel.fileName).append('\n')
                }
            text.append('\n')
        }
        File(out, "disk-usage.txt").apply { writeText(text.toString()) }
    }.getOrNull()

    private fun gb(bytes: Long): String = if (bytes >= 1L shl 30) String.format("%.2f GB", bytes / 1073741824.0)
        else String.format("%.0f MB", bytes / 1048576.0)
}
