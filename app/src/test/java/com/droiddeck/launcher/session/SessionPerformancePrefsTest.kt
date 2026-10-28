package com.droiddeck.launcher.session

import android.content.Context
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class SessionPerformancePrefsTest {
    @Before fun clearSettings() {
        RuntimeEnvironment.getApplication().getSharedPreferences("session", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test fun theRefreshChoiceIsPerModeAndZeroMeansThePanel() {
        val context = RuntimeEnvironment.getApplication()
        assertEquals(0, SessionPrefs.refreshLimit(context, "steam"))
        SessionPrefs.setRefreshLimit(context, "steam", 90)
        assertEquals(90, SessionPrefs.refreshLimit(context, "steam"))
        // The desktop keeps its own answer; the two must not share one key.
        assertEquals(0, SessionPrefs.refreshLimit(context, "lxqt"))
        SessionPrefs.setRefreshLimit(context, "steam", -5)
        assertEquals(0, SessionPrefs.refreshLimit(context, "steam"))
    }

    @Test fun theTightestLimitIgnoresAnAbsentCeiling() {
        assertEquals(60, SessionPrefs.tightestRefreshLimit(0, 60))
        assertEquals(60, SessionPrefs.tightestRefreshLimit(60, 0))
        assertEquals(0, SessionPrefs.tightestRefreshLimit(0, 0))
        assertEquals(60, SessionPrefs.tightestRefreshLimit(60, 120))
        assertEquals(60, SessionPrefs.tightestRefreshLimit(120, 60))
    }

    @Test fun theOverlayLevelAndTheHudSwitchAgreeInBothDirections() {
        val context = RuntimeEnvironment.getApplication()
        SessionPrefs.setOverlayLevel(context, SessionPrefs.OVERLAY_LEVEL_3)
        assertEquals(SessionPrefs.OVERLAY_LEVEL_3, SessionPrefs.overlayLevel(context))
        assertTrue(SessionPrefs.hudEnabled(context))

        SessionPrefs.setOverlayLevel(context, SessionPrefs.OVERLAY_OFF)
        assertEquals(SessionPrefs.OVERLAY_OFF, SessionPrefs.overlayLevel(context))
        assertFalse(SessionPrefs.hudEnabled(context))

        SessionPrefs.setHudEnabled(context, true)
        assertEquals(SessionPrefs.OVERLAY_LEVEL_1, SessionPrefs.overlayLevel(context))
    }

    @Test fun anOutOfRangeOverlayLevelIsClamped() {
        val context = RuntimeEnvironment.getApplication()
        SessionPrefs.setOverlayLevel(context, 99)
        assertEquals(SessionPrefs.OVERLAY_LEVEL_3, SessionPrefs.overlayLevel(context))
        SessionPrefs.setOverlayLevel(context, -1)
        assertEquals(SessionPrefs.OVERLAY_OFF, SessionPrefs.overlayLevel(context))
    }

    @Test fun sustainedPerformanceDefaultsOffAndIsRemembered() {
        val context = RuntimeEnvironment.getApplication()
        assertFalse(SessionPrefs.sustainedPerformance(context))
        SessionPrefs.setSustainedPerformance(context, true)
        assertTrue(SessionPrefs.sustainedPerformance(context))
    }

    @Test fun theSessionResetClearsTheNewSettings() {
        val context = RuntimeEnvironment.getApplication()
        SessionPrefs.setRefreshLimit(context, "steam", 90)
        SessionPrefs.setOverlayLevel(context, SessionPrefs.OVERLAY_LEVEL_2)
        SessionPrefs.setSustainedPerformance(context, true)
        assertTrue(SettingsReset.isSessionSetting("refreshLimit.steam"))
        assertTrue(SettingsReset.isSessionSetting("overlayLevel"))
        assertTrue(SettingsReset.isSessionSetting("sustainedPerformance"))
    }
}
