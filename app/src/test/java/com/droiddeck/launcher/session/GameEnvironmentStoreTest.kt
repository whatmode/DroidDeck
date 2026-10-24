package com.droiddeck.launcher.session

import android.content.Context
import com.droiddeck.launcher.core.GameEnvironment
import com.droiddeck.launcher.runtime.LinuxRuntime
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.io.File

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [28], manifest = Config.NONE)
class GameEnvironmentStoreTest {
    private lateinit var context: Context
    private lateinit var guest: File

    @Before fun setUp() {
        context = RuntimeEnvironment.getApplication()
        guest = File(LinuxRuntime.rootDir(context), "root/.config/droiddeck/game-environment.json")
        File(context.filesDir, "game-environment.json").delete()
    }

    @Test fun savedSettingsAndPublishedSettingsPreserveUnsetsAndLiteralValues() {
        val config = GameEnvironment.Config(
            shared = mapOf("LITERAL" to "a=b 'quoted' $(echo test)", "EMPTY" to "", "VKD3D_SHADER_MODEL" to null),
        )
        GameEnvironmentStore.save(context, config)
        assertEquals(config, GameEnvironmentStore.read(context))
        val published = GameEnvironmentStore.decode(JSONObject(guest.readText()))
        assertEquals("false", published.shared["MESA_SHADER_CACHE_DISABLE"])
        assertEquals("12_2", published.shared["VKD3D_FEATURE_LEVEL"])
        assertNull(published.shared["VKD3D_SHADER_MODEL"])
        assertEquals("a=b 'quoted' $(echo test)", published.shared["LITERAL"])
        assertEquals("", published.shared["EMPTY"])
    }

    @Test fun theGuestFileCarriesNoPerGameSection() {
        GameEnvironmentStore.save(context, GameEnvironment.Config(shared = mapOf("CUSTOM" to "1")))
        val json = JSONObject(guest.readText())
        assertFalse(json.has("games"))
        assertFalse(json.has("dxvkConfigGames"))
    }

    @Test fun changingFexPresetPublishesForNextGameLaunch() {
        GameEnvironmentStore.save(context, GameEnvironment.Config())
        SessionPrefs.setFexPreset(context, "EXTREME")
        assertEquals("none", JSONObject(guest.readText()).getJSONObject("shared").getString("FEX_SMCCHECKS"))
        SessionPrefs.setFexPreset(context, "")
        assertFalse(JSONObject(guest.readText()).getJSONObject("shared").has("FEX_SMCCHECKS"))
    }

    @Test fun textureFilteringPublishesDxvkOptions() {
        GameEnvironmentStore.save(context, GameEnvironment.Config(shared = mapOf("DXVK_CONFIG" to "dxvk.tearFree = True")))
        assertFalse(JSONObject(guest.readText()).has(GameEnvironmentStore.DXVK_CONFIG))
        SessionState.upscaleRatio = 1.5f
        SessionPrefs.setTextureAnisotropy(context, 16)
        SessionPrefs.setTextureLodBias(context, com.droiddeck.launcher.core.TextureFiltering.LOD_BIAS_AUTO)
        val json = JSONObject(guest.readText())
        assertEquals(
            "d3d9.samplerAnisotropy = 16; d3d11.samplerAnisotropy = 16; d3d9.samplerLodBias = -0.58; d3d11.samplerLodBias = -0.58",
            json.getString(GameEnvironmentStore.DXVK_CONFIG),
        )
        assertEquals("dxvk.tearFree = True", json.getJSONObject("shared").getString("DXVK_CONFIG"))
        assertEquals(GameEnvironment.Config(shared = mapOf("DXVK_CONFIG" to "dxvk.tearFree = True")), GameEnvironmentStore.read(context))
        SessionPrefs.setTextureAnisotropy(context, 0)
        SessionPrefs.setTextureLodBias(context, "0")
        assertFalse(JSONObject(guest.readText()).has(GameEnvironmentStore.DXVK_CONFIG))
        SessionState.upscaleRatio = 0f
    }

    @Test fun thePresetChangeReachesTheGuestWithoutTouchingTheSavedSettings() {
        SessionPrefs.setFexPreset(context, "PERFORMANCE_TSO")
        val original = GameEnvironment.Config(shared = mapOf("PROTON_LOG" to "1"))
        GameEnvironmentStore.save(context, original)

        assertEquals(original, GameEnvironmentStore.read(context))
        val published = GameEnvironmentStore.decode(JSONObject(guest.readText()))
        assertEquals("1", published.shared["PROTON_LOG"])
        assertEquals("0", published.shared["FEX_HALFBARRIERTSOENABLED"])
        assertEquals("1", published.shared["FEX_TSOENABLED"])
    }

    @Test fun unchosenFexPresetPublishesPerformanceTso() {
        GameEnvironmentStore.save(context, GameEnvironment.Config())
        val shared = JSONObject(guest.readText()).getJSONObject("shared")
        assertEquals("1", shared.getString("FEX_TSOENABLED"))
        assertEquals("0", shared.getString("FEX_HALFBARRIERTSOENABLED"))
        assertEquals("1", shared.getString("FEX_X87REDUCEDPRECISION"))
        assertEquals("1", shared.getString("FEX_MULTIBLOCK"))
    }

    @Test fun invalidDataCannotReplaceSavedConfiguration() {
        val original = GameEnvironment.Config(shared = mapOf("CUSTOM" to "ok"))
        GameEnvironmentStore.save(context, original)
        assertTrue(runCatching { GameEnvironmentStore.save(context, GameEnvironment.Config(shared = mapOf("BAD=NAME" to "x"))) }.isFailure)
        assertEquals(original, GameEnvironmentStore.read(context))
    }

    @Test fun customComponentVariablesRemainEditable() {
        val config = GameEnvironment.Config(shared = mapOf("DXVK_ASYNC" to "1"))
        GameEnvironmentStore.save(context, config)
        assertEquals(config, GameEnvironmentStore.read(context))
    }
}
