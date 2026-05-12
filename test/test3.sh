#! /bin/bash

set -xeuo pipefail
testdir=$(dirname "$0")
root=$(realpath "$testdir"/..)
check_tuples="${1:-$root/build/bin/check_tuples}"

"$root"/build/bin/SplitCA "$testdir"/axtls.nnf -o "$testdir"/axtls1.ca -t3 >"$testdir"/axtls1.log 2>&1
nums1=$("$check_tuples" "$testdir"/axtls1.ca 3)

if [[ $nums1 -ne 916254 ]]; then
    echo "Wrong at axtls1.ca"
    exit -1
fi

"$root"/build/bin/SplitCA "$testdir"/axtls.cnf "$testdir"/axtls.nnf -o "$testdir"/axtls2.ca -t3  >"$testdir"/axtls2.log 2>&1
nums2=$("$check_tuples" "$testdir"/axtls2.ca 3)

if [[ $nums2 -ne 916254 ]]; then
    echo "Wrong at axtls2.ca"
    exit -1
fi
