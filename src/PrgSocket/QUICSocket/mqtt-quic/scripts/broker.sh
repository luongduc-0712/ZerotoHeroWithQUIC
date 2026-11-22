#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
PROJECT=mqtt-quic-testbed
COMPOSE_DIR="$ROOT/docker"
compose() { docker compose --project-name "$PROJECT" --project-directory "$COMPOSE_DIR" -f "$COMPOSE_DIR/compose.yaml" "$@"; }
check_owner() {
    if docker container inspect emqx-quic >/dev/null 2>&1; then
        local project service directory
        project=$(docker inspect -f '{{index .Config.Labels "com.docker.compose.project"}}' emqx-quic)
        service=$(docker inspect -f '{{index .Config.Labels "com.docker.compose.service"}}' emqx-quic)
        directory=$(docker inspect -f '{{index .Config.Labels "com.docker.compose.project.working_dir"}}' emqx-quic)
        if [[ "$project" != "$PROJECT" || "$service" != emqx || "$directory" != "$COMPOSE_DIR" ]]; then
            echo "Refusing to modify emqx-quic: not owned by this Compose project and directory." >&2
            echo "Existing project=$project service=$service working_dir=$directory" >&2
            exit 1
        fi
    fi
}
case "${1:-up}" in
    up)
        check_owner
        "$ROOT/scripts/gen-certs.sh"
        compose config --quiet
        compose up -d --wait --wait-timeout 90
        compose ps
        local_listeners=$(docker exec emqx-quic emqx ctl listeners)
        printf '%s\n' "$local_listeners"
        if ! awk '/^quic:default$/{quic=1;next} /^[^ ]/{quic=0} quic && $1=="running" && $3=="true"{ready=1} END{exit !ready}' <<< "$local_listeners"; then
            echo 'QUIC listener is not running; inspect scripts/broker.sh logs.' >&2
            exit 1
        fi
        ;;
    config) compose config ;;
    ps) compose ps ;;
    listeners)
        check_owner
        docker exec emqx-quic emqx ctl listeners
        ;;
    logs)
        check_owner
        compose logs --tail 100 emqx
        ;;
    stop)
        check_owner
        compose stop
        ;;
    down)
        check_owner
        # Intentionally keep data/log volumes; never prune Docker resources.
        compose down
        ;;
    *) echo "Usage: $0 {up|config|ps|listeners|logs|stop|down}" >&2; exit 2 ;;
esac
