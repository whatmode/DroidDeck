//! DroidDeck's EA client: a small command-line front end on maxima-lib.
//!
//! Every result is one line on stdout starting with "@ea " followed by a JSON object; anything
//! else on stdout or stderr is log text.
//!
//!   droiddeck-ea login                       prints {"url"} to open, then waits for EA's
//!                                            redirect to 127.0.0.1:31033 and prints {"loggedIn"}
//!   droiddeck-ea status                      {"loggedIn", "name", "userId"}
//!   droiddeck-ea logout
//!   droiddeck-ea library                     {"games": [...]}
//!   droiddeck-ea install OFFER DIR           {"progress"} lines, then {"installed", "exe"}
//!   droiddeck-ea run OFFER EXE -- COMMAND... Steam launch option: licenses the game in the
//!                                            Proton prefix of $STEAM_COMPAT_DATA_PATH, serves
//!                                            LSX and runs COMMAND (Steam's %command%)
//!
//! When started as "maxima-bootstrap" (maxima-lib's game launcher) it runs the command `run`
//! left in DROIDDECK_EA_COMMAND, keeping the EA environment maxima-lib set.

use std::{
    env,
    io::Write,
    path::{Path, PathBuf},
    process::{Command, ExitCode},
    time::{Duration, Instant},
};

use anyhow::{anyhow, bail, Context, Result};
use log::{error, info, warn, Level, LevelFilter, Log, Metadata, Record};
use maxima::{
    content::manager::QueuedGameBuilder,
    core::{
        auth::{context::AuthContext, nucleus_token_exchange},
        clients::JUNO_PC_CLIENT_ID,
        launch::{self, LaunchMode, LaunchOptions},
        manifest::{self, MANIFEST_RELATIVE_PATH},
        LockedMaxima, Maxima, MaximaEvent, MaximaOptionsBuilder,
    },
    util::native::maxima_dir,
};
use serde_json::{json, Value};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    net::TcpListener,
};

const LOGIN_ADDR: &str = "127.0.0.1:31033";
const LOGIN_TIMEOUT: Duration = Duration::from_secs(15 * 60);
const COMMAND_VAR: &str = "DROIDDECK_EA_COMMAND";
const TOUCHED: &str = ".droiddeck-ea-touchup";

struct StderrLog;

impl Log for StderrLog {
    fn enabled(&self, m: &Metadata) -> bool {
        m.level() <= Level::Info || m.target().starts_with("droiddeck_ea")
    }
    fn log(&self, r: &Record) {
        if self.enabled(r.metadata()) {
            eprintln!("[{}] {}: {}", r.level(), r.target(), r.args());
        }
    }
    fn flush(&self) {}
}

static LOGGER: StderrLog = StderrLog;

fn emit(value: Value) {
    let mut out = std::io::stdout().lock();
    let _ = writeln!(out, "@ea {}", value);
    let _ = out.flush();
}

fn main() -> ExitCode {
    let _ = log::set_logger(&LOGGER).map(|()| log::set_max_level(LevelFilter::Info));
    let args: Vec<String> = env::args().collect();
    let argv0 = args.first().map(|a| Path::new(a).file_name().unwrap_or_default().to_string_lossy().to_string()).unwrap_or_default();
    let first = args.get(1).map(String::as_str).unwrap_or("");
    if argv0 == "maxima-bootstrap" || first == "__bootstrap" {
        return bootstrap();
    }
    if argv0 == "droiddeck-ea-wine" {
        return wine(&args[1..]);
    }
    if first == "__wine" {
        return wine(&args[2..]);
    }

    let runtime = tokio::runtime::Builder::new_multi_thread().enable_all().build().expect("tokio");
    match runtime.block_on(dispatch(&args[1..])) {
        Ok(code) => ExitCode::from(code),
        Err(e) => {
            error!("{:#}", e);
            emit(json!({ "error": format!("{:#}", e) }));
            ExitCode::from(1)
        }
    }
}

