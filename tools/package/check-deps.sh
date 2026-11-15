#!/bin/sh
# Standalone runtime check. Requires only /bin/sh, ldd and standard OS utilities.
set -eu
ap_package_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$ap_package_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ap_mode=${1:---all}
case "$ap_mode" in
    --all|--frontend|--backend) ;;
    *) echo "Usage: $0 [--all|--frontend|--backend]" >&2; exit 2 ;;
esac
ap_failed=0
ap_file_list=runtime-files.txt
if [ "$ap_mode" = --backend ]; then ap_file_list=runtime-backend.txt; fi
if [ ! -f "$ap_package_root/$ap_file_list" ]; then
    echo "MISSING FILE: $ap_file_list" >&2
    exit 1
fi
while IFS= read -r ap_file; do
    if [ -n "$ap_file" ] && [ ! -f "$ap_package_root/$ap_file" ]; then
        echo "MISSING PACKAGE DEPENDENCY: $ap_file" >&2
        ap_failed=1
    fi
done < "$ap_package_root/$ap_file_list"
if ! command -v ldd >/dev/null 2>&1; then
    echo "Missing dependency checker: ldd (usually supplied by libc-bin/glibc)." >&2
    exit 1
fi
check_binary() {
    if [ ! -f "$1" ]; then
        echo "MISSING FILE: $1" >&2
        ap_failed=1
        return
    fi
    ap_result=0
    ap_dependencies=$(ldd "$1" 2>&1) || ap_result=$?
    if [ "$ap_result" -ne 0 ] || printf '%s\n' "$ap_dependencies" | grep -q 'not found'; then
        echo "MISSING/INCOMPATIBLE DEPENDENCIES: $1" >&2
        printf '%s\n' "$ap_dependencies" >&2
        ap_failed=1
    else
        echo "OK: $1"
    fi
}
check_binary "$ap_package_root/bin/apack"
if [ "$ap_mode" != --backend ] && [ -f "$ap_package_root/bin/apack-workbench" ]; then
    check_binary "$ap_package_root/bin/apack-workbench"
    if [ ! -f "$ap_package_root/bin/qt.conf" ]; then
        echo "MISSING FILE: bin/qt.conf" >&2
        ap_failed=1
    fi
    ap_qpa=${QT_QPA_PLATFORM:-}
    ap_qpa=${ap_qpa%%:*}
    if [ -z "$ap_qpa" ]; then
        if [ -n "${WAYLAND_DISPLAY:-}" ] && { [ -f "$ap_package_root/plugins/platforms/libqwayland.so" ] || [ -f "$ap_package_root/plugins/platforms/libqwayland-generic.so" ] || [ -f "$ap_package_root/plugins/platforms/libqwayland-egl.so" ]; }; then ap_qpa=wayland
        elif [ -n "${DISPLAY:-}" ]; then ap_qpa=xcb
        else echo "NOTE: No X11/Wayland session. The GUI needs a desktop session; the TUI/backend can run here."
        fi
    fi
    case "$ap_qpa" in
        wayland*)
            if [ -f "$ap_package_root/plugins/platforms/libqwayland.so" ]; then ap_qpa=wayland
            elif [ -f "$ap_package_root/plugins/platforms/libqwayland-generic.so" ]; then ap_qpa=wayland-generic
            else ap_qpa=wayland-egl
            fi ;;
    esac
    if [ -n "$ap_qpa" ]; then
        check_binary "$ap_package_root/plugins/platforms/libq$ap_qpa.so"
    fi
else
    if [ "$ap_mode" != --backend ]; then echo "Frontend: built-in TUI (bin/apack wizard)."; fi
fi
if [ "$ap_mode" = --all ]; then
    for ap_stub in "$ap_package_root"/bin/ap-stubs/ap-setup-linux-x64-*; do
        if [ -f "$ap_stub" ]; then check_binary "$ap_stub"; fi
    done
fi
if [ "$ap_failed" -ne 0 ]; then
    echo "Install the named system libraries, or restore missing files from the complete package." >&2
    exit 1
fi
echo "Runtime dependency check passed. No Python, CMake or Qt SDK is required."
