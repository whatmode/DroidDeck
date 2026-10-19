package com.droiddeck.launcher.stores.ea

import android.content.Context
import com.droiddeck.launcher.stores.CatalogItem
import com.droiddeck.launcher.stores.Store
import com.droiddeck.launcher.stores.StoreShelves
import com.droiddeck.launcher.stores.StoresState
import com.droiddeck.launcher.stores.mergeItems
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicBoolean

object EaLibrary {
    private const val PREFS = "store_ea"
    private const val KEY_CACHE = "library_cache"
    private const val STORE_URL = "https://www.ea.com/games/library"
    private val running = AtomicBoolean(false)
    private val store = Store.EA

    private fun prefs(context: Context) = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun forget(context: Context) { prefs(context).edit().clear().apply() }

    private fun image(game: JSONObject?, vararg keys: String): String? {
        game ?: return null
        for (key in keys) {
            val rendition = game.optJSONObject(key) ?: continue
            for (aspect in listOf("aspect16x9Image", "aspect2x1Image", "aspect9x16Image", "aspect1x1Image", "largestImage")) {
                rendition.optJSONObject(aspect)?.optString("path")?.takeIf { it.startsWith("http") }?.let { return it }
            }
        }
        return null
    }

    private fun tall(game: JSONObject?): String? {
        game ?: return null
        for (key in listOf("packArt", "keyArt")) {
            val rendition = game.optJSONObject(key) ?: continue
            for (aspect in listOf("aspect9x16Image", "aspect7x5Image", "largestImage")) {
                rendition.optJSONObject(aspect)?.optString("path")?.takeIf { it.startsWith("http") }?.let { return it }
            }
        }
        return null
    }

    fun toItem(o: JSONObject): CatalogItem? {
        val offer = o.optString("offerId").ifEmpty { return null }
        val game = o.optJSONObject("game")
        return CatalogItem(
            store = store,
            id = offer,
            title = o.optString("name").ifEmpty { o.optString("displayName") }.ifEmpty { o.optString("slug") },
            imageUrl = image(game, "keyArt", "packArt"),
            tallImageUrl = tall(game),
            owned = true,
            storeUrl = STORE_URL,
            extra = mapOf("slug" to o.optString("slug"), "contentId" to o.optString("contentId"), "version" to ""),
        )
    }

    private fun publish(items: List<CatalogItem>) {
        StoresState.post {
            StoresState.library[store] = mergeItems(StoresState.library[store], items)
            val installed = StoresState.installed.filter { it.sidecar.store == store }.map { it.sidecar.id }.toSet()
            StoresState.shelves[store] = StoreShelves(trending = items.filterNot { it.id in installed }, art = StoresState.shelves[store]?.art.orEmpty())
        }
    }

    private fun parse(text: String?): List<CatalogItem> {
        val array = runCatching { JSONArray(text ?: return emptyList()) }.getOrNull() ?: return emptyList()
        return (0 until array.length()).mapNotNull { array.optJSONObject(it)?.let(::toItem) }.distinctBy { it.id }.sortedBy { it.title.lowercase() }
    }

    fun sync(context: Context) {
        if (!running.compareAndSet(false, true)) return
        val app = context.applicationContext
        fun problem(text: String?) {
            StoresState.post {
                StoresState.status.remove(store)
                if (text == null) StoresState.problems.remove(store) else StoresState.problems[store] = text
            }
        }
        try {
            val cached = prefs(app).getString(KEY_CACHE, null)
            parse(cached).takeIf { it.isNotEmpty() }?.let(::publish)
            if (!EaNative.ready(app)) return problem(EaNative.ENGINE_MISSING)
            StoresState.post { StoresState.status[store] = "Reading your EA library…" }
            val outcome = EaNative.run(app, listOf("library"))
            val games = outcome.first("games")?.optJSONArray("games")
            if (games == null) {
                val error = outcome.error.orEmpty()
                StoresState.logLine("ea: library failed: ${error.ifEmpty { "exit ${outcome.code}" }}")
                if (error.contains("sign", true) || error.contains("auth", true) || error.contains("token", true)) StoresState.markSignInExpired(store)
                return problem(if (cached == null) "EA library could not be read: ${error.ifEmpty { "no answer" }}" else null)
            }
            prefs(app).edit().putString(KEY_CACHE, games.toString()).apply()
            val items = parse(games.toString())
            StoresState.logLine("ea: library has ${items.size} games")
            publish(items)
            problem(if (items.isEmpty()) "No downloadable EA games in this account" else null)
        } catch (e: Exception) {
            problem(e.message ?: "EA library sync failed")
        } finally {
            running.set(false)
        }
    }
}
