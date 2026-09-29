# Sourced by the build scripts: checks that the active nRF Connect SDK and
# toolchain match the versions pinned in firmware/ncs-version.
# Sets ZEPHYR_BASE if it isn't set yet.

xbx_check_sdk() {
	local fw_dir="$1"
	local pin_file="$fw_dir/ncs-version"
	local NCS_VERSION NCS_TOOLCHAIN_BUNDLE

	if [ ! -f "$pin_file" ]; then
		echo "error: $pin_file not found" >&2
		return 1
	fi
	# shellcheck disable=SC1090
	source "$pin_file"

	local fix="install/launch it with:
  nrfutil sdk-manager install --ncs-version $NCS_VERSION
  nrfutil sdk-manager toolchain launch --ncs-version $NCS_VERSION --shell   (or ncs-shell)"

	export ZEPHYR_BASE="${ZEPHYR_BASE:-$HOME/ncs/$NCS_VERSION/zephyr}"

	# SDK: nrf/VERSION next to the zephyr folder
	local nrf_version_file="$ZEPHYR_BASE/../nrf/VERSION"
	if [ ! -f "$nrf_version_file" ]; then
		echo "error: no nRF Connect SDK found at $ZEPHYR_BASE/.. (expected $NCS_VERSION)" >&2
		echo "$fix" >&2
		return 1
	fi
	local major minor patch
	major=$(sed -n 's/^VERSION_MAJOR *= *//p' "$nrf_version_file")
	minor=$(sed -n 's/^VERSION_MINOR *= *//p' "$nrf_version_file")
	patch=$(sed -n 's/^PATCHLEVEL *= *//p' "$nrf_version_file")
	local found="v$major.$minor.$patch"
	if [ "$found" != "$NCS_VERSION" ]; then
		echo "error: SDK at $ZEPHYR_BASE/.. is $found, project is pinned to $NCS_VERSION" >&2
		echo "$fix" >&2
		return 1
	fi

	# Toolchain: west must come from the pinned toolchain bundle
	local west_path
	west_path=$(command -v west || true)
	if [ -z "$west_path" ]; then
		echo "error: west not found; not in the SDK toolchain environment" >&2
		echo "$fix" >&2
		return 1
	fi
	case "$west_path" in
	*/toolchains/"$NCS_TOOLCHAIN_BUNDLE"/*) ;;
	*)
		echo "error: west is $west_path, not from toolchain bundle $NCS_TOOLCHAIN_BUNDLE" >&2
		echo "$fix" >&2
		return 1
		;;
	esac

	echo "sdk:   nRF Connect SDK $NCS_VERSION, toolchain $NCS_TOOLCHAIN_BUNDLE"
}