async fn dispatch(args: &[String]) -> Result<u8> {
    let cmd = args.first().map(String::as_str).unwrap_or("");
    match cmd {
        "login" => login().await.map(|_| 0),
        "status" => status().await.map(|_| 0),
        "logout" => logout().map(|_| 0),
        "library" => library().await.map(|_| 0),
        "install" => {
            let offer = args.get(1).context("install OFFER DIR")?;
            let dir = args.get(2).context("install OFFER DIR")?;
            install(offer, Path::new(dir)).await.map(|_| 0)
        }
        "run" => {
            let offer = args.get(1).context("run OFFER EXE -- COMMAND")?;
            let exe = args.get(2).context("run OFFER EXE -- COMMAND")?;
            let sep = args.iter().position(|a| a == "--").context("run needs -- COMMAND")?;
            run(offer, Path::new(exe), &args[sep + 1..]).await
        }
        _ => bail!("usage: droiddeck-ea login|status|logout|library|install|run"),
    }
}

async fn open_maxima(auth: bool) -> Result<LockedMaxima> {
    let options = MaximaOptionsBuilder::default().load_auth_storage(auth).dummy_local_user(false).build()?;
    Ok(Maxima::new_with_options(options).await?)
}

async fn login() -> Result<()> {
    let mut context = AuthContext::new()?;
    let url = context.nucleus_auth_url(JUNO_PC_CLIENT_ID, "code")?;
    let listener = TcpListener::bind(LOGIN_ADDR).await.context("the sign-in port is busy")?;
    emit(json!({ "url": url }));

    let deadline = Instant::now() + LOGIN_TIMEOUT;
    let code = loop {
        let left = deadline.checked_duration_since(Instant::now()).ok_or_else(|| anyhow!("sign-in timed out"))?;
        let (mut socket, _) = tokio::time::timeout(left, listener.accept()).await.map_err(|_| anyhow!("sign-in timed out"))??;
        let (read, mut write) = socket.split();
        let mut line = String::new();
        BufReader::new(read).read_line(&mut line).await?;
        let target = line.split_whitespace().nth(1).unwrap_or("").to_owned();
        let code = target
            .split_once('?')
            .map(|(_, q)| q)
            .unwrap_or("")
            .split('&')
            .find_map(|kv| kv.strip_prefix("code="))
            .map(str::to_owned);
        let body = if code.is_some() {
            "<html><body style=\"font-family:sans-serif\"><h2>Signed in to EA</h2>You can go back to DroidDeck.</body></html>"
        } else {
            "<html><body>Waiting for EA sign-in...</body></html>"
        };
        let reply = format!("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", body.len(), body);
        let _ = write.write_all(reply.as_bytes()).await;
        if let Some(code) = code {
            break code;
        }
    };
    context.set_code(&code);

    let token = nucleus_token_exchange(&context).await?;
    let maxima = open_maxima(true).await?;
    let maxima = maxima.lock().await;
    maxima.auth_storage().lock().await.add_account(&token).await?;
    let user = maxima.local_user().await?;
    let name = user.player().as_ref().map(|p| p.display_name().to_owned()).unwrap_or_default();
    emit(json!({ "loggedIn": true, "name": name, "userId": user.id() }));
    Ok(())
}

async fn status() -> Result<()> {
    let maxima = open_maxima(true).await?;
    let maxima = maxima.lock().await;
    let logged_in = maxima.auth_storage().lock().await.logged_in().await.unwrap_or(false);
    if !logged_in {
        emit(json!({ "loggedIn": false }));
        return Ok(());
    }
    let user = maxima.local_user().await?;
    let name = user.player().as_ref().map(|p| p.display_name().to_owned()).unwrap_or_default();
    emit(json!({ "loggedIn": true, "name": name, "userId": user.id() }));
    Ok(())
}

fn logout() -> Result<()> {
    let file = maxima_dir()?.join("auth.toml");
    if file.exists() {
        std::fs::remove_file(&file)?;
    }
    emit(json!({ "loggedIn": false }));
    Ok(())
}

async fn library() -> Result<()> {
    let maxima = open_maxima(true).await?;
    let mut maxima = maxima.lock().await;
    let mut games = Vec::new();
    for title in maxima.mut_library().games().await? {
        let base = title.base_offer();
        let offer = base.offer();
        let item = base.product().product().base_item();
        games.push(json!({
            "offerId": base.offer_id(),
            "slug": base.slug(),
            "name": title.name(),
            "contentId": offer.content_id(),
            "displayName": offer.display_name(),
            "cloudSaves": offer.has_cloud_save(),
            "game": serde_json::to_value(item).unwrap_or(Value::Null),
            "extras": title.extra_offers().iter().map(|o| json!({
                "offerId": o.offer_id(),
                "name": o.offer().display_name(),
            })).collect::<Vec<_>>(),
        }));
    }
    emit(json!({ "games": games }));
    Ok(())
}

