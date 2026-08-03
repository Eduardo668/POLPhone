#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
lab_dir="$repo_dir/lab/asterisk"
env_file="$lab_dir/.env"
example_file="$lab_dir/.env.example"
force=no
show_secrets=no

for argument in "$@"; do
    case "$argument" in
        --force) force=yes ;;
        --show-secrets) show_secrets=yes ;;
        *) echo "Uso: $0 [--force] [--show-secrets]" >&2; exit 2 ;;
    esac
done

command -v docker >/dev/null 2>&1 || { echo "Docker não encontrado." >&2; exit 1; }
docker version >/dev/null 2>&1 || { echo "Daemon Docker não acessível." >&2; exit 1; }
docker compose version >/dev/null 2>&1 || { echo "Docker Compose v2+ não encontrado." >&2; exit 1; }
command -v openssl >/dev/null 2>&1 || { echo "OpenSSL é necessário para gerar segredos." >&2; exit 1; }

if [ -f "$env_file" ] && [ "$force" != yes ]; then
    printf 'O .env já existe. Digite SOBRESCREVER para recriá-lo: '
    IFS= read -r confirmation
    if [ "$confirmation" != SOBRESCREVER ]; then
        echo "Configuração preservada; nenhuma alteração realizada."
        exit 0
    fi
fi

secret1001=$(openssl rand -hex 32)
secret1002=$(openssl rand -hex 32)
secret2001=$(openssl rand -hex 32)
sed \
    -e "s|LAB_SIP_1001_SECRET=GENERATE_WITH_LAB_INIT|LAB_SIP_1001_SECRET=$secret1001|" \
    -e "s|LAB_SIP_1002_SECRET=GENERATE_WITH_LAB_INIT|LAB_SIP_1002_SECRET=$secret1002|" \
    -e "s|LAB_PJSIP_2001_SECRET=GENERATE_WITH_LAB_INIT|LAB_PJSIP_2001_SECRET=$secret2001|" \
    "$example_file" > "$env_file"
chmod 0600 "$env_file"

git -C "$repo_dir" check-ignore -q lab/asterisk/.env || {
    echo "Proteção Git inválida: lab/asterisk/.env não está ignorado." >&2
    exit 1
}

echo "Laboratório inicializado sem iniciar containers."
echo "Ramais fictícios: 1001, 1002 e endpoint PJSIP opcional 2001."
echo "Segredos gravados somente em lab/asterisk/.env."
if [ "$show_secrets" = yes ]; then
    echo "AVISO: exibição local solicitada explicitamente; não registre estes valores."
    echo "1001: $secret1001"
    echo "1002: $secret1002"
    echo "2001: $secret2001"
fi
