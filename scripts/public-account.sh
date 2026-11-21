# Source me: reads config/public-account.json (docs/online/accounts.md). Plain sed, no jq. Never prints a value.
#   okay_public_account url|key    prints the value (empty if the file or the field is missing)
okay_public_account() {
    local f="${OKAY_PUBLIC_ACCOUNT_FILE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/config/public-account.json}"
    [ -f "$f" ] || return 0
    sed -n -E 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p' "$f" | head -n1
}
