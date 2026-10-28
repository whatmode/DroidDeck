package com.droiddeck.launcher.session

import android.content.Context
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.droiddeck.launcher.R
import com.droiddeck.launcher.gpu.FrameGen
import com.droiddeck.launcher.gpu.Lossless
import com.droiddeck.launcher.wayland.WaylandCompositor
import java.io.File
import java.util.Locale
import java.util.concurrent.atomic.AtomicInteger

class PerfHud(context: Context) {
    var text by mutableStateOf("")
        private set

    private val context = context.applicationContext
    private val frames = AtomicInteger()
    private var lastTick = 0L
    private var running = false
    private val handler = Handler(Looper.getMainLooper())

    private val enabled: Boolean
        get() = overlayLevel() > SessionPrefs.OVERLAY_OFF
            && !File(Environment.getExternalStorageDirectory(), "Download/droiddeck-no-hud").exists()

    private fun overlayLevel(): Int = SessionPrefs.overlayLevel(context)

    var onPresentingWindowChanged: (() -> Unit)? = null
    private var lastWindow: String? = null

    private val listener = object : WaylandCompositor.GameListener {
        override fun onGameSurface(window: String?, gpuName: String?) {
            if (window != null && window != lastWindow) {
                lastWindow = window
                handler.post { onPresentingWindowChanged?.invoke() }
            }
        }
        override fun onGameFrame() { frames.incrementAndGet() }
    }

    private val tick = object : Runnable {
        override fun run() {
            if (!running) return
            val now = SystemClock.elapsedRealtime()
            val dt = (now - lastTick).coerceAtLeast(1L) / 1000f
            lastTick = now
            text = line(frames.getAndSet(0) / dt)
            handler.postDelayed(this, 1000)
        }
    }

    fun refresh() {
        if (enabled) start() else stop()
    }

    fun start() {
        if (!enabled || running) return
        running = true
        lastTick = SystemClock.elapsedRealtime()
        frames.set(0)
        WaylandCompositor.setGameListener(listener)
        handler.post(tick)
    }

    fun stop() {
        running = false
        WaylandCompositor.clearGameListener(listener)
        text = ""
    }

    private fun line(base: Float): String {
        val mode = FrameGen.mode(context)
        if (mode.engine == FrameGen.ENGINE_OFF) return fps(base) + detail()
        val lsfg = mode.engine == FrameGen.ENGINE_LSFG
        val name = if (lsfg) "LSFG" else "Win-FG"
        val label = FrameGen.label(context, mode)
        if (lsfg && Lossless.cacheFile(context)?.isFile != true) return context.getString(R.string.hud_fg_needs_lossless, name)
        val stats = try { WaylandCompositor.nativeFrameGenStats() } catch (t: Throwable) { null }
        val problem = try { WaylandCompositor.nativeFrameGenProblem() } catch (t: Throwable) { -1 }
        return when {
            problem == 1 -> context.getString(R.string.hud_fg_unsupported, name)
            problem == 2 -> context.getString(R.string.hud_fg_failed, name)
            stats != null && stats[3] > 1f -> String.format(Locale.US, "%s  %.0f → %.0f fps", label, base, stats[3]) + detail()
            else -> context.getString(R.string.hud_fg_starting, label, fps(base)) + detail()
        }
    }

    /**
     * The driver-side numbers the higher overlay levels add. Every call is wrapped: the natives
     * are only valid once the compositor has a surface, and a level that reads them must not be
     * able to take the HUD down before the first frame.
     */
    private fun detail(): String {
        val level = overlayLevel()
        if (level < SessionPrefs.OVERLAY_LEVEL_2) return ""
        val zeroCopy = try {
            val n = WaylandCompositor.nativeZeroCopyFrames()
            val age = WaylandCompositor.nativeZeroCopyLastFrameAgeMs()
            if (n > 0) "  zero-copy $n (${age}ms ago)" else "  zero-copy none"
        } catch (t: Throwable) { "" }
        if (level < SessionPrefs.OVERLAY_LEVEL_3) return zeroCopy
        val hdr = try {
            val age = WaylandCompositor.nativeHdrLastFrameAgeMs()
            if (WaylandCompositor.nativeHdrOnScreen() && age >= 0) "  hdr ${age}ms ago" else ""
        } catch (t: Throwable) { "" }
        val stats = try { WaylandCompositor.nativeFrameGenStats() } catch (t: Throwable) { null }
        val fg = if (stats != null && stats.size >= 4 && stats[3] > 1f) String.format(Locale.US, "  fg %.0f", stats[3]) else ""
        return zeroCopy + hdr + fg
    }

    private fun fps(value: Float) = String.format(Locale.US, "%.0f fps", value)
}