/// "[HKEY_LOCAL_MACHINE\...\Install Dir]bin\game.exe" -> "bin/game.exe"
fn relative_exe(raw: &str) -> String {
    let rest = match raw.rfind(']') {
        Some(i) if raw.starts_with('[') => &raw[i + 1..],
        _ => raw,
    };
    rest.trim_start_matches(['\\', '/']).replace('\\', "/")
}

async fn install(offer_id: &str, dir: &Path) -> Result<()> {
    if !dir.is_absolute() {
        bail!("the install folder must be an absolute path");
    }
    std::fs::create_dir_all(dir)?;
    // Touchup needs the game's Proton prefix, so it runs before the first start instead.
    env::set_var("MAXIMA_SKIP_TOUCHUP", "1");

    let maxima_arc = open_maxima(true).await?;
    // Files that fail are deleted, and finished files are skipped on the next pass, so another
    // pass only fetches what is missing.
    let passes = 3;
    for pass in 1..=passes {
        {
            let mut maxima = maxima_arc.lock().await;
            let builds = maxima.content_manager().service().available_builds(offer_id).await?;
            let build = builds.live_build().ok_or_else(|| anyhow!("EA offers no build of this game to download"))?;
            info!("installing build {} (pass {}/{})", build.to_string(), pass, passes);
            let game = QueuedGameBuilder::default()
                .offer_id(offer_id.to_owned())
                .build_id(build.build_id().to_owned())
                .path(dir.to_path_buf())
                .build()?;
            maxima.content_manager().install_now(game).await?;
        }

        let mut failed = 0usize;
        let mut last = Instant::now() - Duration::from_secs(10);
        loop {
            let mut maxima = maxima_arc.lock().await;
            let _ = maxima.consume_pending_events();
            let finished = match maxima.content_manager().current() {
                Some(d) => {
                    failed = d.failed_files();
                    if last.elapsed() >= Duration::from_secs(1) || d.is_done() {
                        emit(json!({ "progress": d.percentage_done(), "done": d.bytes_downloaded(), "total": d.bytes_total() }));
                        last = Instant::now();
                    }
                    d.is_done()
                }
                None => true,
            };
            if finished {
                // Let maxima retire the download (it saves its queue) before the next pass.
                maxima.update().await;
                break;
            }
            maxima.update().await;
            drop(maxima);
            tokio::time::sleep(Duration::from_millis(500)).await;
        }
        if failed == 0 {
            break;
        }
        warn!("{} file(s) did not download on pass {}", failed, pass);
        if pass == passes {
            bail!("{} file(s) could not be downloaded from EA; start the install again to fetch just those", failed);
        }
    }

    let manifest = manifest::read(dir.join(MANIFEST_RELATIVE_PATH)).await.context("the downloaded game has no installer manifest")?;
    let exe = manifest.execute_path(false).map(|p| relative_exe(&p)).unwrap_or_default();
    emit(json!({ "installed": true, "exe": exe, "version": manifest.version() }));
    Ok(())
}

/// Steam's %command% for a non-Steam shortcut: [..., proton, "waitforexitandrun", target...].
fn proton_of(command: &[String]) -> Option<(Vec<String>, usize)> {
    let verb = command.iter().position(|a| a == "waitforexitandrun" || a == "run")?;
    Some((command[..verb].to_vec(), verb))
}

