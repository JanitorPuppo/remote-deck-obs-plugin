#!/usr/bin/env bash
# Build the local-dev plugin and install it into OBS (ProgramData).
# Quit OBS first — Windows locks the DLL while it is running.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

dest_win="${PROGRAMDATA:-${ALLUSERSPROFILE:-C:\\ProgramData}}\\obs-studio\\plugins"
if command -v cygpath >/dev/null 2>&1; then
	dest_win="$(cygpath -w "${PROGRAMDATA:-/c/ProgramData}/obs-studio/plugins")"
fi

obs_running() {
	tasklist 2>/dev/null | grep -qiE '^obs64\.exe|^obs\.exe'
}

if obs_running; then
	echo "Quit OBS first. The plugin DLL is locked while OBS is running." >&2
	exit 1
fi

if [[ ! -f build_x64_dev/CMakeCache.txt ]]; then
	echo "Configuring windows-x64-dev..."
	cmake --preset windows-x64-dev
fi

echo "Building windows-x64-dev..."
cmake --build --preset windows-x64-dev

echo "Installing into ${dest_win}..."
cmake --install build_x64_dev --config RelWithDebInfo --prefix "${dest_win}"

installed="${dest_win}\\obs-remote-deck\\bin\\64bit\\obs-remote-deck.dll"
if command -v cygpath >/dev/null 2>&1; then
	installed_unix="$(cygpath -u "$installed")"
else
	installed_unix="$installed"
fi

if [[ ! -f "$installed_unix" ]]; then
	echo "Install finished but ${installed} was not found." >&2
	exit 1
fi

echo "Installed $(date -r "$installed_unix" '+%Y-%m-%d %H:%M:%S') ${installed}"
echo "Restart OBS. Tools → Remote Deck should say (local dev)."
