#!/usr/bin/env bash
# Tests scripts/sign-windows.sh with a throwaway self-signed certificate and a tiny Windows program built with MinGW.
# Needs osslsigncode, openssl and a MinGW compiler; exits 77 (skipped) when one is missing.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
sign="$here/../../scripts/sign-windows.sh"
for t in osslsigncode openssl x86_64-w64-mingw32-gcc base64; do
  command -v "$t" >/dev/null 2>&1 || { echo "skipped: $t not found"; exit 77; }
done
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
check() { if [ "$1" = 0 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fail=1; fi; }

cd "$T" || exit 1
printf 'int main(void){return 0;}\n' > h.c
x86_64-w64-mingw32-gcc h.c -o prog.exe || { echo "cannot build the test program"; exit 77; }
openssl req -x509 -newkey rsa:2048 -nodes -keyout k.pem -out c.pem -days 2 -subj "/CN=Test Publisher" \
  -addext "extendedKeyUsage=codeSigning" -addext "keyUsage=digitalSignature" >/dev/null 2>&1
PW='s3cret-Passw0rd!'
openssl pkcs12 -export -inkey k.pem -in c.pem -out c.pfx -passout "pass:$PW" >/dev/null 2>&1
B64="$(base64 -w0 c.pfx)"
export SIGN_ALLOW_NO_TIMESTAMP=1 WINDOWS_SIGN_TIMESTAMP_URLS=none
unset GITHUB_ACTIONS

echo "== no certificate configured"
cp prog.exe a.exe; h0=$(sha256sum a.exe | cut -d' ' -f1)
env -u WINDOWS_SIGN_PFX_BASE64 "$sign" a.exe >out.txt 2>&1; check $? "skips with exit 0"
grep -q "NOT signed" out.txt; check $? "says the programs are not signed"
[ "$(sha256sum a.exe | cut -d' ' -f1)" = "$h0" ]; check $? "leaves the file untouched"
env -u WINDOWS_SIGN_PFX_BASE64 SIGN_REQUIRED=1 "$sign" a.exe >/dev/null 2>&1; [ $? -ne 0 ]; check $? "SIGN_REQUIRED=1 fails without a certificate"

echo "== signing"
cp prog.exe b.exe
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" b.exe >out.txt 2>&1; check $? "signs with exit 0"
[ "$(head -c2 b.exe)" = "MZ" ]; check $? "still a Windows program"
[ "$(sha256sum b.exe | cut -d' ' -f1)" != "$h0" ]; check $? "the file changed"
osslsigncode verify -CAfile c.pem -in b.exe 2>&1 | grep -q "Signature verification: ok"; check $? "the signature verifies against the certificate"
osslsigncode verify -CAfile c.pem -in prog.exe >/dev/null 2>&1; [ $? -ne 0 ]; check $? "the unsigned original does not verify"
! grep -qF "$PW" out.txt && ! grep -qF "${B64:0:60}" out.txt; check $? "neither the password nor the certificate appears in the output"
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" b.exe >/dev/null 2>&1; check $? "signing an already signed file works"
cp prog.exe c1.exe; cp prog.exe c2.exe
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" c1.exe c2.exe >/dev/null 2>&1; check $? "several files in one call"
osslsigncode verify -CAfile c.pem -in c2.exe 2>&1 | grep -q "Signature verification: ok"; check $? "each of them verifies"

echo "== refusals leave files alone and fail"
cp prog.exe d.exe; h1=$(sha256sum d.exe | cut -d' ' -f1)
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="wrong" "$sign" d.exe >out.txt 2>&1; [ $? -ne 0 ]; check $? "wrong password fails"
[ "$(sha256sum d.exe | cut -d' ' -f1)" = "$h1" ]; check $? "and leaves the file unchanged"
! grep -qF "wrong" out.txt || true
WINDOWS_SIGN_PFX_BASE64="not base64 !!!" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" d.exe >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a broken certificate value fails"
printf 'just text\n' > notexe.txt
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" notexe.txt >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a file that is not a Windows program is refused"
[ "$(cat notexe.txt)" = "just text" ]; check $? "and left alone"
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" missing.exe >/dev/null 2>&1; [ $? -ne 0 ]; check $? "a missing file fails"
WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" "$sign" d.exe notexe.txt >/dev/null 2>&1; [ $? -ne 0 ]; check $? "one bad file in the list makes the whole call fail"
env -u SIGN_ALLOW_NO_TIMESTAMP WINDOWS_SIGN_PFX_BASE64="$B64" WINDOWS_SIGN_PFX_PASSWORD="$PW" WINDOWS_SIGN_TIMESTAMP_URLS=none "$sign" d.exe >/dev/null 2>&1; [ $? -ne 0 ]; check $? "no timestamp is refused unless the test override is set"

[ $fail = 0 ] && echo "sign_windows: all checks passed" || echo "sign_windows: FAILED"
exit $fail
