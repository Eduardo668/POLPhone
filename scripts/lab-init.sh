#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

force=0
show_secrets=0
temp_file=''

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-init.sh [--force] [--show-secrets]

  --force         substitui um .env existente sem confirmação
  --show-secrets  exibe as novas credenciais somente no terminal local
EOF
}

while (($#)); do
    case "$1" in
        --force) force=1 ;;
        --show-secrets) show_secrets=1 ;;
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
    shift
done

lab_validate_docker
command -v git >/dev/null 2>&1 || lab_die "Git não foi encontrado; não é possível confirmar a proteção do .env."
[[ -f "${LAB_ENV_EXAMPLE}" ]] || lab_die "Template ausente: lab/asterisk/.env.example."

if [[ -f "${LAB_ENV_FILE}" && ${force} -ne 1 ]]; then
    lab_die "lab/asterisk/.env já existe. Use --force somente se deseja gerar novas credenciais."
fi

if ! git -C "${LAB_REPO_ROOT}" check-ignore --quiet lab/asterisk/.env; then
    lab_die "Proteção Git inválida: lab/asterisk/.env não está ignorado."
fi

generate_secret() {
    if command -v openssl >/dev/null 2>&1; then
        openssl rand -hex 32
        return
    fi
    [[ -r /dev/urandom ]] || lab_die "Nem openssl nem /dev/urandom estão disponíveis para gerar credenciais."
    command -v od >/dev/null 2>&1 && command -v tr >/dev/null 2>&1 || \
        lab_die "O fallback seguro requer od e tr."
    od -An -N32 -tx1 /dev/urandom | tr -d ' \n'
}

secret1001="$(generate_secret)"
secret1002="$(generate_secret)"
secret2001="$(generate_secret)"

umask 077
temp_file="$(mktemp "${LAB_DIR}/.env.tmp.XXXXXX")"
cleanup() {
    [[ -z "${temp_file}" || ! -e "${temp_file}" ]] || rm -f -- "${temp_file}"
}
trap cleanup EXIT

sed \
    -e "s|LAB_SIP_1001_SECRET=GENERATE_WITH_LAB_INIT|LAB_SIP_1001_SECRET=${secret1001}|" \
    -e "s|LAB_SIP_1002_SECRET=GENERATE_WITH_LAB_INIT|LAB_SIP_1002_SECRET=${secret1002}|" \
    -e "s|LAB_PJSIP_2001_SECRET=GENERATE_WITH_LAB_INIT|LAB_PJSIP_2001_SECRET=${secret2001}|" \
    "${LAB_ENV_EXAMPLE}" > "${temp_file}"
chmod 0600 "${temp_file}"
mv -f -- "${temp_file}" "${LAB_ENV_FILE}"
temp_file=''

lab_info "Laboratório inicializado sem iniciar containers."
lab_info "Ramais fictícios: 1001, 1002 e endpoint PJSIP opcional 2001."
lab_info "Credenciais gravadas somente em lab/asterisk/.env."
if (( show_secrets == 1 )); then
    lab_warn "Exibição local solicitada explicitamente; não copie estes valores para logs ou commits."
    printf '1001: %s\n1002: %s\n2001: %s\n' "${secret1001}" "${secret1002}" "${secret2001}"
fi
