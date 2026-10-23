#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

make clean
make -j4 PMM=buddy

output=$(mktemp)
trap 'rm -f "$output"' EXIT HUP INT TERM

# The kernel intentionally stays in its idle loop after initialization.
set +e
timeout 8s make qemu PMM=buddy >"$output" 2>&1
set -e

for marker in \
    "memory management: buddy_system_pmm_manager" \
    "[Buddy] init_memmap: PASS" \
    "[Buddy] split: PASS" \
    "[Buddy] coalesce: PASS" \
    "[Buddy] non-power-of-two request: PASS" \
    "[Buddy] insufficient memory: PASS" \
    "check_alloc_page() succeeded!"
do
    if ! grep -F "$marker" "$output" >/dev/null; then
        echo "Buddy check failed: missing '$marker'" >&2
        sed -n '/memory management:/,$p' "$output" >&2
        exit 1
    fi
done

echo "Buddy System integration check: PASS"
