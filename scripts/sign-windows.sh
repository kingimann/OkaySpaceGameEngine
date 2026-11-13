#!/usr/bin/env bash
# Authenticode-sign Windows programs from Linux (osslsigncode), so Windows shows a publisher name instead of an
# "unknown publisher" SmartScreen warning.
#
#   scripts/sign-windows.sh FILE.exe [FILE.exe ...]
#
# The certificate comes from the ENVIRONMENT only (never put it in a file in this repo, never print it):
#   WINDOWS_SIGN_PFX_BASE64     the code-signing certificate with its private key, a .pfx/.p12 file, base64 encoded
#   WINDOWS_SIGN_PFX_PASSWORD   its password (may be empty if the file has none)
# Optional:
#   WINDOWS_SIGN_TIMESTAMP_URLS space-separated RFC 3161 timestamp servers, tried in order
#                               (default: DigiCert, Sectigo, GlobalSign). A timestamp keeps the signature valid after the
#                               certificate expires, so signing without one is refused.
#   WINDOWS_SIGN_NAME / _URL    the description and URL shown in the signature (default: OkaySpace)
#   SIGN_REQUIRED=1             fail when no certificate is configured (default: skip with a warning)
#
# Behaviour:
#   * no certificate and SIGN_REQUIRED unset -> prints a warning and exits 0 (the release stays unsigned, as before);
#   * a certificate is set and ANY file fails to sign or verify -> exits 1. A release never ships half-signed or silently
#     unsigned when a certificate was provided.
#   * a file that is already signed with the same certificate is signed again (the new signature replaces the old one).
# Sign BEFORE anything records the file's checksum (zip, installer, versions.json), because signing changes the bytes.
set -euo pipefail

if [ "$#" -lt 1 ]; then echo "usage: $0 FILE.exe [FILE.exe ...]" >&2; exit 2; fi

if [ -z "${WINDOWS_SIGN_PFX_BASE64:-}" ]; then
  msg="No Windows code-signing certificate is configured (WINDOWS_SIGN_PFX_BASE64 is empty): the programs are NOT signed."
  if [ "${SIGN_REQUIRED:-0}" = "1" ]; then echo "error: $msg" >&2; exit 1; fi
  echo "warning: $msg"
  [ -n "${GITHUB_ACTIONS:-}" ] && echo "::warning title=Windows programs are not signed::$msg Add the repository secrets WINDOWS_SIGN_PFX_BASE64 and WINDOWS_SIGN_PFX_PASSWORD (see docs/packaging.md)."
  exit 0
fi

command -v osslsigncode >/dev/null 2>&1 || { echo "error: osslsigncode is not installed (apt-get install osslsigncode)" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
umask 077
# The certificate and password go through private temp files, never the command line (visible in ps) and never the log.
printf '%s' "$WINDOWS_SIGN_PFX_BASE64" | base64 -d > "$tmp/cert.pfx" 2>/dev/null || { echo "error: WINDOWS_SIGN_PFX_BASE64 is not valid base64" >&2; exit 1; }
[ -s "$tmp/cert.pfx" ] || { echo "error: the decoded certificate is empty" >&2; exit 1; }
printf '%s' "${WINDOWS_SIGN_PFX_PASSWORD:-}" > "$tmp/pass.txt"

NAME="${WINDOWS_SIGN_NAME:-OkaySpace}"
URL="${WINDOWS_SIGN_URL:-https://github.com/kingimann/OkaySpaceGameEngine}"
TS_URLS="${WINDOWS_SIGN_TIMESTAMP_URLS:-http://timestamp.digicert.com http://timestamp.sectigo.com http://timestamp.globalsign.com/tsa/r6advanced1}"
if [ "$TS_URLS" = "none" ]; then
  # Only for tests with a throwaway certificate: a signature without a timestamp stops being valid when the certificate expires.
  [ "${SIGN_ALLOW_NO_TIMESTAMP:-0}" = "1" ] || { echo "error: WINDOWS_SIGN_TIMESTAMP_URLS=none needs SIGN_ALLOW_NO_TIMESTAMP=1 (tests only)" >&2; exit 1; }
  TS_URLS=""
fi

# The chain used to check our own signatures: every certificate in the .pfx, plus the system roots when present.
openssl pkcs12 -in "$tmp/cert.pfx" -passin "file:$tmp/pass.txt" -nokeys -out "$tmp/chain.pem" >/dev/null 2>&1 || {
  # OpenSSL 3 needs -legacy for old RC2/3DES pfx files.
  openssl pkcs12 -legacy -in "$tmp/cert.pfx" -passin "file:$tmp/pass.txt" -nokeys -out "$tmp/chain.pem" >/dev/null 2>&1 || { echo "error: cannot read the certificate: wrong password or not a .pfx" >&2; exit 1; }; }
[ -f /etc/ssl/certs/ca-certificates.crt ] && cat /etc/ssl/certs/ca-certificates.crt >> "$tmp/chain.pem"

fail=0
for f in "$@"; do
  [ -f "$f" ] || { echo "error: $f does not exist" >&2; fail=1; continue; }
  # A Windows program starts with "MZ"; refuse anything else instead of signing a wrong file.
  [ "$(head -c2 "$f")" = "MZ" ] || { echo "error: $f is not a Windows program (no MZ header)" >&2; fail=1; continue; }
  signed=0
  for ts in ${TS_URLS:-NONE}; do
    tsargs=(-ts "$ts"); [ "$ts" = "NONE" ] && tsargs=()
    if osslsigncode sign -pkcs12 "$tmp/cert.pfx" -readpass "$tmp/pass.txt" -h sha256 "${tsargs[@]}" \
         -n "$NAME" -i "$URL" -in "$f" -out "$tmp/signed.exe" >"$tmp/log.txt" 2>&1; then
      signed=1; break
    fi
    echo "  timestamp server $ts failed for $f, trying the next"
  done
  if [ "$signed" != 1 ]; then
    echo "error: could not sign $f. Last output (no secrets are printed by the tool):" >&2
    sed 's/^/    /' "$tmp/log.txt" | tail -8 >&2
    fail=1; continue
  fi
  # Verify what we just made, before replacing the file. The chain is the certificate's own (plus system roots), so this
  # proves the signature is intact and was made by the supplied certificate. A chain-trust complaint (a self-signed test
  # certificate, an intermediate missing from the .pfx) is a warning for the owner: the signature is still valid, but
  # Windows may not trust it. Anything else is fatal.
  osslsigncode verify -CAfile "$tmp/chain.pem" -in "$tmp/signed.exe" >"$tmp/verify.txt" 2>&1 || true
  if grep -q "Signature verification: ok" "$tmp/verify.txt"; then
    :
  elif grep -qi "self-signed\|unable to get\|certificate verify error\|unable to verify" "$tmp/verify.txt" && grep -q "Number of verified signatures: 1" "$tmp/verify.txt"; then
    echo "warning: $f is signed, but the certificate chain does not verify here (self-signed, or an intermediate certificate is missing from the .pfx): Windows may not trust it"
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::warning title=Signature chain not verified::$f is signed but its certificate chain could not be verified. Include the intermediate certificates in the .pfx."
  else
    echo "error: $f was signed but the signature does not verify:" >&2; sed 's/^/    /' "$tmp/verify.txt" | tail -8 >&2; fail=1; continue
  fi
  mv -f "$tmp/signed.exe" "$f"
  echo "signed $f"
done
exit $fail
