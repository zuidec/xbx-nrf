#!/usr/bin/env bash
# One-file configurator build with PyInstaller, into output/:
#   xbx-config-<version>-<os>-<cpu>[.exe]   Windows, macOS; Linux --release
#   xbx-config-<version>-linux-<cpu>-local  Linux without --release
# The version comes first: a name ending in ".1" reads as a man page to
# file managers, which then won't run it.
# Work files (venv, PyInstaller's) stay in build/local/ and build/release/.
#
# Usage (from anywhere): tools/configurator/build.sh [-p] [--release]
#   -p         pristine: recreate the venv and work folder, and delete earlier
#              versions of this file in output/ (same OS, CPU and kind)
#   --release  Linux: build in a Debian 12 container (podman or docker), so
#              the binary runs on Debian 12 (glibc 2.36) and every newer
#              distro; a -local build needs this system's glibc or newer
#
# Needs Python 3 with tkinter and venv, and objdump on Linux (--release: only
# podman or docker). PyInstaller (requirements-build.txt) goes into the
# build's own venv; what goes in the file: xbx-config.spec. PyInstaller
# doesn't cross-build: each OS and CPU builds its own.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
image="xbx-config-build:debian12"

pristine=false
release=false
for arg in "$@"; do
	case "$arg" in
	-p) pristine=true ;;
	--release) release=true ;;
	*) echo "usage: $0 [-p] [--release]" >&2; exit 2 ;;
	esac
done

os="$(uname -s | tr '[:upper:]' '[:lower:]')"
case "$os" in
mingw* | msys* | cygwin*) os=windows ;;
darwin) os=macos ;;
esac

# Report everything missing at once.
missing=()
need_done() {
	[ ${#missing[@]} -eq 0 ] && return
	echo "missing:" >&2
	printf '  - %s\n' "${missing[@]}" >&2
	echo "install them with your system's package manager, then run $0 again" >&2
	exit 1
}

if $release; then
	[ "$os" = linux ] || { echo "error: --release is for Linux; elsewhere run $0" >&2; exit 2; }
	engine=""
	for e in podman docker; do
		if command -v "$e" >/dev/null; then
			engine="$e"
			break
		fi
	done
	[ -n "$engine" ] || missing+=("podman or docker (for --release)")
	need_done

	echo "image: $image ($engine, from Containerfile; the first build takes a minute)"
	"$engine" build --quiet -t "$image" -f "$here/Containerfile" "$here" >/dev/null
	run=("$engine" run --rm -v "$here:/src:z" -e XBX_RELEASE_BUILD=1)
	if [ "$engine" = docker ]; then
		# rootless podman's root is the user already; docker's isn't
		run+=(--user "$(id -u):$(id -g)" -e HOME=/tmp)
	fi
	args=()
	$pristine && args+=(-p)
	"${run[@]}" "$image" /src/build.sh "${args[@]}" | sed "s#/src/#$here/#g"
	exit 0
fi

python="${PYTHON:-python3}"
if ! command -v "$python" >/dev/null; then
	missing+=("Python 3 ($python)")
else
	"$python" -c "import tkinter" 2>/dev/null || missing+=("Python's tkinter module")
	"$python" -c "import venv, ensurepip" 2>/dev/null ||
		missing+=("Python's venv module (with ensurepip)")
fi
if [ "$os" = linux ] && ! command -v objdump >/dev/null; then
	missing+=("objdump (binutils; PyInstaller needs it on Linux)")
fi
need_done

# inside the --release container: the release kind
kind=release
suffix=""
if [ "$os" = linux ] && [ -z "${XBX_RELEASE_BUILD:-}" ]; then
	kind=local
	suffix="-local"
fi
out="$here/output"
venv="$here/build/$kind/venv"
work="$here/build/$kind/pyinstaller"

target="$os-$(uname -m)$suffix" # e.g. linux-x86_64-local
version="$("$python" -c "import sys; sys.path.insert(0, '$here'); import xbxconfig; print(xbxconfig.__version__)")"
name="xbx-config-$version-$target"

if $pristine; then
	rm -rf "$venv" "$work"
	# earlier versions of this file only: same OS, CPU and kind
	for f in "$out"/xbx-config-*-"$target" "$out"/xbx-config-*-"$target".exe; do
		[ -e "$f" ] || continue
		echo "removed: $f"
		rm -f "$f"
	done
fi

if [ ! -x "$venv/bin/python" ] && [ ! -x "$venv/Scripts/python.exe" ]; then
	echo "venv: $venv"
	"$python" -m venv "$venv"
fi
vpy="$venv/bin/python"
[ -x "$vpy" ] || vpy="$venv/Scripts/python.exe"
if ! "$vpy" -m pip install --quiet --disable-pip-version-check \
	-r "$here/requirements-build.txt"; then
	echo "error: pip couldn't install requirements-build.txt (no internet?)" >&2
	exit 1
fi

echo "configurator $version -> $out/$name"

XBX_BUILD_NAME="$name" "$vpy" -m PyInstaller --noconfirm --clean --log-level WARN \
	--distpath "$out" --workpath "$work" "$here/xbx-config.spec"

ext=""
[ "$os" = windows ] && ext=".exe" # PyInstaller adds it
echo "built: $out/$name$ext"
