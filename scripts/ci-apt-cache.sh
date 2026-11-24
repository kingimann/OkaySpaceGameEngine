#!/usr/bin/env bash
# CI helper for the Actions cache of downloaded .deb files (~/apt-cache), used with scripts/ci-install-mingw.sh by ci.yml
# (the Windows PR check) and release.yml.
#   scripts/ci-apt-cache.sh before            record which files the restored cache holds
#   scripts/ci-apt-cache.sh after <outcome>   tidy the folder; print save=1 to $GITHUB_OUTPUT when its set of files changed
# <outcome> is the install step's outcome. After a good install, files of packages that are not installed (an older cache's
# posix compiler, another job's extra tools, old versions) are dropped so the saved cache stays small.
set -u
DIR="${OKAY_APT_ARCHIVES:-$HOME/apt-cache}"
list() { ls "$DIR"/*.deb 2>/dev/null | xargs -r -n1 basename | sort | md5sum; }
case "${1:-}" in
before)
    list > "$HOME/apt-debs-before" ;;
after)
    [ -d "$DIR" ] || exit 0
    # A timed-out install can leave apt-get or curl still downloading.
    sudo pkill -x apt-get || true
    sudo pkill -x curl || true
    sudo chown -R "$USER" "$DIR"
    rm -rf "$DIR/partial" "$DIR/lock"
    if [ "${2:-}" != "failure" ] && command -v x86_64-w64-mingw32-g++ >/dev/null; then
        for f in "$DIR"/*.deb; do
            [ -f "$f" ] || continue
            pkg=$(dpkg-deb -f "$f" Package); ver=$(dpkg-deb -f "$f" Version)
            [ "$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null)" = "$ver" ] || rm -f "$f"
        done
    fi
    n=$(ls "$DIR"/*.deb 2>/dev/null | wc -l)
    echo "cache: $n files, $(du -sm "$DIR" | cut -f1) MB"
    if [ "$n" -gt 0 ] && [ "$(list)" != "$(cat "$HOME/apt-debs-before" 2>/dev/null)" ]; then
        echo "save=1" >> "${GITHUB_OUTPUT:-/dev/stdout}"
    fi ;;
*)
    echo "usage: $0 before | after <install outcome>" >&2; exit 2 ;;
esac
