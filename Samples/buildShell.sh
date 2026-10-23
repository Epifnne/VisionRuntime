#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sample=${1:-anomalyDirectory}
case "$sample" in
	-h|--help)
		printf '%s\n' 'Usage: sh buildShell.sh [sample] [CMake configure options]' \
			'Default: anomalyDirectory; requires Ninja and Qt6 Quick development files.' \
			'Output: Publish/release; replaces the previous design kit after successful deployment.' \
			'Models/images and final GUI packaging are selected in the bundled Designer.' \
			'Declarative products such as businessExports bundle build-time manifests automatically.' \
			'Environment: VISION_SHELL_BUILD_DIR, VISION_SHELL_JOBS'
		exit 0 ;;
esac
if [ "$#" -gt 0 ]; then shift; fi
case "$sample" in
	""|*[!A-Za-z0-9_.-]*) echo "Invalid sample name" >&2; exit 1 ;;
esac
case "$sample" in
	[A-Za-z0-9]*) ;;
	*) echo "Sample name must start with a letter or digit" >&2; exit 1 ;;
esac
build=${VISION_SHELL_BUILD_DIR:-"$root/Build/SampleShell/$sample"}
if [ -f "$root/Samples/$sample/application.cmake" ]; then
	set -- -DVISION_PRODUCT_SAMPLE= "-DVISION_PRODUCT_APPLICATION=$root/Samples/$sample" "$@"
else
	set -- "-DVISION_PRODUCT_SAMPLE=$sample" -DVISION_PRODUCT_APPLICATION= "$@"
fi
cmake -S "$root" -B "$build" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DVISION_BUILD_SERVICE=ON -DVISION_BUILD_SHELL=ON \
	-DVISION_BUILD_DESIGNER=ON -DVISION_BUILD_SAMPLES=OFF "$@"
cmake --build "$build" --config Release --target publishSampleShell --parallel "${VISION_SHELL_JOBS:-4}"
publish_root=$(sed -n 's/^VISION_DESIGN_KIT_ROOT:PATH=//p' "$build/CMakeCache.txt")
if [ -z "$publish_root" ]; then
	echo "Build completed without a design-kit output path" >&2
	exit 1
fi
printf 'Design kit updated: %s/release\nOpen design.sh to assemble the final product in Designer.\n' "$publish_root"
