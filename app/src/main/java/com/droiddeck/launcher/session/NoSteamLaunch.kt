package com.droiddeck.launcher.session

import android.content.Context
import com.droiddeck.launcher.frontend.AddedGames
import com.droiddeck.launcher.frontend.Library

object NoSteamLaunch {
    const val PROTON_RUN = "/usr/local/bin/droiddeck-proton-run"
    const val EPIC = "/usr/local/bin/droiddeck-epic"

    class Command(val program: String, val args: List<String>, val name: String)

    fun scope(game: Library.SteamGame): String? = when {
        game.library == Library.ADDED || game.appId > 0 -> game.profileKey
        else -> null
    }?.takeIf { it.isNotEmpty() && Regex("[1-9][0-9]*").matches(it) }

    fun enabled(context: Context, game: Library.SteamGame): Boolean =
        scope(game)?.let { SessionPrefs.noSteam(context, it) } == true

    fun enabled(context: Context, appId: Long): Boolean =
        SessionPrefs.noSteam(context, (appId and 0xFFFFFFFFL).toString())

    fun command(game: Library.SteamGame, added: AddedGames.Game?): Command? = when {
        game.library == Library.ADDED -> added?.let { a -> a.steamAppId?.let { Command(PROTON_RUN, listOf("--app", it.toString()), a.name) } ?: addedCommand(a) }
        game.appId > 0 -> Command(PROTON_RUN, listOf("--app", game.appId.toString()), game.name)
        else -> null
    }

    fun addedCommand(game: AddedGames.Game): Command = Command(
        PROTON_RUN,
        listOf("--app", (game.appId and 0xFFFFFFFFL).toString(), "--dir", game.guestDir, "--options", game.launchOptions, "--", game.guestExe),
        game.name,
    )

    fun settingsId(game: AddedGames.Game): Long = (game.steamAppId?.toLong() ?: game.appId) and 0xFFFFFFFFL

    fun addedGame(context: Context, game: Library.SteamGame): AddedGames.Game? {
        val id = game.appId.toLong() and 0xFFFFFFFFL
        return AddedGames.scan(context).firstOrNull { settingsId(it) == id }
    }

    fun nameFor(program: String?, args: List<String>): String? = when (program) {
        EPIC -> Library.EPIC_LAUNCHER_NAME
        PROTON_RUN -> args.getOrNull(args.indexOf("--app") + 1)?.takeIf { args.contains("--app") }?.let { names[it] }
        else -> null
    }

    val names = java.util.concurrent.ConcurrentHashMap<String, String>()

    fun remember(command: Command) {
        if (command.program == PROTON_RUN) command.args.getOrNull(command.args.indexOf("--app") + 1)?.let { names[it] = command.name }
    }
}
