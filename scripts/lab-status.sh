#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-status.sh

Mostra containers, healthchecks e o estado SIP/dialplan do laboratório.
EOF
}

if (($#)); then
    case "$1" in
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
fi

lab_validate_docker

if [[ ! -f "${LAB_ENV_FILE}" ]]; then
    lab_info "Laboratório não inicializado: lab/asterisk/.env está ausente."
    lab_info "Execute ./scripts/lab-init.sh."
    exit 0
fi

lab_require_env
lab_info "Containers:"
lab_compose ps --all
printf '\nHealth Asterisk: %s\n' "$(lab_container_health "${LAB_ASTERISK_CONTAINER}")"
printf 'Health relay:    %s\n' "$(lab_container_health "${LAB_RELAY_CONTAINER}")"

if ! lab_container_running "${LAB_ASTERISK_CONTAINER}"; then
    lab_info "O laboratório está parado. Execute ./scripts/lab-up.sh."
    exit 0
fi

show_cli() {
    local title=$1
    local command=$2
    printf '\n== %s ==\n' "${title}"
    if ! lab_asterisk_cli "${command}"; then
        lab_warn "Não foi possível executar: ${command}."
    fi
}

show_cli "Versão do Asterisk" "core show version"
show_cli "Estado do chan_sip" "module show like chan_sip.so"
show_cli "Estado do chan_pjsip" "module show like chan_pjsip.so"
show_cli "Peers chan_sip 1001 e 1002" "sip show peers"
show_cli "Endpoint PJSIP 2001" "pjsip show endpoints"
for extension in 600 9991 9992 9993 9999; do
    show_cli "Extensão ${extension}@from-lab" "dialplan show ${extension}@from-lab"
done
show_cli "Canais ativos" "core show channels"
