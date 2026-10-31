package com.droiddeck.launcher.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class ShaderPrecompileTest {
    @get:Rule val tmp = TemporaryFolder()

    @Test
    fun readsWhatTheGuestWrites() {
        val p = ShaderPrecompile.parse("""{"game": "Lake", "state": "compiling", "done": 812, "total": 1985, "eta": 95}""")!!
        assertEquals("Lake", p.game)
        assertEquals(812, p.done)
        assertEquals(1985, p.total)
        assertEquals(95, p.etaSeconds)
        assertEquals(812f / 1985f, p.fraction, 0.0001f)
        assertFalse(p.finished)
    }

    @Test
    fun beforeTheTotalIsKnownTheBarIsEmpty() {
        val p = ShaderPrecompile.parse("""{"game": "Lake", "state": "compiling", "done": 0, "total": 0, "eta": -1}""")!!
        assertEquals(0f, p.fraction, 0f)
    }

    @Test
    fun endStates() {
        for (state in listOf("done", "skipped", "failed")) {
            assertTrue(ShaderPrecompile.parse("""{"state": "$state"}""")!!.finished)
        }
    }

    @Test
    fun noFileOrHalfWrittenFileShowsNothing() {
        assertNull(ShaderPrecompile.parse(File(tmp.root, "missing.json")))
        val half = File(tmp.root, "half.json").apply { writeText("""{"game": "La""") }
        assertNull(ShaderPrecompile.parse(half))
    }
}
