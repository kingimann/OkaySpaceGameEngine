#!/usr/bin/env bash
# check_no_sdl_symbols.sh <binary>...  -- fails if a program built with -DOKAY_NO_SDL still references SDL.
# Checks (1) no undefined or defined symbol named SDL_* / ImGui_ImplSDL* in the symbol table, (2) no dynamic dependency on
# an SDL library (ELF: readelf -d NEEDED; PE: objdump -p "DLL Name"). Uses nm / objdump of the target toolchain:
#   NM=x86_64-w64-mingw32-nm OBJDUMP=x86_64-w64-mingw32-objdump scripts/check_no_sdl_symbols.sh OkaySpace.exe
# Written for docs/platform_layer.md (round 14); no third-party code.
set -u
NM=${NM:-nm}; OBJDUMP=${OBJDUMP:-objdump}
rc=0
for f in "$@"; do
    [ -f "$f" ] || { echo "FAIL $f: not found"; rc=1; continue; }
    syms=$("$NM" -A "$f" 2>/dev/null | grep -E ' [A-Za-z] _?(SDL_|ImGui_ImplSDL)' | grep -vE ' [Nn] ' || true)
    # Import-library style names too (__imp_SDL_*)
    imp=$("$NM" "$f" 2>/dev/null | grep -E '__imp_SDL_|_SDL_[A-Za-z]' || true)
    deps=$("$OBJDUMP" -p "$f" 2>/dev/null | grep -iE 'NEEDED.*SDL|DLL Name:.*SDL' || true)
    n=$(printf '%s\n%s\n' "$syms" "$imp" | grep -c . || true)
    if [ -n "$syms$imp$deps" ]; then
        echo "FAIL $f: $n SDL symbol(s), dependencies: ${deps:-none}"
        printf '%s\n%s\n' "$syms" "$imp" | grep . | head -20
        rc=1
    else
        echo "OK   $f: 0 SDL symbols, no SDL library dependency"
    fi
done
exit $rc
