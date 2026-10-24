package com.droiddeck.launcher.session

import android.view.Display
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.shadows.ShadowDisplayManager

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [31, 34], manifest = Config.NONE)
class PerfModeTest {
    private fun mode(id: Int, hz: Float, width: Int = 1080): Display.Mode =
        ShadowDisplayManager.ModeBuilder.modeBuilder(id)
            .setWidth(width).setHeight(2520).setRefreshRate(hz).build()

    @Test fun sixtyHzMirrorChoosesSixtyEvenThoughOneTwentyHasMatchingCadence() {
        val low = mode(1, 60f)
        val high = mode(2, 120f)
        assertEquals(low, PerfMode.selectMode(arrayOf(low, high), high, 60, 60f))
        assertEquals(high, PerfMode.selectMode(arrayOf(low, high), low, 0, Float.POSITIVE_INFINITY))
    }

    @Test fun aLowerGameLimitDoesNotOverrideTheGlassesRefreshCeiling() {
        val modes = arrayOf(mode(1, 60f), mode(2, 90f), mode(3, 120f))
        assertEquals(modes[0], PerfMode.selectMode(modes, modes[2], 30, 60f))
        assertEquals(modes[1], PerfMode.selectMode(modes, modes[2], 45, 90f))
    }

    @Test fun selectionPreservesResolutionAndUsesTheLowestModeIfTheCapIsUnsupported() {
        val low = mode(1, 90f)
        val high = mode(2, 120f)
        val otherResolution = mode(3, 60f, 720)
        assertEquals(low, PerfMode.selectMode(arrayOf(low, high, otherResolution), high, 60, 60f))
    }

    // The panel exposes one 120 Hz mode under two ids (1 and 255), which is the shape that made
    // PerfMode write preferredDisplayModeId 2242 times in three minutes: each write reordered
    // supportedModes, the maxByOrNull winner flipped, and the caller's guard never matched.
    @Test fun twoIdsForTheSameRefreshDoNotAlternate() {
        val id1 = mode(1, 120f)
        val id255 = mode(255, 120f)
        val forward = arrayOf(id1, id255)
        val reversed = arrayOf(id255, id1)

        // The answer does not depend on the order supportedModes is reported in.
        for (modes in listOf(forward, reversed)) {
            assertEquals(id1, PerfMode.selectMode(modes, id1, 0, Float.POSITIVE_INFINITY))
            assertEquals(id255, PerfMode.selectMode(modes, id255, 0, Float.POSITIVE_INFINITY))
        }

        // And every answer is a fixed point, so the caller stops reapplying after one write.
        for (current in listOf(id1, id255)) {
            val once = PerfMode.selectMode(forward, current, 0, Float.POSITIVE_INFINITY)!!
            assertEquals(once, PerfMode.selectMode(forward, once, 0, Float.POSITIVE_INFINITY))
        }
    }

    @Test fun aModeAlreadyAllowedAndAsGoodIsKept() {
        val low = mode(1, 60f)
        val high = mode(2, 120f)
        // Already at the best allowed refresh: keep the mode in use rather than switching to a
        // same-refresh sibling or bouncing to another id.
        assertEquals(high, PerfMode.selectMode(arrayOf(low, high), high, 0, Float.POSITIVE_INFINITY))
        // A cap that lowers the refresh still moves off the current mode.
        assertEquals(low, PerfMode.selectMode(arrayOf(low, high), high, 0, 60f))
    }
}
