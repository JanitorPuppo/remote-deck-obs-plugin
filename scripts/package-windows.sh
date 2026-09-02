#!/usr/bin/env bash
# Build a Windows zip a streamer can install (same layout as a GitHub Release).
# Uses the production windows-x64 build. Do not package a local-dev DLL.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

if command -v jq >/dev/null 2>&1; then
	version="$(jq -r .version buildspec.json)"
else
	version="$(sed -n 's/^[[:space:]]*"version": "\([^"]*\)".*/\1/p' buildspec.json | tail -n 1)"
fi
if [[ -z "$version" || "$version" == "null" ]]; then
	echo "Could not read version from buildspec.json" >&2
	exit 1
fi

if [[ ! -f build_x64/CMakeCache.txt ]]; then
	echo "Configuring windows-x64..."
	cmake --preset windows-x64
fi

echo "Building windows-x64..."
cmake --build --preset windows-x64

stage="$root/release/RelWithDebInfo"
rm -rf "$stage"
mkdir -p "$stage"

echo "Staging plugin files..."
cmake --install build_x64 --config RelWithDebInfo --prefix "$stage"

rm -f "$stage"/obs-remote-deck/bin/64bit/*.pdb
cp -f "$root/scripts/README.md" "$stage/README.md"

zip_name="obs-remote-deck-${version}-windows-x64.zip"
zip_path="$root/release/${zip_name}"
rm -f "$zip_path"

stage_win="$stage"
zip_win="$zip_path"
if command -v cygpath >/dev/null 2>&1; then
	stage_win="$(cygpath -w "$stage")"
	zip_win="$(cygpath -w "$zip_path")"
fi

echo "Writing ${zip_path}..."
powershell.exe -NoProfile -Command \
	"Compress-Archive -Force -Path (Join-Path -Path '${stage_win}' -ChildPath '*') -DestinationPath '${zip_win}'"

echo "Building Windows installer..."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$root/scripts/compile-installer.ps1" 2>/dev/null || echo "$root/scripts/compile-installer.ps1")" \
	-SourceDir "$stage_win" \
	-Version "$version" \
	-OutputDir "$(cygpath -w "$root/release" 2>/dev/null || echo "$root/release")"

echo "Done. Give streamers release/obs-remote-deck-${version}-windows-installer.exe"
