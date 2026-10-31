package com.droiddeck.launcher.session

import android.app.Activity
import android.app.GameManager
import android.app.GameState
import android.content.Context
import android.os.Build
import android.provider.Settings
import android.util.Log
import android.view.Display

object PerfMode {
    private const val TAG = "PerfMode"

    fun apply(a: Activity, fpsLimit: Int = 0, refreshLimit: Int = 0): String {
        val parts = ArrayList<String>(2)

        parts += try {
            val display = if (Build.VERSION.SDK_INT >= 30) a.display else @Suppress("DEPRECATION") a.windowManager.defaultDisplay
            if (display == null || Build.VERSION.SDK_INT < 23) "display mode unchanged" else {
                val cur = display.mode
                val cap = minOf(peakRefreshCap(a), refreshLimit.takeIf { it > 0 }?.toFloat() ?: Float.POSITIVE_INFINITY)
                val best = selectMode(display.supportedModes, cur, fpsLimit, cap)
                // The panel can report the mode it runs in under another id than the one asked for
                // (asking for 120 Hz mode 1 reads back as mode 255), so comparing ids alone rewrote
                // the preference on every onDisplayChanged and the display flapped between 60 and
                // 120 Hz every few frames. Keep a preference that already asks for the same size
                // and refresh as the best mode.
                val asked = display.supportedModes.firstOrNull { it.modeId == a.window.attributes.preferredDisplayModeId }
                val alreadyAsked = best != null && asked != null &&
                    asked.physicalWidth == best.physicalWidth && asked.physicalHeight == best.physicalHeight &&
                    kotlin.math.abs(asked.refreshRate - best.refreshRate) < 0.5f
                if (best != null && !alreadyAsked && a.window.attributes.preferredDisplayModeId != best.modeId) {
                    a.window.attributes = a.window.attributes.apply { preferredDisplayModeId = best.modeId }
                    "display ${cur.refreshRate.toInt()} → ${best.refreshRate.toInt()} Hz (mode ${best.modeId})"
                } else "display ${cur.refreshRate.toInt()} Hz"
            }
        } catch (t: Throwable) { Log.w(TAG, "display mode", t); "display mode failed" }

        parts += if (Build.VERSION.SDK_INT >= 31) {
            val gm = a.getSystemService(GameManager::class.java)
            if (gm == null) "no GameManager" else {
                val mode = when (gm.gameMode) {
                    GameManager.GAME_MODE_PERFORMANCE -> "performance"
                    GameManager.GAME_MODE_BATTERY -> "battery"
                    GameManager.GAME_MODE_STANDARD -> "standard"
                    else -> "unsupported"
                }
                if (Build.VERSION.SDK_INT >= 33) {
                    try { gm.setGameState(GameState(false, GameState.MODE_GAMEPLAY_INTERRUPTIBLE)) } catch (t: Throwable) { Log.w(TAG, "game state", t) }
                }
                "game mode $mode"
            }
        } else "game mode needs Android 12"

        return parts.joinToString(" · ")
    }

    fun peakRefreshCap(context: Context): Float {
        val peak = try {
            Settings.System.getFloat(context.contentResolver, "peak_refresh_rate")
        } catch (_: Throwable) { 0f }
        return if (peak >= 1f) peak else Float.POSITIVE_INFINITY
    }

    internal fun selectMode(modes: Array<Display.Mode>, current: Display.Mode, fpsLimit: Int, cap: Float): Display.Mode? {
        val sameSize = modes.filter {
            it.physicalWidth == current.physicalWidth && it.physicalHeight == current.physicalHeight
        }
        val allowed = sameSize.filter { it.refreshRate <= cap + 0.5f }
        val best = allowed.filter { cadenceFits(it.refreshRate, fpsLimit) }.maxByOrNull { it.refreshRate }
            ?: allowed.maxByOrNull { it.refreshRate }
            ?: sameSize.minByOrNull { it.refreshRate }
            ?: return null
        // Two modes can share a size and a refresh (a panel often exposes one mode under two ids).
        // maxByOrNull picks whichever comes first in supportedModes, and that order follows the
        // current mode, so the "winner" flips on every write: the caller's guard never sees its own
        // value and reapplies forever, once per onDisplayChanged. Keep the current mode when it is
        // already allowed and as good, and otherwise break the tie on the id, which is unique, so
        // the choice depends only on the modes and the limits, never on the array order.
        if (allowed.any { it.modeId == current.modeId } && best.refreshRate <= current.refreshRate + 0.5f) {
            return current
        }
        return sameSize.filter { it.refreshRate == best.refreshRate }.minByOrNull { it.modeId } ?: best
    }

    fun cadenceFits(hz: Float, fpsLimit: Int): Boolean {
        if (fpsLimit <= 0) return true
        val ratio = hz / fpsLimit
        return ratio >= 0.98f && Math.abs(ratio - Math.round(ratio)) < 0.02f
    }
}
