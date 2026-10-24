# Steam game environment

DroidDeck publishes one shared game environment for every Proton title. The app
no longer exposes an environment editor, and there are no per-game profiles: a
game inherits the shared environment, its Proton version and its driver
unchanged. The only per-game choices are the Proton build used to run it and the
driver bundle; both come from the Components page.

Proton choices are applied through Steam's per-app compatibility mapping,
including while Steam is running when its live compatibility helper is attached.
A newly installed Proton that the running client has not discovered yet applies
when Steam next starts.

Each Proton build's original components are preserved, including an explicitly
empty bundle when a component is not shipped. A missing or unreadable original is
an error, not permission to delete the component's files.

Component selections and the launch table are saved with a recovery record.
Publishing the launch table commits the selection: if the app stops before its
settings file is updated, the next component read recovers the matching settings.
An interruption before publication leaves the previous selections in place.
Unused staged packages are removed only after both files have been saved.

The order is inherited process environment, then the selected FEX preset and
built-in settings, then the shared edits. Values are literal strings, with no
shell expansion; quotes are only needed when the consuming program expects them.

For a one-off diagnostic, Steam's own per-app launch options work and are not a
per-game profile: `NAME=value %command%` puts the words in the game's
environment. The `%command%` matters, because Steam appends launch options after
the executable, so bare `NAME=value` arrives as an argument that nothing reads.
`droiddeck-game-env` also moves bare `NAME=value` words into the environment and
says so on stderr, but prefer the `%command%` form.

## Defaults

The initial configuration enables Mesa shader caching with
`MESA_SHADER_CACHE_DISABLE=false`, sets `VKD3D_FEATURE_LEVEL=12_2` and
`VKD3D_SHADER_MODEL=6_9`, and preserves the selected FEX preset. Explicit
removals take precedence over these defaults. FEX's own default remains the
default preset. Shader caching in VKD3D/DXVK, synchronization, ray tracing and
diagnostic logging otherwise retain the runtime's defaults.

On an Adreno 6xx, Turnip does not expose `storageBuffer8BitAccess`, which DXVK 3
requires. A Steam or desktop session installs DXVK 2.7.1 into any Proton whose
DXVK is still the copy that Proton shipped, including after a Proton update put
that copy back. A DXVK package chosen in Components stays for that Proton build.

Forcing `12_2` or `6_9` changes reported capabilities; it cannot implement a
missing Vulkan feature. The old `DXVK_FRAME_RATE` environment variable was
removed from current DXVK; prefer the session frame limiter for ordinary play.

## Launch path and references

DroidDeck publishes an atomic JSON snapshot at
`/root/.config/droiddeck/game-environment.json`. Both its Valve ARM64 Proton
wrapper and adopted third-party Proton wrappers execute `droiddeck-game-env`,
which reads that snapshot for each real game launch and uses `execvpe` to start
Proton. Probe prefix `compatdata/0` and non-launch verbs are unchanged. Malformed
configuration falls back to the inherited environment without evaluating its
contents. Component selection normalizes signed non-Steam prefix IDs to unsigned
IDs.

The published file also carries a generated `dxvkConfig` value the app's own file
never has. The session menu's texture filtering (`core/TextureFiltering`) becomes
`d3d9/d3d11.samplerAnisotropy` and `samplerLodBias` options and applies to every
game. The launcher appends the selected options to `DXVK_CONFIG` after the
environment profile, so a user's own `DXVK_CONFIG` entry keeps its options.
"Auto" texture sharpness is `-log2(panel / session)`, derived when the session is
sized, and applies from the next launch.

Upstream references: [VKD3D capability parsing](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/libs/vkd3d/device.c),
[VKD3D options](https://github.com/HansKristian-Work/vkd3d-proton#environment-variables),
[Proton runtime options](https://github.com/ValveSoftware/Proton/tree/proton_11.0#runtime-config-options),
[Mesa variables](https://docs.mesa3d.org/envvars.html),
[DXVK variables](https://github.com/doitsujin/dxvk#environment-variables),
[DXVK frame-limiter changes](https://github.com/doitsujin/dxvk/releases).

Validation: `./gradlew testDebugUnitTest` and
`python3 -m unittest discover -s tools/tests -p test_game_environment.py`.
Actual game compatibility depends on the installed Proton, VKD3D and Vulkan driver;
these checks do not establish that every device supports feature level 12_2 or SM 6_9.
