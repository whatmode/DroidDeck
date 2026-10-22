package com.droiddeck.launcher.core

import org.junit.Assert.*
import org.junit.Test

class GameEnvironmentTest {
    @Test fun defaultsIncludeRequestedGraphicsLevelsAndKeepCaching() {
        val values = GameEnvironment.defaults("")
        assertEquals("false", values["MESA_SHADER_CACHE_DISABLE"])
        assertEquals("12_2", values["VKD3D_FEATURE_LEVEL"])
        assertEquals("6_9", values["VKD3D_SHADER_MODEL"])
    }

    @Test fun forceSsbsIsADefaultTheUserCanTurnOff() {
        assertFalse(GameEnvironment.defaults("").containsKey(GameEnvironment.FORCE_SSBS))
        assertEquals("1", GameEnvironment.defaults("", forceSsbs = true)[GameEnvironment.FORCE_SSBS])
        val config = GameEnvironment.Config().withEntries(mapOf(GameEnvironment.FORCE_SSBS to "0"))
        assertEquals("0", GameEnvironment.effective(config, "", forceSsbs = true)[GameEnvironment.FORCE_SSBS])
    }

    @Test fun savedValuesOverrideThePresetDefaults() {
        val config = GameEnvironment.Config(
            shared = mapOf("FEX_MULTIBLOCK" to "0", "VKD3D_FEATURE_LEVEL" to "12_0"),
        )
        val values = GameEnvironment.effective(config, "COMPATIBILITY")
        assertEquals("0", values["FEX_MULTIBLOCK"])
        assertEquals("12_0", values["VKD3D_FEATURE_LEVEL"])
    }

    @Test fun anUnsetValueRemovesADefaultAndClearingItRestoresTheDefault() {
        val config = GameEnvironment.Config(shared = mapOf("VKD3D_FEATURE_LEVEL" to "12_0"))
            .withEntries(mapOf("VKD3D_SHADER_MODEL" to null))
        val values = GameEnvironment.effective(config, "")
        assertEquals("12_0", values["VKD3D_FEATURE_LEVEL"])
        assertTrue(values.containsKey("VKD3D_SHADER_MODEL"))
        assertNull(values["VKD3D_SHADER_MODEL"])
        val restored = GameEnvironment.effective(config.withEntries(emptyMap()), "")
        assertEquals("6_9", restored["VKD3D_SHADER_MODEL"])
    }

    @Test fun dotnetRuntimeFitsAndroidAddressSpace() {
        val values = GameEnvironment.effective(GameEnvironment.Config(), "PERFORMANCE")
        assertEquals("800000000", values["DOTNET_GCRegionRange"])
        assertEquals("0", values["DOTNET_EnableWriteXorExecute"])
    }

    @Test fun dotnetHeapRangeIsBareHexForNativeAot() {
        for (preset in FexPreset.all) {
            val values = GameEnvironment.effective(GameEnvironment.Config(), preset.id)
            assertEquals(32L * 1024 * 1024 * 1024, values.getValue("DOTNET_GCRegionRange")!!.toLong(16))
        }
    }

    @Test fun switchingPresetDropsOldPresetOnlyVariables() {
        val config = GameEnvironment.Config()
        assertEquals("none", GameEnvironment.effective(config, "EXTREME")["FEX_SMCCHECKS"])
        assertFalse(GameEnvironment.effective(config, "COMPATIBILITY").containsKey("FEX_SMCCHECKS"))
        assertFalse(GameEnvironment.effective(config, "").containsKey("FEX_MULTIBLOCK"))
    }

    @Test fun withValueAddsReplacesAndRemovesAnEntry() {
        val added = GameEnvironment.withValue(GameEnvironment.Config(), "CUSTOM", "value")
        assertEquals("value", added.shared["CUSTOM"])
        val replaced = GameEnvironment.withValue(added, "CUSTOM", "other")
        assertEquals("other", replaced.shared["CUSTOM"])
        val removed = GameEnvironment.withValue(replaced, "CUSTOM", null)
        assertFalse(removed.shared.containsKey("CUSTOM"))
    }

    @Test fun validationPreservesLiteralValuesAndRejectsInvalidKeys() {
        assertTrue(GameEnvironment.validName("mesa_glthread"))
        assertFalse(GameEnvironment.validName("X=Y"))
        assertFalse(GameEnvironment.validName("1VAR"))
        assertFalse(GameEnvironment.validName("VAR\n"))
        assertTrue(GameEnvironment.validValue("a=b 'quote' $(touch file) ; $"))
        assertTrue(GameEnvironment.validValue(""))
        assertFalse(GameEnvironment.validValue("bad\u0000value"))
        assertFalse(GameEnvironment.validValue("x".repeat(8193)))
    }

    @Test fun syncEnvironmentNamesEachBackend() {
        assertEquals(
            mapOf("BL_SYNC" to "1", "BL_FSYNC_FIRST" to "0", "BL_SYNC_FALLBACK" to "1"),
            GameEnvironment.syncEnvironment("ntsync"),
        )
        assertEquals(
            mapOf("BL_SYNC" to "0", "BL_FSYNC_FIRST" to "1", "BL_SYNC_FALLBACK" to "1", "PROTON_NO_ESYNC" to "1"),
            GameEnvironment.syncEnvironment("fsync"),
        )
        assertEquals(
            mapOf("BL_SYNC" to "0", "BL_FSYNC_FIRST" to "0", "BL_SYNC_FALLBACK" to "0"),
            GameEnvironment.syncEnvironment("wineserver"),
        )
        assertThrows(IllegalArgumentException::class.java) { GameEnvironment.syncEnvironment("nonsense") }
    }
}
