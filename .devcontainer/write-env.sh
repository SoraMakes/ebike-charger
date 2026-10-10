#!/usr/bin/env bash
# Host paths for the compose volume mounts.
set -euo pipefail

ws="${1:?usage: write-env.sh <local-workspace-folder>}"
[ -e "$ws/.git" ] || { echo "write-env.sh: no .git in $ws" >&2; exit 1; }
common="$(git -C "$ws" rev-parse --path-format=absolute --git-common-dir)"
[ -e "$common" ] || { echo "write-env.sh: common dir missing: $common" >&2; exit 1; }

printf 'LOCAL_WORKSPACE_FOLDER="%s"\nGIT_COMMON_DIR="%s"\n' "$ws" "$common" \
  > "$(dirname "$0")/.env"
