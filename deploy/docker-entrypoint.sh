#!/bin/sh
set -e
mkdir -p /data/blobs
chown -R vox:vox /data

if [ -z "${VOX_SESSION_PEPPER}" ]; then
  echo "vox-server: set VOX_SESSION_PEPPER (non-empty secret for session token HMAC)" >&2
  exit 1
fi

extra=""
if [ -f /data/vox.conf ]; then
  extra="${extra} --config /data/vox.conf"
fi

if [ -n "${VOX_ADMIN_TOKEN}" ]; then
  exec runuser -u vox -- /usr/local/bin/vox-server ${extra} \
    --listen 0.0.0.0 \
    --port 8080 \
    --db /data/vox_server.db \
    --blobs /data/blobs \
    --session-pepper "${VOX_SESSION_PEPPER}" \
    --admin-token "${VOX_ADMIN_TOKEN}"
fi

exec runuser -u vox -- /usr/local/bin/vox-server ${extra} \
  --listen 0.0.0.0 \
  --port 8080 \
  --db /data/vox_server.db \
  --blobs /data/blobs \
  --session-pepper "${VOX_SESSION_PEPPER}"
