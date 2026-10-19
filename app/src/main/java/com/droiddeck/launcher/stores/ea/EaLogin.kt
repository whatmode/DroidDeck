package com.droiddeck.launcher.stores.ea

import android.content.Context
import android.net.Uri
import android.util.Log
import android.webkit.WebView
import com.droiddeck.launcher.runtime.GuestCommand
import com.droiddeck.launcher.stores.Store
import com.droiddeck.launcher.stores.StoreAccounts
import com.droiddeck.launcher.stores.StoreLoginActivity
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * EA's PC sign-in, run in the app's sign-in window. droiddeck-ea login prints the sign-in address
 * and waits on 127.0.0.1:31033 for EA's redirect with the code; the guest shares the phone's
 * network, so the code is handed to it there.
 */
internal class EaLogin private constructor(override val startUrl: String, private val session: Session) : StoreLoginActivity.Flow {
    override val userAgent: String get() = DESKTOP_UA

    override fun isRedirect(url: String): Boolean = url.startsWith(REDIRECT) || url.startsWith("qrc:")

    override fun finish(activity: StoreLoginActivity, view: WebView, url: String): String? {
        val code = Uri.parse(url.replace("qrc:///", "http://x/").replace("qrc:/", "http://x/")).let { u ->
            u.getQueryParameter("code") ?: Uri.parse("http://x/?" + (u.fragment ?: "")).getQueryParameter("code")
        }
        if (code.isNullOrEmpty()) {
            Log.w(TAG, "EA's redirect carried no code")
            return "EA did not return a sign-in code; try again."
        }
        if (!session.done.await(0, TimeUnit.MILLISECONDS)) {
            runCatching {
                val c = URL("$REDIRECT/auth?code=" + Uri.encode(code)).openConnection() as HttpURLConnection
                c.connectTimeout = 5_000; c.readTimeout = 10_000
                c.responseCode
                c.disconnect()
            }.onFailure { Log.w(TAG, "handing the code over: ${it.javaClass.simpleName}") }
        }
        if (!session.done.await(60, TimeUnit.SECONDS)) return "The EA client did not finish signing in; try again."
        val answer = session.result
        if (answer?.optBoolean("loggedIn") != true) return "EA sign-in failed: " + (answer?.optString("error")?.ifEmpty { null } ?: "no answer")
        StoreAccounts.write(
            activity.applicationContext, Store.EA,
            JSONObject().put("access_token", "maxima").put("display_name", answer.optString("name").ifEmpty { "EA" }).put("user_id", answer.optString("userId")),
        )
        return null
    }

    class Session {
        val url = CountDownLatch(1)
        val done = CountDownLatch(1)
        @Volatile var startUrl: String? = null
        @Volatile var result: JSONObject? = null
    }

    companion object {
        private const val TAG = "EaLogin"
        const val REDIRECT = "http://127.0.0.1:31033"
        const val DESKTOP_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36"

        @Volatile private var pending: EaLogin? = null

        /** Starts droiddeck-ea login and waits for its sign-in address (call off the main thread). */
        fun begin(context: Context): String? {
            val app = context.applicationContext
            val session = Session()
            Thread({
                try {
                    GuestCommand.run(app, listOf(EaNative.PROGRAM, "login"), logName = "ea-login") { line ->
                        val o = EaNative.parse(line) ?: return@run
                        when {
                            o.has("url") -> { session.startUrl = o.optString("url"); session.url.countDown() }
                            o.has("loggedIn") || o.has("error") -> session.result = o
                        }
                    }
                } finally {
                    session.url.countDown()
                    session.done.countDown()
                }
            }, "ea-login").start()
            if (!session.url.await(90, TimeUnit.SECONDS)) return "The EA client did not start"
            val url = session.startUrl ?: return session.result?.optString("error")?.ifEmpty { null } ?: "The EA client did not start"
            pending = EaLogin(url, session)
            return null
        }

        fun take(): StoreLoginActivity.Flow? = pending.also { pending = null }
    }
}
