#!/usr/bin/env bash
# Kvrocks single-instance YCSB baseline on local loopback (127.0.0.1:6666).
# Each run starts a fresh Kvrocks pinned to 16 cores on one NUMA node; YCSB runs
# on a disjoint set of cores so the two never share CPU.
# Flow: load (once) -> warmup -> 3 x 180s measured rounds, for workloadc & workloada.

set -uo pipefail

# --- Kvrocks (must be built) ---
KVROCKS=./build/kvrocks
CONF=./configs/kvrocks-bench.conf
NODE=0                  # NUMA node for Kvrocks memory
CPUS=0-15               # 16 cores for Kvrocks
YCSB_CPUS=16-47         # cores for YCSB (must not overlap CPUS)

# --- YCSB (must be built) ---
YCSB=$HOME/nosql/YCSB
OUT=./bench-results
RECORDS=10000000        # 10M records
THREADS=128             # client threads; tune up if Kvrocks 16 cores aren't saturated

log() { echo "[$(date +%H:%M:%S)] $*" >&2; }
die() { echo "ERROR: $*" >&2; exit 1; }

# Start a fresh Kvrocks, pinned to CPUS on NODE. Kills any existing instance.
start_kv() {
  pkill -x kvrocks 2>/dev/null; sleep 1
  log "starting Kvrocks (cpus=$CPUS node=$NODE)"
  numactl --membind=$NODE taskset -c "$CPUS" "$KVROCKS" -c "$CONF" > "$OUT/kvrocks.log" 2>&1 &
  local i
  for (( i=0; i<50; i++ )); do
    </dev/tcp/127.0.0.1/6666 2>/dev/null && return 0
    sleep 0.2
  done
  die "Kvrocks didn't come up (see $OUT/kvrocks.log)"
}

# Run YCSB, pinned to YCSB_CPUS. args: phase(load|run) workload seconds outfile
ycsb() {
  taskset -c "$YCSB_CPUS" "$YCSB/bin/ycsb" "$1" redis \
    -s -P "$YCSB/workloads/$2" \
    -p redis.host=127.0.0.1 -p redis.port=6666 \
    -p recordcount=$RECORDS -p operationcount=100000000 \
    -p maxexecutiontime=$3 \
    -p fieldcount=1 -p readallfields=false \
    -threads $THREADS > "$4" 2>&1 || die "YCSB $1 $2 failed"
  grep -q '\[OVERALL\], Throughput(ops/sec)' "$4" || die "no [OVERALL] in $4"
}

# Read [OVERALL] Throughput from one output file.
tp() { grep '\[OVERALL\], Throughput(ops/sec)' "$1" | awk -F', ' '{printf "%.2f", $3}'; }

load_phase() {
  log "LOAD $RECORDS records ($THREADS threads)"
  ycsb load workloada 0 "$OUT/load.txt"
  log "LOAD done: $(tp "$OUT/load.txt") ops/sec"
}

# Warmup + 3 x 180s measured rounds for one workload.
run_workload() {
  local wl="$1" rounds=() r
  log "WARMUP $wl 90s"
  ycsb run "$wl" 90 "$OUT/warmup_$wl.txt"
  for (( r=1; r<=3; r++ )); do
    ycsb run "$wl" 180 "$OUT/run_${wl}_r${r}.txt"
    rounds+=( "$(tp "$OUT/run_${wl}_r${r}.txt")" )
    log "  $wl round $r: ${rounds[-1]} ops/sec"
  done
  local med; med=$(printf '%s\n' "${rounds[@]}" | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')
  log "RESULT $wl: median=$med ops/sec  rounds=[${rounds[*]}]"
}

bench() {
  run_workload workloadc
  run_workload workloada
}

main() {
  [[ -x "$KVROCKS" ]] || die "Kvrocks binary not found: $KVROCKS"
  [[ -x "$YCSB/bin/ycsb" ]] || die "YCSB launcher not found: $YCSB/bin/ycsb"
  mkdir -p "$OUT"
  start_kv
  case "${1:-}" in
    load)  load_phase ;;
    bench) bench ;;
    all)   load_phase; bench ;;
    *) echo "usage: $0 {load|bench|all}" >&2; exit 1 ;;
  esac
}

main "$@"
