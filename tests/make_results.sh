#!/bin/sh
# Regenerate the evidence files in results/ from the current build.

cd "$(dirname "$0")/.." || exit 1
BURS=./build/burs
OUT=results
[ -x "$BURS" ] || { echo "build first: make"; exit 1; }
mkdir -p "$OUT"

$BURS --rules                >"$OUT/rule_tables.txt"
$BURS --suite                >"$OUT/suite_summary.txt"
$BURS --exhaustive           >"$OUT/exhaustive_check.txt"
$BURS --bench                >"$OUT/benchmark.txt"
sh tests/run_tests.sh        >"$OUT/test_report.txt" 2>&1

: >"$OUT/sample_runs.txt"
for e in "a + b * c" "(a + b) * c" "a + b + c" "a * (b + c)" "a - b + c" "load(a) + 4"; do
	$BURS -v "$e" >>"$OUT/sample_runs.txt"
	printf '\n\n' >>"$OUT/sample_runs.txt"
done

ls -1 "$OUT"
