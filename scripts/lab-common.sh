#!/usr/bin/env bash
set -Eeuo pipefail

LAB_SCRIPTS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
LAB_REPO_ROOT="$(cd -- "${LAB_SCRIPTS_DIR}/.." && pwd -P)"
LAB_DIR="${LAB_REPO_ROOT}/lab/asterisk"
LAB_ENV_FILE="${LAB_DIR}/.env"
LAB_ENV_EXAMPLE="${LAB_DIR}/.env.example"
LAB_COMPOSE_FILE="${LAB_DIR}/docker-compose.yml"
LAB_ASTERISK_CONTAINER="polphone-asterisk-lab"
LAB_RELAY_CONTAINER="polphone-asterisk-lab-gateway"
LAB_ERROR_REPORTED=0

readonly LAB_SCRIPTS_DIR LAB_REPO_ROOT LAB_DIR LAB_ENV_FILE LAB_ENV_EXAMPLE
readonly LAB_COMPOSE_FILE LAB_ASTERISK_CONTAINER LAB_RELAY_CONTAINER

cd -- "${LAB_REPO_ROOT}"

lab_info() {
    printf '[LAB] %s\n' "$*"
}

lab_warn() {
    printf '[LAB][AVISO] %s\n' "$*" >&2
}

lab_error() {
    printf '[LAB][ERRO] %s\n' "$*" >&2
}

lab_die() {
    local message=$1
    local status=${2:-1}
    LAB_ERROR_REPORTED=1
    lab_error "${message}"
    exit "${status}"
}

lab_on_error() {
    local status=$1
    local line=$2
    if (( LAB_ERROR_REPORTED == 0 )); then
        lab_error "Comando falhou com código ${status} na linha ${line}."
    fi
    exit "${status}"
}

lab_on_interrupt() {
    lab_warn "Operação interrompida pelo usuário."
    exit 130
}

lab_on_term() {
    lab_warn "Operação encerrada por sinal."
    exit 143
}

trap 'lab_on_error "$?" "$LINENO"' ERR
trap lab_on_interrupt INT
trap lab_on_term TERM

lab_usage_error() {
    lab_die "$1 Use --help para consultar as opções." 2
}

lab_validate_docker() {
    command -v docker >/dev/null 2>&1 || \
        lab_die "Docker não foi encontrado no PATH do WSL. Instale ou habilite a integração do Docker sem usar sudo automaticamente."

    if ! docker compose version >/dev/null 2>&1; then
        lab_die "Docker Compose v2 não está disponível. Verifique 'docker compose version'."
    fi

    if ! docker info --format '{{.ServerVersion}}' >/dev/null 2>&1; then
        lab_die "O daemon Docker não está acessível pelo WSL. Inicie o Docker e verifique a integração desta distribuição."
    fi
}

lab_compose_with_env() {
    local env_file=$1
    shift
    docker compose \
        --project-directory "${LAB_DIR}" \
        --env-file "${env_file}" \
        -f "${LAB_COMPOSE_FILE}" \
        "$@"
}

lab_compose() {
    lab_compose_with_env "${LAB_ENV_FILE}" "$@"
}

lab_compose_env_or_example() {
    if [[ -f "${LAB_ENV_FILE}" ]]; then
        printf '%s\n' "${LAB_ENV_FILE}"
    else
        printf '%s\n' "${LAB_ENV_EXAMPLE}"
    fi
}

lab_env_value() {
    local key=$1
    local file=${2:-${LAB_ENV_FILE}}
    awk -v expected="${key}" '
        index($0, expected "=") == 1 {
            value = substr($0, length(expected) + 2)
            sub(/\r$/, "", value)
            print value
            exit
        }
    ' "${file}"
}

