#!/bin/sh
# Regression test runner.
#
#   sh tests/run_tests.sh            compare every case with tests/expected/
#   sh tests/run_tests.sh --update   rewrite tests/expected/ from current output
#
# Each case's output (stdout + stderr + exit status) must match its stored
# expected file exactly.  The suite and exhaustive checks are run as well.

cd "$(dirname "$0")/.." || exit 1
BURS=./build/burs
CASES=tests/cases.txt
EXPECTED=tests/expected
UPDATE=0
[ "$1" = "--update" ] && UPDATE=1

[ -x "$BURS" ] || { echo "build first: make"; exit 1; }
mkdir -p "$EXPECTED"

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

pass=0
fail=0

trim() { sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'; }

while IFS='|' read -r id opts expr; do
	id=$(printf '%s' "$id" | trim)
	case "$id" in ''|'#'*) continue ;; esac
	opts=$(printf '%s' "$opts" | trim)
	expr=$(printf '%s' "$expr" | trim)

	# shellcheck disable=SC2086  # opts is intentionally word-split
	$BURS $opts "$expr" >"$TMP" 2>&1
	echo "exit=$?" >>"$TMP"

	if [ "$UPDATE" = 1 ]; then
		cp "$TMP" "$EXPECTED/$id.out"
		echo "updated  $id"
	elif cmp -s "$TMP" "$EXPECTED/$id.out"; then
		pass=$((pass + 1))
		echo "PASS  $id"
	else
		fail=$((fail + 1))
		echo "FAIL  $id"
		diff "$EXPECTED/$id.out" "$TMP" | head -20 | sed 's/^/      /'
	fi
done <"$CASES"

[ "$UPDATE" = 1 ] && exit 0

for mode in --suite --exhaustive; do
	if $BURS $mode >"$TMP" 2>&1; then
		pass=$((pass + 1))
		echo "PASS  burs $mode ($(tail -1 "$TMP"))"
	else
		fail=$((fail + 1))
		echo "FAIL  burs $mode"
		sed 's/^/      /' "$TMP"
	fi
done

echo
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
