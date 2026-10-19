package com.droiddeck.launcher.stores.ea

import android.content.Context
import com.droiddeck.launcher.frontend.AddedGames
import com.droiddeck.launcher.session.SessionPrefs
import com.droiddeck.launcher.stores.CatalogItem
import com.droiddeck.launcher.stores.SteamLiveShortcuts
import com.droiddeck.launcher.stores.Store
import com.droiddeck.launcher.stores.StoreExe
import com.droiddeck.launcher.stores.StoreGameSidecar
import com.droiddeck.launcher.stores.StoreInstalls
import com.droiddeck.launcher.stores.download.DownloadQueue
import com.droiddeck.launcher.stores.download.DownloadStage
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

internal class EaInstaller(
    private val app: Context,
    private val item: CatalogItem,
    private val folder: File,
) : DownloadQueue.DownloadJob {
    private val cancelled = AtomicBoolean(false)

    override fun run(handle: DownloadQueue.JobHandle): String? {
        if (!EaNative.ready(app)) throw IllegalStateException(EaNative.ENGINE_MISSING)
        handle.stage(DownloadStage.MANIFEST, "Asking EA for the game…")
        val cover = item.tallImageUrl ?: item.imageUrl
        StoreInstalls.begin(folder, Store.EA, item.id, item.title, cover, item.imageUrl)
        val guest = AddedGames.guestPath(app, folder) ?: throw IllegalStateException("The install folder is not visible to the Linux system: ${folder.path}")
        var downloading = false
        val outcome = EaNative.run(app, listOf("install", item.id, guest)) { o ->
            if (o.has("progress")) {
                if (!downloading) { downloading = true; handle.stage(DownloadStage.DOWNLOAD) }
                handle.progress(o.optLong("done"), o.optLong("total"), "Downloading from EA")
            }
        }
        if (cancelled.get()) return null
        val done = outcome.first("installed")
        if (done == null) throw IllegalStateException("EA install failed: " + (outcome.error ?: "exit ${outcome.code}"))

        handle.stage(DownloadStage.INSTALL, "Registering…")
        var exe = done.optString("exe")
        if (exe.isEmpty() || !File(folder, exe).exists()) {
            handle.log("ea: manifest exe \"$exe\" not found; picking one")
            exe = StoreExe.pick(folder, null, item.title).ifEmpty { exe.ifEmpty { "game.exe" } }
        }
        handle.log("ea: \"${item.title}\" exe=$exe version=${done.optString("version")}")
        val sidecar = StoreGameSidecar(
            Store.EA, item.id, item.title, exe = exe,
            installVersion = done.optString("version"), installedAt = System.currentTimeMillis(),
            cover = cover, hero = item.imageUrl, cloud = false,
            extra = mapOf("offerId" to item.id, "contentId" to item.extra["contentId"].orEmpty(), "slug" to item.extra["slug"].orEmpty()),
        )
        StoreInstalls.complete(app, folder, sidecar, onStep = { d, steps -> handle.stageProgress(d.toLong(), steps.toLong()) })
        launchThroughClient(app, folder, item.id)
        return folder.path
    }

    override fun cancel(deleteFiles: Boolean) {
        cancelled.set(true)
        if (deleteFiles) StoreInstalls.discard(app, folder, emptyList())
    }

    companion object {
        /** The Steam shortcut starts the game through droiddeck-ea, which licenses it and serves LSX. */
        fun launchThroughClient(app: Context, folder: File, offerId: String) {
            val game = AddedGames.single(app, folder.path) ?: return
            val options = "${EaNative.PROGRAM} run $offerId \"${game.guestExe}\" -- %command%"
            if (SessionPrefs.addedGameLaunch(app, folder.path) == options) return
            SessionPrefs.setAddedGameLaunch(app, folder.path, options)
            StoreInstalls.register(app)
            AddedGames.single(app, folder.path)?.let { runCatching { SteamLiveShortcuts.update(app, it) } }
        }
    }
}
