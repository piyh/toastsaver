#!/bin/sh
# Checks the settings file without a display: --set keeps comments and unknown lines,
# each saver's own settings (SaverOption tables) are written, read back and kept in range,
# an old key is moved over, and an unknown key is refused. Run with `make check`.
set -eu
BIN=${1:-bin/toastsaver}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
export XDG_CONFIG_HOME="$dir"
conf="$dir/toastsaver.conf"
fail() { echo "config-check: $*"; cat "$conf"; exit 1; }
printf '# mine\nGERM_CELLS=90   # lots\nBOOK_PAGE_SECONDS=7\nGERM_SPEED=99\nFOO=bar\n' > "$conf"
"$BIN" --set GERM_SPLASH=5 >/dev/null
grep -qx '# mine' "$conf"                                 || fail "lost a comment line"
grep -qx 'FOO=bar' "$conf"                                || fail "lost a line it doesn't know"
grep -q '^GERM_CELLS=90 *# lots$' "$conf"                 || fail "lost a kept comment or value"
grep -q '^GERM_SPEED=10\( \|$\)' "$conf"                || fail "GERM_SPEED not pulled into range"
grep -q '^GERM_SPLASH=5 ' "$conf"                         || fail "--set of a saver setting not saved"
grep -q '^BOOK_WORDS_PER_MINUTE=100 ' "$conf"             || fail "BOOK_PAGE_SECONDS not moved over"
grep -q 'BOOK_PAGE_SECONDS' "$conf"                       && fail "old key left behind"
grep -q '^GERM_POP=1 .*# ' "$conf"                        || fail "a new saver setting missing its comment"
[ "$(grep -c '^GERM_CELLS=' "$conf")" = 1 ]               || fail "a saver setting written twice"
"$BIN" --set GERM_NOPE=1 >/dev/null 2>&1                  && fail "accepted an unknown key"
"$BIN" --set GERM_POP=7 >/dev/null
grep -q '^GERM_POP=1 ' "$conf"                            || fail "a toggle isn't 0 or 1"
"$BIN" --set GERM_CELLS= >/dev/null
grep -q '^GERM_CELLS=60 ' "$conf"                         || fail "an empty value isn't the default"
echo "settings file OK"
