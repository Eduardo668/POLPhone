#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

build_mode=auto
timeout=180
allow_lan_exposure=0
use_wsl_ip=0

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-up.sh [--build|--no-build] [--timeout SEGUNDOS] [--use-wsl-ip]

  --build               força a reconstrução da imagem
  --no-build            não permite construir a imagem ausente
  --timeout SEGUNDOS    limite para os healthchecks (padrão: 180)
  --use-wsl-ip          atualiza explicitamente os dois arquivos locais ignorados com o IP atual do WSL
EOF
}

while (($#)); do
    case "$1" in
        --build) build_mode=build ;;
        --no-build) build_mode=no-build ;;
        --timeout)
            (($# >= 2)) || lab_usage_error "--timeout exige um valor."
            timeout=$2
            shift
            ;;
        --use-wsl-ip)
            use_wsl_ip=1
            allow_lan_exposure=1
            ;;
        --allow-lan-exposure) allow_lan_exposure=1 ;;
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
    shift
done

[[ "${timeout}" =~ ^[1-9][0-9]*$ ]] || lab_usage_error "Timeout inválido: ${timeout}."

lab_validate_docker
lab_require_env

if (( use_wsl_ip == 1 )); then
    lab_update_local_files_with_wsl_ip
    lab_require_env
else
    lab_assert_wsl_ip_current
fi

if [[ "${LAB_BIND_IP_VALUE}" != 127.0.0.1 ]]; then
    lab_warn "LAB_BIND_IP=${LAB_BIND_IP_VALUE} expõe portas em um IP local."
    lab_warn "Restrinja o firewall e nunca use uma interface de VPN/VLAN corporativa."
    (( allow_lan_exposure == 1 )) || \
        lab_die "Bind fora de localhost bloqueado. Use --use-wsl-ip para o IP virtual atual ou --allow-lan-exposure somente após revisão explícita."
fi

lab_info "Validando Docker Compose."
lab_compose config --quiet

up_arguments=(up -d)
case "${build_mode}" in
    build) up_arguments+=(--build) ;;
    no-build) up_arguments+=(--no-build) ;;
esac

lab_info "Subindo o laboratório (build: ${build_mode})."
if ! lab_compose "${up_arguments[@]}"; then
    lab_show_diagnostics
    lab_die "Falha ao subir os serviços do laboratório."
fi

lab_info "Aguardando healthchecks por até ${timeout} segundos."
if ! lab_wait_for_health "${timeout}"; then
    lab_show_diagnostics
    lab_die "O laboratório não ficou saudável dentro do timeout."
fi

lab_info "Laboratório saudável. Dados sanitizados:"
printf '  Host SIP anunciado: %s\n' "${LAB_ADVERTISED_IP_VALUE}"
printf '  chan_sip UDP:        %s:%s\n' "${LAB_BIND_IP_VALUE}" "${LAB_CHAN_SIP_PORT_VALUE}"
printf '  PJSIP UDP:           %s:%s\n' "${LAB_BIND_IP_VALUE}" "${LAB_PJSIP_PORT_VALUE}"
printf '  RTP UDP:             %s:%s-%s\n' "${LAB_BIND_IP_VALUE}" "${LAB_RTP_START_VALUE}" "${LAB_RTP_END_VALUE}"
printf '  Ramal principal:     1001\n'
printf '  Próximos comandos:   ./scripts/lab-status.sh\n'
printf '                       ./scripts/lab-logs.sh --dtmf\n'
printf '  Senha: consulte localmente lab/asterisk/.env (não exibida).\n'
