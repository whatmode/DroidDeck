package com.droiddeck.launcher.stores.ea

import android.content.Context
import android.content.Intent
import com.droiddeck.launcher.stores.CatalogItem
import com.droiddeck.launcher.stores.InstalledStoreGame
import com.droiddeck.launcher.stores.LoginFlows
import com.droiddeck.launcher.stores.Store
import com.droiddeck.launcher.stores.StoreBackend
import com.droiddeck.launcher.stores.StoreInstallRoot
import com.droiddeck.launcher.stores.StoreLoginActivity
import com.droiddeck.launcher.stores.StoresState
import com.droiddeck.launcher.stores.download.DownloadEntry
import com.droiddeck.launcher.stores.download.DownloadQueue

object EaBackend : StoreBackend {
    override val store: Store = Store.EA

    init {
        LoginFlows.eaFlow = { EaLogin.take() }
    }

    private fun engineMissing(context: Context): Boolean {
        if (EaNative.ready(context)) return false
        StoresState.logLine(EaNative.ENGINE_MISSING)
        StoresState.post { StoresState.problems[store] = EaNative.ENGINE_MISSING }
        return true
    }

    override fun signIn(context: Context) {
        val app = context.applicationContext
        if (engineMissing(app)) return
        StoresState.post { StoresState.status[store] = "Starting EA sign-in…" }
        Thread({
            val problem = EaLogin.begin(app)
            StoresState.post { StoresState.status.remove(store) }
            if (problem != null) {
                StoresState.logLine("ea: $problem")
                StoresState.post { StoresState.problems[store] = problem }
                return@Thread
            }
            app.startActivity(
                Intent(app, StoreLoginActivity::class.java)
                    .putExtra(StoreLoginActivity.EXTRA_STORE, store.id)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }, "ea-sign-in").start()
    }

    override fun signOut(context: Context) {
        val app = context.applicationContext
        if (EaNative.ready(app)) runCatching { EaNative.run(app, listOf("logout")) }
        EaLibrary.forget(app)
    }

    override fun syncLibrary(context: Context, force: Boolean) {
        val app = context.applicationContext
        Thread({ EaLibrary.sync(app) }, "ea-sync").start()
    }

    override fun loadShelves(context: Context, force: Boolean) {}

    override fun install(context: Context, item: CatalogItem, root: java.io.File) {
        val app = context.applicationContext
        if (engineMissing(app)) return
        val folder = StoreInstallRoot.folderFor(app, Store.EA, item.id, item.title, root)
        val entry = DownloadEntry(
            Store.EA, item.id, item.title, cover = item.imageUrl, bytesTotal = item.sizeBytes,
            location = StoreInstallRoot.labelFor(app, folder),
        )
        DownloadQueue.enqueue(app, entry) { EaInstaller(app, item, folder) }
    }

    override fun uninstall(context: Context, game: InstalledStoreGame) {}
}