async fn run(offer_id: &str, exe: &Path, command: &[String]) -> Result<u8> {
    if command.is_empty() {
        bail!("no command to run");
    }
    let compat = env::var("STEAM_COMPAT_DATA_PATH").context("run must be started by Steam (no STEAM_COMPAT_DATA_PATH)")?;
    let prefix = PathBuf::from(compat).join("pfx");
    std::fs::create_dir_all(&prefix)?;
    let me = env::current_exe()?;
    let wine = me.with_file_name("droiddeck-ea-wine");
    env::set_var("MAXIMA_WINE_PREFIX", &prefix);
    env::set_var("MAXIMA_WINE_COMMAND", &wine);
    env::set_var("MAXIMA_DISABLE_WINE_VERIFICATION", "1");
    env::set_var(COMMAND_VAR, serde_json::to_string(command)?);

    let dir = exe.parent().context("the game program has no folder")?.to_path_buf();
    let install_root = find_install_root(&dir).unwrap_or_else(|| dir.clone());
    let marker = install_root.join(TOUCHED);
    if !marker.exists() {
        match manifest::read(install_root.join(MANIFEST_RELATIVE_PATH)).await {
            Ok(m) => {
                info!("first start: running the game's touchup in its prefix");
                if let Err(e) = m.run_touchup(&install_root).await {
                    warn!("touchup failed: {}", e);
                }
                let _ = std::fs::write(&marker, b"");
            }
            Err(e) => warn!("no installer manifest under {:?}: {}", install_root, e),
        }
    }

    let maxima_arc = open_maxima(true).await?;
    {
        let maxima = maxima_arc.lock().await;
        if !maxima.auth_storage().lock().await.logged_in().await.unwrap_or(false) {
            bail!("not signed in to EA; sign in from DroidDeck's Stores page");
        }
        maxima.start_lsx(maxima_arc.clone()).await?;
    }

    launch::start_game(
        maxima_arc.clone(),
        LaunchMode::Online(offer_id.to_owned()),
        LaunchOptions { path_override: Some(exe.to_string_lossy().to_string()), arguments: Vec::new(), cloud_saves: true },
    )
    .await
    .map_err(|e| anyhow!("{}", e))?;

    loop {
        let mut maxima = maxima_arc.lock().await;
        let _ = maxima.consume_pending_events();
        maxima.update().await;
        if maxima.playing().is_none() {
            break;
        }
        drop(maxima);
        tokio::time::sleep(Duration::from_secs(1)).await;
    }
    Ok(0)
}

fn find_install_root(start: &Path) -> Option<PathBuf> {
    let mut dir = Some(start);
    while let Some(d) = dir {
        if d.join(MANIFEST_RELATIVE_PATH).exists() {
            return Some(d.to_path_buf());
        }
        dir = d.parent();
    }
    None
}

fn saved_command() -> Option<Vec<String>> {
    serde_json::from_str(&env::var(COMMAND_VAR).ok()?).ok()
}

/// maxima-lib starts "maxima-bootstrap launch <base64 json>" with the EA environment set; run
/// Steam's own command instead, which starts the game through the shortcut's launcher.
fn bootstrap() -> ExitCode {
    let Some(command) = saved_command().filter(|c| !c.is_empty()) else {
        eprintln!("maxima-bootstrap: no {} to run", COMMAND_VAR);
        return ExitCode::from(1);
    };
    match Command::new(&command[0]).args(&command[1..]).status() {
        Ok(s) => ExitCode::from(s.code().unwrap_or(1) as u8),
        Err(e) => {
            eprintln!("maxima-bootstrap: {}: {}", command[0], e);
            ExitCode::from(1)
        }
    }
}

/// maxima-lib's wine command (regedit, touchup): the same Proton Steam chose, with "run".
fn wine(args: &[String]) -> ExitCode {
    let Some((proton, _)) = saved_command().as_deref().and_then(proton_of) else {
        eprintln!("droiddeck-ea-wine: no Proton command in {}", COMMAND_VAR);
        return ExitCode::from(1);
    };
    let verb = match env::var("PROTON_VERB").as_deref() {
        Ok("waitforexitandrun") => "waitforexitandrun",
        _ => "run",
    };
    let mut cmd = Command::new(&proton[0]);
    cmd.args(&proton[1..]).arg(verb).args(args);
    // maxima-lib's umu settings mean nothing to Steam's Proton.
    for var in ["WINEPREFIX", "GAMEID", "PROTONPATH", "STORE", "PROTON_EAC_RUNTIME", "UMU_ZENITY", "PROTON_VERB"] {
        cmd.env_remove(var);
    }
    match cmd.status() {
        Ok(s) => ExitCode::from(s.code().unwrap_or(1) as u8),
        Err(e) => {
            eprintln!("droiddeck-ea-wine: {}: {}", proton[0], e);
            ExitCode::from(1)
        }
    }
}
