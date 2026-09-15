#!/usr/bin/env bash
# Build the local-dev plugin and install it into OBS (per-user AppData).
# Quit OBS first — Windows locks the DLL while it is running.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

plugin_id="obs-remote-deck"
build_dir="$root/build_x64_dev"
config="RelWithDebInfo"
build_dll="$build_dir/$config/${plugin_id}.dll"
build_pdb="$build_dir/$config/${plugin_id}.pdb"
locale_src="$root/data/locale/en-US.ini"

appdata="${APPDATA:-${HOME}/AppData/Roaming}"
program_data="${PROGRAMDATA:-${ALLUSERSPROFILE:-C:/ProgramData}}"
dest_root="${appdata}/obs-studio/plugins/${plugin_id}"
dest_bin="${dest_root}/bin/64bit"
dest_locale="${dest_root}/data/locale/en-US.ini"
legacy_root="${program_data}/obs-studio/plugins/${plugin_id}"
legacy_dll="${legacy_root}/bin/64bit/${plugin_id}.dll"

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

if [[ ! -f "$build_dll" ]]; then
	echo "Build finished but ${build_dll} was not found." >&2
	exit 1
fi

echo "Installing into ${dest_root}..."
mkdir -p "$dest_bin" "$(dirname "$dest_locale")"
cp -f "$build_dll" "${dest_bin}/${plugin_id}.dll"
if [[ -f "$build_pdb" ]]; then
	cp -f "$build_pdb" "${dest_bin}/${plugin_id}.pdb" || echo "Note: could not copy PDB (non-fatal)."
fi

if [[ -f "$legacy_dll" ]]; then
	echo "Removing stale all-users plugin copy (OBS may load this instead of AppData):"
	echo "  ${legacy_root}"
	rm -rf "$legacy_root" 2>/dev/null || echo "Note: could not remove ProgramData copy (run as admin or use the installer)."
fi

dest_tls="${dest_root}/data/tls"
tls_backend="$(find "$root/.deps" -path '*/plugins/tls/qschannelbackend.dll' 2>/dev/null | head -n 1 || true)"
if [[ -n "$tls_backend" ]]; then
	tls_src="$(dirname "$tls_backend")"
	mkdir -p "$dest_tls"
	cp -f "${tls_src}/qschannelbackend.dll" "${tls_src}/qcertonlybackend.dll" "$dest_tls/"
	rm -rf "${dest_root}/tls"
else
	echo "Warning: Qt TLS plugins not found under .deps; HTTPS auth will fail." >&2
fi

locale_note=""
if [[ -f "$locale_src" ]]; then
	if ! cp -f "$locale_src" "$dest_locale" 2>/dev/null; then
		locale_note="Locale file could not be updated (permission denied). The DLL was installed; restart OBS."
	fi
fi

installed="${dest_bin}/${plugin_id}.dll"
echo "Installed $(date -r "$installed" '+%Y-%m-%d %H:%M:%S') ${installed}"
if [[ -n "$locale_note" ]]; then
	echo "Note: ${locale_note}" >&2
fi
echo "Restart OBS. Tools → Remote Deck should say (local dev)."
