# Build from source

Streamers should install from [Releases](https://github.com/JanitorPuppo/remote-deck-obs-plugin/releases). This page is for people changing the plugin.

You need Visual Studio 2022 or 2026 Build Tools with the C++ workload, CMake 3.28+, and a network connection on first configure.

## Production build

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Do not turn on `ENABLE_REMOTE_DECK_LOCAL_DEV` for anything you give to streamers.

Make the Windows installer (and a zip for manual install):

```
./scripts/package-windows.sh
```

That writes `release/obs-remote-deck-<version>-windows-installer.exe`. Streamers run that file. You need [Inno Setup 6](https://jrsoftware.org/isinfo.php).

A git tag like `1.0.0` builds the same installer on GitHub and attaches it to a draft release.

## Local Remote Deck

This build adds a **Local Remote Deck URL** field (default `http://localhost:3000`) so you can point at an API on this machine. The settings window title says `(local dev)`.

```powershell
cmake --preset windows-x64-dev
cmake --build --preset windows-x64-dev
```

```
./scripts/install-dev.sh
```

Quit OBS first. That script installs the local-dev DLL into OBS.

## Layout

OBS 32+ loads plugins from `C:\ProgramData\obs-studio\plugins`. `%APPDATA%\obs-studio\plugins` is ignored.

```
obs-remote-deck/bin/64bit/obs-remote-deck.dll
obs-remote-deck/data/locale/en-US.ini
```

macOS and Linux CMake presets from the OBS plugin template are still here. Windows is the one we actually build.
