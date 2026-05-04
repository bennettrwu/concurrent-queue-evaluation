#!/usr/bin/env bash
set -euo pipefail

BINARY=bin/benchmark
CALIBRATE=bin/calibrate
QUEUES="MutexQueue TwoLockQueue PLJQueue MSQueue ValoisQueue LCRQueue LPRQueue"
THREADS="1 2 4 8 16 32 64 128 256"
TRIALS=5
TOTAL_OPS=${TOTAL_OPS:-64000000}
WORK_ITERS=100
MAX_SAMPLES=100000
OUTPUT_DIR=${1:-results}
TRIAL_TIMEOUT=${TRIAL_TIMEOUT:-60}

trap 'echo; echo "interrupted"; exit 130' INT

mkdir -p "$OUTPUT_DIR"
for queue in $QUEUES; do
  echo $queue
  marker="$OUTPUT_DIR/${queue}.timeout"

  for n_threads in $THREADS; do
    if [ -f "$marker" ] && [ "$n_threads" -ge "$(cat "$marker")" ]; then
      echo "   skip $queue threads=$n_threads (timed out previously at threads=$(cat "$marker"))"
      continue
    fi

    ops_per_thread=$((TOTAL_OPS / n_threads))

    for trial in $(seq 1 $((TRIALS))); do
      out="$OUTPUT_DIR/${queue}_threads${n_threads}_trial${trial}.csv"

      if [ -f "$out" ]; then
        echo "   skip (already done)" "$out"
        continue
      fi

      echo "  " num_threads=${n_threads} trial=${trial} ops_per_thread=${ops_per_thread} work_iters=${WORK_ITERS} max_samples=${MAX_SAMPLES}
      if ! timeout "${TRIAL_TIMEOUT}" "$BINARY" $ops_per_thread $WORK_ITERS $queue $n_threads $out $MAX_SAMPLES > /dev/null; then
        rc=$?
        echo "   TIMEOUT (rc=$rc, timeout=${TRIAL_TIMEOUT}): queue=$queue threads=$n_threads trial=$trial"
        echo "   wiping cell and skipping all higher thread counts for $queue"
        rm -f "$OUTPUT_DIR/${queue}_threads${n_threads}_trial"*.csv
        echo "$n_threads" > "$marker"
        break 2
      fi
    done
  done
done
