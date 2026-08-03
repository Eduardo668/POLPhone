#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

remove_volumes=0
remove_images=0

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-down.sh [--volumes] [--remove-images]

  --volumes       remove também volumes do projeto
  --remove-images remove também imagens locais construídas pelo Compose
EOF
}

while (($#)); do
    case "$1" in
        --volumes) remove_volumes=1 ;;
        --remove-images) remove_images=1 ;;
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
    shift
done

lab_validate_docker
env_file="$(lab_compose_env_or_example)"
arguments=(down --remove-orphans)
(( remove_volumes == 0 )) || arguments+=(--volumes)
(( remove_images == 0 )) || arguments+=(--rmi local)

lab_info "Encerrando o laboratório."
lab_compose_with_env "${env_file}" "${arguments[@]}"
lab_info "Laboratório parado."
