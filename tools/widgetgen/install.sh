#!/bin/sh
# Materialise the developer-only widget generator module into a host Unreal
# project, turning the inert BetaHubWidgetGen.uplugin.in template into a real
# .uplugin only at that destination.
#
# The template is kept extension-less in the repository on purpose: the repo
# root is the shipped BetaHub plugin, so a stray *.uplugin here would end up in
# the customer release archive.
#
#   ./install.sh "/mnt/c/Users/upsoft/Documents/Unreal Projects/BetaHub_5_3"

set -eu

SRC_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_DIR=${1:-}

if [ -z "$PROJECT_DIR" ]; then
	echo "usage: $0 <host-unreal-project-dir>" >&2
	exit 2
fi

if [ ! -d "$PROJECT_DIR" ]; then
	echo "error: no such project directory: $PROJECT_DIR" >&2
	exit 1
fi

if ! ls "$PROJECT_DIR"/*.uproject >/dev/null 2>&1; then
	echo "error: $PROJECT_DIR contains no .uproject" >&2
	exit 1
fi

DEST="$PROJECT_DIR/Plugins/BetaHubWidgetGen"

echo "installing developer widget generator"
echo "  from : $SRC_DIR"
echo "  to   : $DEST"

# Replace only Source and the manifest. Binaries/ and Intermediate/ are left
# alone: wiping them fails outright when an editor is running and holding the
# DLL, and rebuilding from scratch is slower for no benefit.
mkdir -p "$DEST"
rm -rf "$DEST/Source"
cp -R "$SRC_DIR/Source" "$DEST/Source"
cp "$SRC_DIR/BetaHubWidgetGen.uplugin.in" "$DEST/BetaHubWidgetGen.uplugin"
cp "$SRC_DIR/README.md" "$DEST/README.md"

echo "installed. Now build the host project's editor target, e.g.:"
echo '  Build.bat <Project>Editor Win64 Development -Project="<...>.uproject"'
