#!/bin/bash
# Homebrew preflight, including replay when Homebrew restores an upgrade backup.
set -Eeuo pipefail

homebrew_verify_prepared() (
    local bundle="$1" requested_edition="$2"
    source "$(dirname "${BASH_SOURCE[0]}")/install-snow-shot-macos.sh"
    case "$requested_edition" in
        full) ;;
        mini) product_bundle_id=com.snowshot.snow_shot_mini ;;
        *) return 1 ;;
    esac
    work=$(mktemp -d "${TMPDIR:-/tmp}/snow-homebrew-verify.XXXXXX")
    trap 'rm -rf -- "$work"' EXIT
    if ! verify_project_application "$bundle"; then
        cat "$work/diagnostic.log" >&2
        return 1
    fi
)

homebrew_prepare_application() {
    [[ $# -ge 2 && $# -le 3 ]] || { printf '%s\n' 'Expected a DMG path and staged application path.' >&2; return 1; }
    local dmg="$1" bundle="$2" edition="${3:-full}"
    if [[ -e "$bundle" || -L "$bundle" ]]; then
        # A prepared copy or rollback keeps the publisher's bytes and identity.
        # Historical releases restore using their own archived wrapper.
        [[ -d "$bundle" && ! -L "$bundle" ]] || { printf '%s\n' 'Invalid staged Snow Shot application.' >&2; return 1; }
        homebrew_verify_prepared "$bundle" "$edition"
    else
        /bin/bash "$(dirname "${BASH_SOURCE[0]}")/install-snow-shot-macos.sh" \
            --edition "$edition" --dmg "$dmg" --prepare-app "$bundle"
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    export PATH=/usr/bin:/bin:/usr/sbin:/sbin
    homebrew_prepare_application "$@"
fi
