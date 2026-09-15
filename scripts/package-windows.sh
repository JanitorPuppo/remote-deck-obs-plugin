#!/usr/bin/env bash
# Build the Windows installer. Uses the production windows-x64 build.
# Do not package a local-dev DLL.
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

stage_win="$stage"
if command -v cygpath >/dev/null 2>&1; then
	stage_win="$(cygpath -w "$stage")"
	release_win="$(cygpath -w "$root/release")"
else
	release_win="$root/release"
fi

echo "Building Windows installer..."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$root/scripts/compile-installer.ps1" 2>/dev/null || echo "$root/scripts/compile-installer.ps1")" \
	-SourceDir "$stage_win" \
	-Version "$version" \
	-OutputDir "$release_win"

echo "Done: release/obs-remote-deck-${version}-windows-installer.exe"
echo "Done: release/latest.json"
