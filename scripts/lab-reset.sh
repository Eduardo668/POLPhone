#!/usr/bin/env bash
set -Eeuo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/lab-common.sh"

assume_yes=0
regenerate_env=0

usage() {
    cat <<'EOF'
Uso: ./scripts/lab-reset.sh [--yes] [--regenerate-env]

  --yes             confirma explicitamente o reset para automação
  --regenerate-env  recria o .env com novas credenciais após o reset
EOF
}

while (($#)); do
    case "$1" in
        --yes) assume_yes=1 ;;
        --regenerate-env) regenerate_env=1 ;;
        --help|-h) usage; exit 0 ;;
        *) lab_usage_error "Opção desconhecida: $1." ;;
    esac
    shift
done

lab_validate_docker
if (( assume_yes == 0 )); then
    if ! lab_confirm RESETAR-LAB "O reset removerá containers, volumes e dados transitórios do laboratório."; then
        lab_info "Reset cancelado; nenhuma alteração realizada."
        exit 0
    fi
fi

"${LAB_SCRIPTS_DIR}/lab-down.sh" --volumes
lab_clear_transient_data

if (( regenerate_env == 1 )); then
    "${LAB_SCRIPTS_DIR}/lab-init.sh" --force
    lab_info "Credenciais locais regeneradas sem exibição."
else
    lab_info "lab/asterisk/.env preservado."
fi

lab_info "Reset concluído; nenhum arquivo fora de lab/asterisk foi removido."
