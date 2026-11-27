#!/bin/sh
# After macdeployqt, before signing: what it leaves pointing outside the app
# (src/app/CMakeLists.txt, target macos_app).
#
# - Qt WebEngine's helper (QtWebEngineProcess.app, inside QtWebEngineCore)
#   still names Homebrew's Qt frameworks by their full paths. It is pointed at
#   the copies in the app's Frameworks.
# - Qt's plugins for modules the app does not use (the virtual keyboard, Qt 3D,
#   the state machine, the timeline) come without their frameworks, which
#   macdeployqt could not find; they could never load. They are removed, with
#   their QML modules.
#
# Usage: finish_bundle.sh path/to/SeventhSlide.app
set -eu

app=$1
frameworks="$app/Contents/Frameworks"

is_macho() {
    file -b "$1" | grep -q 'Mach-O'
}

deps() {
    otool -L "$1" | tail -n +2 | awk '{print $1}'
}

# The helper: /opt/homebrew/.../lib/QtX.framework/... -> the app's copy, seven
# folders up from its MacOS folder.
helper="$frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
if [ -f "$helper" ]; then
    for dep in $(deps "$helper"); do
        case "$dep" in
        /opt/homebrew/* | /usr/local/*)
            framework=${dep#*/lib/}
            if [ -e "$frameworks/$framework" ]; then
                install_name_tool -change "$dep" "@executable_path/../../../../../../../$framework" "$helper"
            else
                echo "finish_bundle: the WebEngine helper needs $dep, which is not in the app" >&2
                exit 1
            fi
            ;;
        esac
    done
fi

# Plugins whose frameworks are not in the app.
find "$app/Contents/PlugIns" -type f -name '*.dylib' | while read -r plugin; do
    is_macho "$plugin" || continue
    for dep in $(deps "$plugin"); do
        case "$dep" in
        @rpath/*.framework/*)
            if [ ! -e "$frameworks/${dep#@rpath/}" ]; then
                rm -f "$plugin"
                break
            fi
            ;;
        esac
    done
done

# Their QML modules (Contents/Resources/qml) link to them; a link to nothing
# fails the signature check. The module goes with its plugin.
broken=$(find "$app/Contents/Resources/qml" -type l ! -exec test -e {} ';' -print)
printf '%s\n' "$broken" | while IFS= read -r link; do
    if [ -n "$link" ]; then
        rm -rf "$(dirname "$link")"
    fi
done

# Nothing else may still point at Homebrew.
left=$(find "$app/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) | while read -r f; do
    is_macho "$f" || continue
    deps "$f" | grep -E '^(/opt/homebrew|/usr/local)/' | sed "s|^|$f -> |" || true
done)
if [ -n "$left" ]; then
    echo "finish_bundle: these still point outside the app:" >&2
    echo "$left" >&2
    exit 1
fi
