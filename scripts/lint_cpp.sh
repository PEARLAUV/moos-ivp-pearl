#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

mode=check
scope=module
files=()
while (($#)); do
  case "$1" in
    --fix)
      mode=fix
      shift
      ;;
    --all)
      scope=all
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [--fix] [--all | FILE ...]"
      echo "Checks clang-format style in src/pRendezvous by default."
      echo "Use --all to check all tracked C++ sources under src, or pass files."
      exit 0
      ;;
    *)
      files+=("$1")
      shift
      ;;
  esac
done

if ! command -v clang-format >/dev/null 2>&1; then
  echo "$0: clang-format is required." >&2
  exit 127
fi

if ((${#files[@]} == 0)); then
  search_root=src/pRendezvous
  [[ "$scope" == all ]] && search_root=src
  mapfile -d '' files < <(
    find "$search_root" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \
      -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' \
      -o -name '*.hxx' \) -print0 | sort -z
  )
fi

if ((${#files[@]} == 0)); then
  echo "No C++ files to check."
  exit 0
fi

if [[ "$mode" == fix ]]; then
  clang-format -i "${files[@]}"
else
  clang-format --dry-run --Werror "${files[@]}"
fi
