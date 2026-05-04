#!/usr/bin/env bash
set -euo pipefail

BINARY=bin/benchmark
QUEUES="MutexQueue TwoLockQueue PLJQueue MSQueue ValoisQueue LCRQueue LPRQueue"
THREADS="1 2 4 8 16 32 48 64 96 144 192"
TOTAL_OPS=38400000
WORK_ITERS=${2:-100}
MAX_SAMPLES=128000
OUTPUT_DIR=${1:-results-perf}
TRIAL_TIMEOUT=180

PERF_EVENTS="cycles,instructions,L1-dcache-load-misses,LLC-load-misses"

trap 'echo; echo "interrupted"; exit 130' INT

lscpu

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

    perf_out="$OUTPUT_DIR/${queue}_threads${n_threads}.perf"

    if [ -f "$perf_out" ]; then
      echo "   skip (already done)" "$perf_out"
      continue
    fi

    echo "  " num_threads=${n_threads} ops_per_thread=${ops_per_thread} work_iters=${WORK_ITERS}
    if ! timeout "${TRIAL_TIMEOUT}" perf stat -x , -e "$PERF_EVENTS" -o "$perf_out" -- \
          "$BINARY" $ops_per_thread $WORK_ITERS $queue $n_threads > /dev/null; then
      rc=$?
      echo "   TIMEOUT (rc=$rc, timeout=${TRIAL_TIMEOUT}): queue=$queue threads=$n_threads"
      echo "   wiping cell and skipping all higher thread counts for $queue"
      echo "$n_threads" > "$marker"
      break
    fi
  done
done
