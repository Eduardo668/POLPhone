#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

service=''
dtmf=0
tail_lines=100
follow=1

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-logs.sh [--asterisk|--relay] [--dtmf] [--tail N] [--no-follow]

  sem serviço   acompanha logs de todos os serviços
  --asterisk    mostra somente o Asterisk
  --relay       mostra somente o relay UDP
  --dtmf        filtra POLPHONE_LAB_DTMF (somente Asterisk)
  --tail N      quantidade inicial de linhas (padrão: 100)
  --no-follow   mostra o histórico e encerra
EOF
}

while (($#)); do
    case "$1" in
        --asterisk)
            [[ -z "${service}" || "${service}" == asterisk ]] || lab_usage_error "Escolha apenas um serviço."
            service=asterisk
            ;;
        --relay)
            [[ -z "${service}" || "${service}" == gateway ]] || lab_usage_error "Escolha apenas um serviço."
            service=gateway
            ;;
        --dtmf) dtmf=1 ;;
        --tail)
            (($# >= 2)) || lab_usage_error "--tail exige um valor."
            tail_lines=$2
            shift
            ;;
        --no-follow) follow=0 ;;
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
    shift
done

[[ "${tail_lines}" =~ ^[0-9]+$ ]] || lab_usage_error "Valor inválido para --tail: ${tail_lines}."
if (( dtmf == 1 )); then
    [[ "${service}" != gateway ]] || lab_usage_error "--dtmf não pode ser combinado com --relay."
    service=asterisk
fi

lab_validate_docker
env_file="$(lab_compose_env_or_example)"

if ! lab_container_exists "${LAB_ASTERISK_CONTAINER}" && ! lab_container_exists "${LAB_RELAY_CONTAINER}"; then
    lab_info "O laboratório está parado e não possui logs de containers atuais."
    exit 0
fi

arguments=(logs --no-color --tail "${tail_lines}")
(( follow == 0 )) || arguments+=(--follow)
[[ -z "${service}" ]] || arguments+=("${service}")

if (( dtmf == 0 )); then
    lab_compose_with_env "${env_file}" "${arguments[@]}"
    exit $?
fi

lab_compose_with_env "${env_file}" "${arguments[@]}" | \
    awk 'index($0, "POLPHONE_LAB_DTMF") { print; fflush() }'
