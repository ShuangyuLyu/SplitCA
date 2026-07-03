#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-splitca:latest}"
SEED="${SEED:-1}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/artifact_outputs/reproduction}"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath "$OUTPUT_DIR")"

for name_cnf in $(ls "$ROOT_DIR/benchmarks"); do
    name="$(basename "$name_cnf" .cnf)"
    docker run --rm -v "$OUTPUT_DIR":/workspace/artifact_outputs "$IMAGE_NAME" \
        ./build/bin/SplitCA "benchmarks/$name.cnf" -s "$SEED" \
        -o "artifact_outputs/${name}_seed${SEED}.csv" \
        > "${OUTPUT_DIR}/${name}_seed${SEED}.log"
done

echo "full run end"
echo "see \"$OUTPUT_DIR\" for the results"
