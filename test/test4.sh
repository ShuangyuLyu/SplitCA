#! /bin/bash

set -xeuo pipefail
testdir=$(dirname "$0")
root=$(realpath "$testdir"/..)
check_tuples="${1:-$root/build/bin/check_tuples}"

"$root"/build/bin/SplitCA "$testdir"/axtls.cnf "$testdir"/axtls.nnf -o "$testdir"/axtls4.ca -t4 --not_use_addition_tc >"$testdir"/axtls4.log 2>&1
nums=$("$check_tuples" "$testdir"/axtls4.ca 4)

if [[ $nums -ne 37505369 ]]; then
    echo "Wrong at axtls4.ca"
    exit -1
fi
