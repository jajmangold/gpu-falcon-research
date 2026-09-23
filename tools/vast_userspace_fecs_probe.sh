#!/usr/bin/env bash
set -euo pipefail

out_dir="${1:-vast-userspace-fecs-$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$out_dir"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
probe_src="$script_dir/rm_sm_issue_rate_probe.c"
probe_bin="$out_dir/rm_sm_issue_rate_probe"

{
  date -u
  uname -a
  id
  printf 'cwd=%s\n' "$PWD"
} > "$out_dir/session.txt" 2>&1 || true

{
  command -v nvidia-smi || true
  command -v nvidia-debugdump || true
  command -v gcc || true
  ls -l /dev/nvidia* 2>/dev/null || true
} > "$out_dir/tools-and-devices.txt" 2>&1 || true

if command -v nvidia-smi >/dev/null 2>&1; then
  nvidia-smi -L > "$out_dir/nvidia-smi-L.txt" 2>&1 || true
  nvidia-smi -q > "$out_dir/nvidia-smi-q.txt" 2>&1 || true
fi

if command -v nvidia-debugdump >/dev/null 2>&1; then
  nvidia-debugdump --list > "$out_dir/nvidia-debugdump-list.txt" 2>&1 || true
  nvidia-debugdump --dumpall > "$out_dir/nvidia-debugdump-dumpall.log" 2>&1 || true
fi

if command -v gcc >/dev/null 2>&1; then
  gcc -O2 -Wall -Wextra -o "$probe_bin" "$probe_src" > "$out_dir/gcc.log" 2>&1 || true
  if [ -x "$probe_bin" ]; then
    if [ -e /dev/nvidiactl ]; then
      for dev in /dev/nvidia[0-9]*; do
        [ -e "$dev" ] || continue
        minor="${dev#/dev/nvidia}"
        case "$minor" in
          *[!0-9]*|'') continue ;;
        esac
        "$probe_bin" "$minor" > "$out_dir/rm-probe-gpu${minor}.txt" 2>&1 || true
      done
    fi
  fi
fi

{
  printf 'FECS targets:\n'
  printf '  0x00409660 GV100 FECS FEATURE_READOUT\n'
  printf '  0x00409664 GV100 FECS SM_SPEED_SELECT override\n'
  printf '\nExpected override decode for VBIOS value 0x999:\n'
  printf '  IMLA/FMLA/DP reduced=1 override=1\n'
} > "$out_dir/README.txt"

tar -czf "${out_dir}.tar.gz" "$out_dir"
printf '%s\n' "${out_dir}.tar.gz"