lab_validate_ipv4() {
    local address=$1
    local part
    local -a parts

    [[ "${address}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1
    IFS=. read -r -a parts <<< "${address}"
    (( ${#parts[@]} == 4 )) || return 1
    for part in "${parts[@]}"; do
        [[ "${part}" =~ ^[0-9]{1,3}$ ]] || return 1
        (( 10#${part} <= 255 )) || return 1
    done
}

lab_validate_port() {
    local value=$1
    [[ "${value}" =~ ^[0-9]+$ ]] && (( value >= 1024 && value <= 65535 ))
}

lab_is_wsl() {
    [[ -n "${WSL_DISTRO_NAME:-}" ]] || grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null
}

lab_detect_wsl_ipv4() {
    local route interface address

    lab_is_wsl || return 1
    command -v ip >/dev/null 2>&1 || return 1
    route="$(ip -4 route get 1.1.1.1 2>/dev/null)" || return 1
    interface="$(awk '{ for (i = 1; i <= NF; i++) if ($i == "dev") { print $(i + 1); exit } }' <<< "${route}")"
    address="$(awk '{ for (i = 1; i <= NF; i++) if ($i == "src") { print $(i + 1); exit } }' <<< "${route}")"

    case "${interface}" in
        ''|lo|docker0|br-*|veth*) return 1 ;;
    esac
    lab_validate_ipv4 "${address}" || return 1
    [[ "${address}" != 0.0.0.0 && "${address}" != 127.* ]] || return 1
    printf '%s\n' "${address}"
}

lab_assert_wsl_ip_current() {
    local current_ip
    lab_is_wsl || return 0
    current_ip="$(lab_detect_wsl_ipv4)" || \
        lab_die "Não foi possível detectar o IPv4 de origem da rota padrão do WSL."

    if [[ "${LAB_BIND_IP_VALUE}" != "${current_ip}" || "${LAB_ADVERTISED_IP_VALUE}" != "${current_ip}" ]]; then
        lab_die "O IP atual do WSL é ${current_ip}, mas LAB_BIND_IP/LAB_ADVERTISED_IP estão desatualizados. Revise e execute ./scripts/lab-up.sh --use-wsl-ip."
    fi
}

lab_update_local_files_with_wsl_ip() {
    local current_ip env_temp config_temp

    lab_is_wsl || lab_die "--use-wsl-ip só pode ser utilizado dentro do WSL."
    current_ip="$(lab_detect_wsl_ipv4)" || \
        lab_die "Não foi possível detectar o IPv4 de origem da rota padrão do WSL."
    [[ -f "${LAB_ENV_FILE}" ]] || lab_die "lab/asterisk/.env está ausente; execute ./scripts/lab-init.sh."
    [[ -f "${LAB_REPO_ROOT}/config/polphone.config.lab.json" ]] || \
        lab_die "config/polphone.config.lab.json está ausente; crie a configuração local a partir do exemplo antes de usar --use-wsl-ip."

    command -v git >/dev/null 2>&1 || lab_die "Git não foi encontrado para confirmar os arquivos ignorados."
    git -C "${LAB_REPO_ROOT}" check-ignore --quiet lab/asterisk/.env || \
        lab_die "lab/asterisk/.env não está ignorado pelo Git."
    git -C "${LAB_REPO_ROOT}" check-ignore --quiet config/polphone.config.lab.json || \
        lab_die "config/polphone.config.lab.json não está ignorado pelo Git."

    env_temp="${LAB_DIR}/.env.update.$$"
    config_temp="${LAB_DIR}/data/polphone.config.lab.update.$$"
    mkdir -p "${LAB_DIR}/data"
    umask 077

    if ! awk -v address="${current_ip}" '
        /^LAB_BIND_IP=/ { print "LAB_BIND_IP=" address; next }
        /^LAB_ADVERTISED_IP=/ { print "LAB_ADVERTISED_IP=" address; next }
        { print }
    ' "${LAB_ENV_FILE}" > "${env_temp}"; then
        rm -f -- "${env_temp}" "${config_temp}"
        lab_die "Falha ao preparar a atualização local do .env."
    fi

    if ! sed -E \
        -e "s#(\"idUri\":[[:space:]]*\")sip:1001@[^\"]+#\1sip:1001@${current_ip}#" \
        -e "s#(\"registrarUri\":[[:space:]]*\")sip:[^\"]+:15060#\1sip:${current_ip}:15060#" \
        -e "s#(\"domain\":[[:space:]]*\")[^\"]+#\1${current_ip}#" \
        "${LAB_REPO_ROOT}/config/polphone.config.lab.json" > "${config_temp}"; then
        rm -f -- "${env_temp}" "${config_temp}"
        lab_die "Falha ao preparar a atualização local da configuração do POLPhone."
    fi

    chmod 0600 "${env_temp}" "${config_temp}"
    mv -f -- "${env_temp}" "${LAB_ENV_FILE}"
    mv -f -- "${config_temp}" "${LAB_REPO_ROOT}/config/polphone.config.lab.json"

    LAB_BIND_IP_VALUE="${current_ip}"
    LAB_ADVERTISED_IP_VALUE="${current_ip}"
    lab_info "Arquivos locais ignorados atualizados explicitamente para o IP atual do WSL: ${current_ip}."
}

lab_require_env() {
    local name value
    local -a required=(
        LAB_BIND_IP LAB_ADVERTISED_IP LAB_CHAN_SIP_HOST_PORT LAB_PJSIP_HOST_PORT
        LAB_RTP_START LAB_RTP_END LAB_SIP_1001_SECRET LAB_SIP_1002_SECRET
        LAB_PJSIP_2001_SECRET
    )

    [[ -f "${LAB_ENV_FILE}" ]] || \
        lab_die "Configuração ausente: lab/asterisk/.env. Execute ./scripts/lab-init.sh."

    for name in "${required[@]}"; do
        value="$(lab_env_value "${name}")"
        [[ -n "${value}" ]] || lab_die "Variável obrigatória ausente no .env: ${name}."
        [[ "${value}" != GENERATE_WITH_LAB_INIT ]] || \
            lab_die "O .env ainda contém placeholders. Execute ./scripts/lab-init.sh --force."
    done

    LAB_BIND_IP_VALUE="$(lab_env_value LAB_BIND_IP)"
    LAB_ADVERTISED_IP_VALUE="$(lab_env_value LAB_ADVERTISED_IP)"
    LAB_CHAN_SIP_PORT_VALUE="$(lab_env_value LAB_CHAN_SIP_HOST_PORT)"
    LAB_PJSIP_PORT_VALUE="$(lab_env_value LAB_PJSIP_HOST_PORT)"
    LAB_RTP_START_VALUE="$(lab_env_value LAB_RTP_START)"
    LAB_RTP_END_VALUE="$(lab_env_value LAB_RTP_END)"

    lab_validate_ipv4 "${LAB_BIND_IP_VALUE}" || \
        lab_die "LAB_BIND_IP deve conter um IPv4 explícito."
    lab_validate_ipv4 "${LAB_ADVERTISED_IP_VALUE}" || \
        lab_die "LAB_ADVERTISED_IP deve conter um IPv4 explícito."
    [[ "${LAB_BIND_IP_VALUE}" != 0.0.0.0 && "${LAB_ADVERTISED_IP_VALUE}" != 0.0.0.0 ]] || \
        lab_die "0.0.0.0 não é permitido. Use localhost ou um IP local explícito e seguro."

    for value in "${LAB_CHAN_SIP_PORT_VALUE}" "${LAB_PJSIP_PORT_VALUE}" \
        "${LAB_RTP_START_VALUE}" "${LAB_RTP_END_VALUE}"; do
        lab_validate_port "${value}" || lab_die "Porta inválida no .env: ${value}."
    done
    (( LAB_RTP_START_VALUE <= LAB_RTP_END_VALUE )) || \
        lab_die "LAB_RTP_START deve ser menor ou igual a LAB_RTP_END."

    for name in LAB_SIP_1001_SECRET LAB_SIP_1002_SECRET LAB_PJSIP_2001_SECRET; do
        value="$(lab_env_value "${name}")"
        [[ "${value}" =~ ^[A-Za-z0-9_-]{24,}$ ]] || \
            lab_die "O segredo local ${name} é inválido; regenere o .env."
    done
}

lab_confirm() {
    local expected=$1
    local prompt=$2
    local response

    if [[ ! -t 0 ]]; then
        lab_error "Confirmação interativa indisponível. Use a opção explícita de automação quando aplicável."
        return 1
    fi
    printf '%s Digite %s: ' "${prompt}" "${expected}" >&2
    IFS= read -r response
    [[ "${response}" == "${expected}" ]]
}

lab_container_exists() {
    docker inspect "$1" >/dev/null 2>&1
}

lab_container_running() {
    [[ "$(docker inspect --format '{{.State.Running}}' "$1" 2>/dev/null || true)" == true ]]
}

lab_container_health() {
    local name=$1
    if ! lab_container_exists "${name}"; then
        printf 'ausente\n'
        return 0
    fi
    docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "${name}" 2>/dev/null
}

lab_wait_for_health() {
    local timeout=$1
    local deadline=$((SECONDS + timeout))
    local asterisk_health relay_health

    while (( SECONDS < deadline )); do
        asterisk_health="$(lab_container_health "${LAB_ASTERISK_CONTAINER}")"
        relay_health="$(lab_container_health "${LAB_RELAY_CONTAINER}")"
        if [[ "${asterisk_health}" == healthy && "${relay_health}" == healthy ]]; then
            return 0
        fi
        if [[ "${asterisk_health}" == unhealthy || "${relay_health}" == unhealthy ]]; then
            return 1
        fi
        sleep 2
    done
    return 1
}

lab_show_diagnostics() {
    local env_file
    env_file="$(lab_compose_env_or_example)"
    lab_warn "Estado dos serviços:"
    lab_compose_with_env "${env_file}" ps --all || true
    lab_warn "Últimas 120 linhas dos logs:"
    lab_compose_with_env "${env_file}" logs --no-color --tail 120 asterisk gateway || true
}

lab_asterisk_cli() {
    local command=$1
    lab_compose exec -T asterisk asterisk -rx "${command}"
}

lab_clear_transient_data() {
    local directory
    for directory in "${LAB_DIR}/data" "${LAB_DIR}/logs" "${LAB_DIR}/captures"; do
        [[ -d "${directory}" ]] || continue
        find "${directory}" -mindepth 1 -maxdepth 1 ! -name .gitkeep -exec rm -rf -- {} +
    done
}
