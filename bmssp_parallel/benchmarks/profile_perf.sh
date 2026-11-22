#!/usr/bin/env bash
set -euo pipefail

if (($# < 3)) || [[ $2 != -- ]]; then
    printf 'Usage: %s log_path -- bench_synthetic [arguments...]\n' "$0" >&2
    exit 2
fi
log_path=$1
shift 2
perf_bin=${PERF_BIN:-perf}
mkdir -p -- "$(dirname -- "${log_path}")"
export OMP_PROC_BIND=close OMP_PLACES=cores OMP_DYNAMIC=FALSE

probe() {
    local event=$1 status=0 output
    printf '\nProbe %s\n' "${event}"
    output=$("${perf_bin}" stat -e "${event}" -- true 2>&1) || status=$?
    printf '%s\n' "${output}"
    if [[ ${output} == *'<not supported>'* || ${output} == *'<not counted>'* ]]; then
        status=1
    fi
    printf 'Probe exit code: %d\n' "${status}"
    return "${status}"
}

run_profile() {
    uname -sr
    printf 'Command:'
    printf ' %q' "$@"
    printf '\n'
    if ! "${perf_bin}" --version; then
        printf 'perf is unavailable. Running the separate phase-profile binary.\n'
        "$@"
        return $?
    fi
    local -a available=()
    local event status=0
    for event in L1-dcache-load-misses LLC-load-misses cycles instructions context-switches lock:contention_begin; do
        if probe "${event}"; then
            available+=("${event}")
        fi
    done
    if ((${#available[@]})); then
        local counters
        counters=$(IFS=,; printf '%s' "${available[*]}")
        printf '\nProcess-level counters: %s\n' "${counters}"
        "${perf_bin}" stat -e "${counters}" -- "$@" || status=$?
    else
        printf '\nNo requested counters available. Running the separate phase-profile binary.\n'
        "$@" || status=$?
    fi
    printf 'Profile command exit code: %d\n' "${status}"
    return "${status}"
}

run_profile "$@" 2>&1 | tee "${log_path}"
