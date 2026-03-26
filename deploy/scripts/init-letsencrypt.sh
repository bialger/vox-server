#!/usr/bin/env bash
# Run from the deploy/ directory after DNS points to this server and port 80 is reachable.
# Usage: ./scripts/init-letsencrypt.sh your@email.example

set -euo pipefail

DOMAIN="${DOMAIN:-messenger.bialger.com}"
EMAIL="${1:?Usage: $0 <email-for-letsencrypt>}"

cd "$(dirname "$0")/.."

docker compose up -d nginx vox-server

docker compose --profile certbot run --rm certbot certonly \
  --webroot \
  -w /var/www/certbot \
  -d "${DOMAIN}" \
  --email "${EMAIL}" \
  --agree-tos \
  --non-interactive

echo "Certificates are in volume certbot-conf. Add deploy/nginx/conf.d/20-ssl.conf from 20-ssl.conf.example, then:"
echo "  docker compose exec nginx nginx -s reload"
