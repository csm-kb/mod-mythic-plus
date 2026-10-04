#!/usr/bin/env bash
# Isolated MySQL for SQL validation and live tests. Restores read-only dumps of the real DBs under their
# ORIGINAL names, so schema-qualified module SQL (acore_world.x) can only touch this copy.
# Usage: tools/mp-db.sh up|down|status    Expect: 'up' takes several minutes (dump + restore).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/mp-lib.sh"
mp_require_docker
case "${1:-}" in
  up)
    docker ps --format '{{.Names}}' | grep -qx ac-database \
      || docker compose -f "$MP_ROOT/docker-compose.yml" up -d --wait ac-database >/dev/null
    NET="$(mp_compose_network)"
    docker rm -f "$MP_DB_CONTAINER" >/dev/null 2>&1 || true
    docker volume rm "$MP_DB_VOLUME" >/dev/null 2>&1 || true
    mp_say "starting $MP_DB_CONTAINER on network $NET"
    docker run -d --name "$MP_DB_CONTAINER" --network "$NET" -e MYSQL_ROOT_PASSWORD="$MP_DB_PW" \
      -v "$MP_DB_VOLUME:/var/lib/mysql" mysql:8.4 >/dev/null
    # TCP login, not ping: the image's temporary init server (skip-networking) answers ping before the real one.
    until docker exec "$MP_DB_CONTAINER" mysql -h127.0.0.1 -uroot -p"$MP_DB_PW" -e 'SELECT 1' >/dev/null 2>&1; do
      sleep 2
    done
    DBS=$(docker exec ac-database sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -N -e "SHOW DATABASES"' \
      | grep -xE 'acore_(auth|world|characters|playerbots)' | tr '\n' ' ')
    [[ -n "$DBS" ]] || { echo "mp: no acore_* schemas on the real server" >&2; exit 2; }
    mp_say "copying (read-only dump) $DBS — this takes a few minutes"
    docker exec ac-database sh -c "MYSQL_PWD=\"\$MYSQL_ROOT_PASSWORD\" mysqldump -uroot \
        --single-transaction --routines --triggers --databases $DBS" \
      | docker exec -i -e MYSQL_PWD="$MP_DB_PW" "$MP_DB_CONTAINER" mysql -uroot
    mp_say "PASS db up ($MP_DB_CONTAINER has: $DBS)"
    ;;
  down)
    docker rm -f "$MP_DB_CONTAINER" >/dev/null 2>&1 || true
    docker volume rm "$MP_DB_VOLUME" >/dev/null 2>&1 || true
    mp_say "db down"
    ;;
  status)
    docker ps --filter "name=^$MP_DB_CONTAINER$" --format '{{.Names}} {{.Status}}'
    ;;
  *) echo "usage: $0 up|down|status" >&2; exit 2 ;;
esac
