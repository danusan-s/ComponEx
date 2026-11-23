#!/usr/bin/env bash
# Sample a running game with perf and produce flame graphs.
#
#   tools/profile.sh <executable> [seconds=20] [startup-delay-ms=4000]
#
# Writes everything to ./profile-<timestamp>/ :
#   perf.data       raw samples            (perf report -i perf.data)
#   all.svg         flame graph, all threads
#   main.svg        flame graph, main thread only (what bounds frame time)
#   *.folded        collapsed stacks, for tools/ diffing with difffolded.pl
#
# Needs: perf, and FlameGraph's stackcollapse-perf.pl / flamegraph.pl on PATH.
# The executable should be a profiling build: see docs/PROFILING.md.
#
# Uses user-space-only sampling (cpu-clock:u, --user-callchains) because that
# is what works when kernel.perf_event_paranoid is 2, the usual default. See
# docs/PROFILING.md for lifting that restriction.
set -euo pipefail

exe="${1:?usage: $0 <executable> [seconds] [startup-delay-ms]}"
seconds="${2:-20}"
delay_ms="${3:-4000}"

for tool in perf prime-run stackcollapse-perf.pl flamegraph.pl; do
  command -v "$tool" > /dev/null || { echo "missing: $tool" >&2; exit 1; }
done

out="profilings/profile-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out"

# -D skips the startup (asset loading, scene spawn), which would otherwise
# dominate a short capture and has nothing to do with steady-state frames.
timeout -s INT "$((seconds + delay_ms / 1000))" \
  perf record -D "$delay_ms" -F 999 -g --user-callchains -e cpu-clock:u \
  -o "$out/perf.data" -- prime-run "$exe" > "$out/run.log" 2>&1 || true

[ -s "$out/perf.data" ] || { echo "perf recorded nothing; see $out/run.log" >&2; exit 1; }

# The thread with the most samples is the main/render thread.
main_tid=$(perf report -i "$out/perf.data" --no-children --stdio -g none \
  --sort pid 2> /dev/null | grep -v '^#' | grep -v '^$' | head -1 |
  awk '{print $2}' | cut -d: -f1)

perf script -i "$out/perf.data" 2> /dev/null | stackcollapse-perf.pl > "$out/all.folded"
perf script -i "$out/perf.data" --tid "$main_tid" 2> /dev/null |
  stackcollapse-perf.pl > "$out/main.folded"

name="$(basename "$exe")"
flamegraph.pl --title "$name: all threads" --width 1800 "$out/all.folded" > "$out/all.svg"
flamegraph.pl --title "$name: main thread" --width 1800 "$out/main.folded" > "$out/main.svg"

echo
echo "== busiest threads (self time) =="
perf report -i "$out/perf.data" --no-children --stdio -g none --sort pid 2> /dev/null |
  grep -v '^#' | grep -v '^$' | head -5
echo
echo "== top functions (self time, all threads) =="
perf report -i "$out/perf.data" --no-children --stdio -g none --sort symbol 2> /dev/null |
  grep -v '^#' | grep -v '^$' | head -12 | cut -c1-140
echo
echo "Open in a browser:  $out/main.svg   $out/all.svg"
