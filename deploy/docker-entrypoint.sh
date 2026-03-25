#!/bin/sh
set -e
mkdir -p /data/blobs
chown -R vox:vox /data

if [ -n "${VOX_ADMIN_TOKEN}" ]; then
  exec runuser -u vox -- /usr/local/bin/vox-server \
    --listen 0.0.0.0 \
    --port 8080 \
    --db /data/vox_server.db \
    --blobs /data/blobs \
    --admin-token "${VOX_ADMIN_TOKEN}"
fi

exec runuser -u vox -- /usr/local/bin/vox-server \
  --listen 0.0.0.0 \
  --port 8080 \
  --db /data/vox_server.db \
  --blobs /data/blobs
