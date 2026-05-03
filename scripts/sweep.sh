#!/usr/bin/env bash
set -euo pipefail

BINARY=bin/benchmark
CALIBRATE=bin/calibrate
QUEUES="MutexQueue TwoLockQueue PLJQueue MSQueue ValoisQueue LCRQueue LPRQueue"
THREADS="1 2 4 8 16"
TRIALS=5
OPS_PER_THREAD=1000000
WORK_NS=100
MAX_SAMPLES=100000
OUTPUT_DIR=${1:-results}

mkdir -p "$OUTPUT_DIR"

WORK_ITERS=$($CALIBRATE $WORK_NS)
echo "Calibrated: ${WORK_ITERS} iters for ${WORK_NS} ns of inter-op work"

for queue in $QUEUES; do
  echo $queue

  for n_threads in $THREADS; do
    for trial in $(seq 1 $((TRIALS))); do
      out="$OUTPUT_DIR/${queue}_threads${n_threads}_trial${trial}.csv"

      echo "  " num_threads=${n_threads} trial=${trial} work_iters=${WORK_ITERS} max_samples=${MAX_SAMPLES}
      $BINARY $OPS_PER_THREAD $WORK_ITERS $queue $n_threads $out $MAX_SAMPLES >/dev/null
    done
  done
done
