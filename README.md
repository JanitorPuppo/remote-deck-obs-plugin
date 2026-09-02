# Remote Deck OBS plugin

Standalone OBS Studio **Tools** plugin for Remote Deck. It authenticates with Remote Deck, dials *out* to the studio control socket, then applies producer commands (mute / volume) inside OBS through libobs. Producers never receive an OBS host, port, or obs-websocket password.

This is not a source or filter. Settings live in **Tools → Remote Deck**: machine name plus **Authenticate with Remote Deck**. The browser handles sign-in and studio approval. See [docs/REMOTE-DECK-CONTRACT.md](docs/REMOTE-DECK-CONTRACT.md).

Public/release builds always authenticate against production Remote Deck.

## Windows build

Requires Visual Studio 2022 or 2026 Build Tools with the C++ workload, CMake 3.28+, and a network connection on first configure (the OBS plugin template downloads OBS sources and prebuilt Qt/deps). The `windows-x64` preset uses the VS 2026 generator. **Do not enable `ENABLE_REMOTE_DECK_LOCAL_DEV` on a DLL you ship.**

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

To point Remote Deck mode at a local API (field in the dialog, default `http://localhost:3000`):

```powershell
cmake --preset windows-x64-dev
cmake --build --preset windows-x64-dev
```

Install with `./scripts/install-dev.sh` (quits if OBS is still running). The settings window title includes `(local dev)` so you can tell the builds apart.

The first configure can take several minutes. Output lands under `build_x64/rundir/RelWithDebInfo/`.

### Install

Quit OBS, then copy the built plugin folder into OBS's plugin search path (OBS 32+), or run `./scripts/install-dev.sh` for the local-dev build:

```
C:\ProgramData\obs-studio\plugins\obs-remote-deck\bin\64bit\obs-remote-deck.dll
C:\ProgramData\obs-studio\plugins\obs-remote-deck\data\locale\en-US.ini
```

Fully quit and reopen OBS. `%APPDATA%\obs-studio\plugins` is not scanned.

The CMake install layout is:

```
obs-remote-deck/bin/64bit/obs-remote-deck.dll
obs-remote-deck/data/locale/en-US.ini
```

Restart OBS. Open **Tools → Remote Deck**.

macOS and Linux presets from the [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) CMake are kept; Windows is the first-class local build.

## Security notes

- The plugin only makes outbound connections to Remote Deck.
- Tokens are stored in the OBS module config on the streamer PC. They are not written to the OBS log.
- TLS (`wss`) is required except for localhost.

## License

GPL-2.0-or-later, same as the OBS plugin template this repo is adapted from.
