#!/usr/bin/env bash
# Smoke-test: run each binary briefly, check frame output looks sane.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BINS=(cube cube_colored)
# Faces usually in front during the default spin; '#' / ';' often stay occluded.
VISIBLE_FACES='@$~+'
RUN_SECS=0.5
fail=0

run_one() {
  local bin=$1
  local out
  out="$(mktemp)"

  if [[ ! -x $bin ]]; then
    echo "FAIL  $bin: missing executable (run make first)"
    rm -f "$out"
    return 1
  fi

  # SIGTERM restores the terminal via onExitSignal; timeout exits 124.
  set +e
  timeout "$RUN_SECS" "./$bin" >"$out" 2>/dev/null
  local ec=$?
  set -e

  if [[ $ec -ne 124 && $ec -ne 0 ]]; then
    echo "FAIL  $bin: unexpected exit $ec"
    rm -f "$out"
    return 1
  fi

  if [[ ! -s $out ]]; then
    echo "FAIL  $bin: empty stdout"
    rm -f "$out"
    return 1
  fi

  # Cursor home / alternate-screen setup should appear in the stream.
  if ! grep -q $'\x1b\\[' "$out"; then
    echo "FAIL  $bin: no ANSI escapes in output"
    rm -f "$out"
    return 1
  fi

  if ! grep -q '[.]' "$out"; then
    echo "FAIL  $bin: no background '.' in frame"
    rm -f "$out"
    return 1
  fi

  local missing=0
  local i
  for ((i = 0; i < ${#VISIBLE_FACES}; i++)); do
    local ch="${VISIBLE_FACES:i:1}"
    if ! grep -qF "$ch" "$out"; then
      echo "FAIL  $bin: missing face glyph '$ch'"
      missing=1
    fi
  done

  rm -f "$out"

  if [[ $missing -ne 0 ]]; then
    return 1
  fi

  echo "ok    $bin"
  return 0
}

for bin in "${BINS[@]}"; do
  if ! run_one "$bin"; then
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  echo "tests failed"
  exit 1
fi

echo "all tests passed"
