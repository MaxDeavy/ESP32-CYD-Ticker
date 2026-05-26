#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ENV_FILE="${1:-../deploy.env}"
source "$ENV_FILE"
export SERVER_NAME HOST_PORT="${HOST_PORT:-4546}"
envsubst '${SERVER_NAME} ${HOST_PORT}' < nginx-ticker.conf.template > nginx-ticker.conf
echo "OK: nginx-ticker.conf ($SERVER_NAME:$HOST_PORT)"
