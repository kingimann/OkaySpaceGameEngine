#!/usr/bin/env bash
# Sign, verify and check the release index (versions.json) for the updater. Docs: docs/online/security.md "Updater signing",
# docs/release/updater.md "Signed releases". A thin wrapper over scripts/sign_index.py (ed25519, RFC 8032, pure Python 3: no
# package to install); the verifier in the launcher and the editor is shared/UpdateSignature.hpp.
#
#   scripts/sign-release.sh sign   <versions.json> [out.sig]     write the signature (default <file>.sig)
#   scripts/sign-release.sh verify <versions.json> <file.sig> [pubkeys]   exit 0 only if it verifies (pubkeys default $OKAY_UPDATE_TRUSTED_KEYS)
#   scripts/sign-release.sh check-key                            does the signing seed match a public key the programs trust?
#   scripts/sign-release.sh status                               one line: is a seed set, which public key it makes (never the seed)
#
# The PRIVATE seed (64 hex digits) comes only from the environment variable OKAY_UPDATE_SIGNING_SEED: in GitHub, a repository
# SECRET with that name (Settings > Secrets and variables > Actions > Secrets > New repository secret). It is never a file
# or an argument, never printed, never logged; the script refuses to run under `set -x` while it is set. The matching PUBLIC key
# goes in the repository VARIABLE OKAY_UPDATE_TRUSTED_KEYS (the build bakes it into the launcher and the editor).
#
# Without the seed, `sign` and `check-key` SKIP with a clear message and exit 0, so a workflow step that calls them does not fail
# on a repository that has not set the secret up yet; REQUIRE_SIGNED=1 turns a skip into a failure (use it once releases must be signed).
set -uo pipefail
case "$-" in *x*) if [ -v OKAY_UPDATE_SIGNING_SEED ]; then echo "sign-release: refusing to run with 'set -x' while OKAY_UPDATE_SIGNING_SEED is set (the trace would print it)" >&2; exit 1; fi;; esac
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SIGN=(python3 -I "$REPO/scripts/sign_index.py")
die() { echo "sign-release: $*" >&2; exit 1; }
skip() { echo "sign-release: SKIPPED: $* (the release will be UNSIGNED and the update signature check cannot protect it)"; [ "${REQUIRE_SIGNED:-0}" = 1 ] && exit 1; exit 0; }
seed_ok() {
    [ -n "${OKAY_UPDATE_SIGNING_SEED:-}" ] || return 1
    [[ "$OKAY_UPDATE_SIGNING_SEED" =~ ^[0-9a-fA-F]{64}$ ]] || die "OKAY_UPDATE_SIGNING_SEED is not 64 hex digits"
}

cmd="${1:-}"; shift || true
case "$cmd" in
    sign)
        [ $# -ge 1 ] || die "usage: sign <versions.json> [out.sig]"
        f="$1"; out="${2:-$1.sig}"
        [ -f "$f" ] || die "no such file: $f"
        seed_ok || skip "OKAY_UPDATE_SIGNING_SEED is not set"
        "${SIGN[@]}" sign --file "$f" --out "$out" || die "could not sign $f"
        pub="$("${SIGN[@]}" pubkey)" || die "could not derive the public key"
        echo "sign-release: signed $f -> $out (public key $pub)"
        trusted="${OKAY_UPDATE_TRUSTED_KEYS:-}"
        if [ -n "$trusted" ]; then
            "${SIGN[@]}" verify --file "$f" --sig "$out" --pub "$trusted" >/dev/null 2>&1 ||
                die "the new signature does not verify under OKAY_UPDATE_TRUSTED_KEYS: the programs built with that key would refuse this release"
            echo "sign-release: verified under the trusted keys"
        fi
        ;;
    verify)
        [ $# -ge 2 ] || die "usage: verify <versions.json> <file.sig> [pubkeys]"
        pub="${3:-${OKAY_UPDATE_TRUSTED_KEYS:-}}"
        [ -n "$pub" ] || die "no public key: pass it or set OKAY_UPDATE_TRUSTED_KEYS"
        "${SIGN[@]}" verify --file "$1" --sig "$2" --pub "$pub" || exit 1
        ;;
    check-key)
        seed_ok || skip "OKAY_UPDATE_SIGNING_SEED is not set"
        trusted="${OKAY_UPDATE_TRUSTED_KEYS:-}"
        [ -n "$trusted" ] || die "OKAY_UPDATE_SIGNING_SEED is set but the repository variable OKAY_UPDATE_TRUSTED_KEYS is empty: launchers built now would not check signatures"
        pub="$("${SIGN[@]}" pubkey)" || die "could not derive the public key"
        case ",$trusted," in
            *",$pub,"*) echo "sign-release: the signing key matches a trusted public key ($pub)" ;;
            *) die "the signing seed makes public key $pub, which is NOT in OKAY_UPDATE_TRUSTED_KEYS: launchers would refuse every release signed with it" ;;
        esac
        ;;
    status)
        if seed_ok; then echo "update signing: seed set; public key $("${SIGN[@]}" pubkey)"; else echo "update signing: no seed (OKAY_UPDATE_SIGNING_SEED is not set); releases are unsigned"; fi
        ;;
    *) die "usage: sign-release.sh sign|verify|check-key|status (see the top of the file)" ;;
esac
