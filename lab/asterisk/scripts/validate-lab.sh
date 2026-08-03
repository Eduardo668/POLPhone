#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
lab_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_dir=$(CDPATH= cd -- "$lab_dir/../.." && pwd)

fail() {
    echo "[FALHA] $*" >&2
    exit 1
}

check_file() {
    [ -f "$lab_dir/$1" ] || fail "arquivo obrigatório ausente: $1"
}

for required_file in \
    Dockerfile docker-compose.yml .env.example .gitignore README.md \
    config/acl.conf config/asterisk.conf config/cdr.conf config/cel.conf \
    config/extensions.conf config/features.conf config/indications.conf config/logger.conf \
    config/manager.conf config/modules.conf config/pjproject.conf config/stasis.conf config/udptl.conf \
    config/rtp.conf config/sip.conf.template config/pjsip.conf.template \
    scripts/entrypoint.sh scripts/gateway.sh scripts/generate-config.sh scripts/healthcheck.sh; do
    check_file "$required_file"
done

for operation_script in \
    lab-common.sh lab-init.sh lab-up.sh lab-status.sh lab-logs.sh lab-down.sh lab-reset.sh; do
    [ -f "$repo_dir/scripts/$operation_script" ] || fail "script Bash obrigatório ausente: scripts/$operation_script"
    grep -F 'set -Eeuo pipefail' "$repo_dir/scripts/$operation_script" >/dev/null || \
        fail "script Bash sem modo estrito: scripts/$operation_script"
done

grep -F 'BASH_SOURCE[0]' "$repo_dir/scripts/lab-common.sh" >/dev/null || fail "lab-common.sh não descobre a raiz pela própria localização"
grep -F 'docker compose' "$repo_dir/scripts/lab-common.sh" >/dev/null || fail "comando Compose não está centralizado"
grep -F -- '--env-file' "$repo_dir/scripts/lab-common.sh" >/dev/null || fail "scripts Bash não usam --env-file explícito"
grep -F 'ip -4 route get 1.1.1.1' "$repo_dir/scripts/lab-common.sh" >/dev/null || fail "detecção do IP atual do WSL ausente"
grep -F -- '--use-wsl-ip' "$repo_dir/scripts/lab-up.sh" >/dev/null || fail "opção explícita --use-wsl-ip ausente"
grep -F 'lab-up.sh --build' "$repo_dir/README.md" >/dev/null || fail "README principal não documenta Bash-first"

for wrapper in lab-init.ps1 lab-up.ps1 lab-status.ps1 lab-logs.ps1 lab-down.ps1 lab-reset.ps1; do
    grep -F 'Invoke-POLPhoneLabWsl' "$repo_dir/scripts/$wrapper" >/dev/null || \
        fail "wrapper PowerShell não encaminha ao WSL: scripts/$wrapper"
done

command -v docker >/dev/null 2>&1 || fail "Docker não encontrado"
docker compose version >/dev/null 2>&1 || fail "Docker Compose não encontrado"

compose_output=$(mktemp)
generated_dir=$(mktemp -d)
cleanup() {
    rm -f "$compose_output"
    rm -rf "$generated_dir"
}
trap cleanup EXIT HUP INT TERM

(
    cd "$lab_dir"
    docker compose --env-file .env.example config > "$compose_output"
)

grep -F 'host_ip: 127.0.0.1' "$compose_output" >/dev/null || fail "portas padrão não estão ligadas a localhost"
! grep -Eq 'privileged:[[:space:]]*true' "$compose_output" || fail "privileged não é permitido"
! grep -Eq 'network_mode:[[:space:]]*host' "$compose_output" || fail "network_mode host não é permitido"
grep -Eq 'internal:[[:space:]]*true' "$compose_output" || fail "rede de runtime precisa ser interna"
grep -Eq 'enable_ip_masquerade:.*false' "$compose_output" || fail "rede de publicação não pode mascarar egress"
grep -Eq 'no-new-privileges:true' "$compose_output" || fail "no-new-privileges ausente"
grep -Eq 'restart:.*no' "$compose_output" || fail "restart deve ser no"

grep -F 'ARG ASTERISK_TAG=20.19.0' "$lab_dir/Dockerfile" >/dev/null || fail "versão do Asterisk não está fixa"
grep -F 'ARG ASTERISK_COMMIT=f66917fe6da487155f80ab50403bf3d7bdc86183' "$lab_dir/Dockerfile" >/dev/null || fail "commit do Asterisk não está fixo"
grep -F -- '--enable chan_sip' "$lab_dir/Dockerfile" >/dev/null || fail "chan_sip não está habilitado no build"
grep -F 'load => chan_sip.so' "$lab_dir/config/modules.conf" >/dev/null || fail "chan_sip não está carregado"
grep -F 'autoload = no' "$lab_dir/config/modules.conf" >/dev/null || fail "módulos precisam usar allowlist"
grep -F 'load => res_pjsip_pubsub.so' "$lab_dir/config/modules.conf" >/dev/null || fail "dependência do chan_pjsip ausente"

for peer in 1001 1002; do
    grep -F "[$peer]" "$lab_dir/config/sip.conf.template" >/dev/null || fail "peer fictício $peer ausente"
done
for extension in 600 9991 9992 9993 9999; do
    grep -F "exten => $extension," "$lab_dir/config/extensions.conf" >/dev/null || fail "extensão $extension ausente"
done

grep -F 'SIPDtmfMode(rfc2833)' "$lab_dir/config/extensions.conf" >/dev/null || fail "teste RFC2833 ausente"
grep -F 'SIPDtmfMode(info)' "$lab_dir/config/extensions.conf" >/dev/null || fail "teste INFO ausente"
grep -F 'SIPDtmfMode(inband)' "$lab_dir/config/extensions.conf" >/dev/null || fail "teste in-band ausente"
grep -F 'POLPHONE_LAB_DTMF' "$lab_dir/config/extensions.conf" >/dev/null || fail "marcador de coleta ausente"

! grep -Eiq 'register[[:space:]]*=>|outbound_auth|server_uri|client_uri|_X[.!]' "$lab_dir/config"/* || fail "possível tronco ou rota externa detectada"
! grep -Eiq '(10\.[0-9]+\.[0-9]+\.[0-9]+|192\.168\.[0-9]+\.[0-9]+|172\.(1[6-9]|2[0-9]|3[01])\.[0-9]+\.[0-9]+)' "$lab_dir/config"/* "$lab_dir/.env.example" || fail "IP privado não-local detectado"

LAB_CONFIG_SOURCE_DIR="$lab_dir/config" \
LAB_ADVERTISED_IP=127.0.0.1 \
LAB_CHAN_SIP_HOST_PORT=15060 \
LAB_PJSIP_HOST_PORT=15061 \
LAB_RTP_START=10000 \
LAB_RTP_END=10100 \
LAB_SIP_1001_SECRET=StaticValidationSecret1001 \
LAB_SIP_1002_SECRET=StaticValidationSecret1002 \
LAB_PJSIP_2001_SECRET=StaticValidationSecret2001 \
    "$lab_dir/scripts/generate-config.sh" "$generated_dir"

! grep -RE '\$\{LAB_(ADVERTISED_IP|CHAN_SIP_HOST_PORT|PJSIP_HOST_PORT|RTP_START|RTP_END|SIP_1001_SECRET|SIP_1002_SECRET|PJSIP_2001_SECRET)\}' "$generated_dir" >/dev/null 2>&1 || fail "placeholders pendentes na configuração gerada"
grep -F 'secret=StaticValidationSecret1001' "$generated_dir/sip.conf" >/dev/null || fail "secret 1001 não foi renderizado"
grep -F 'password=StaticValidationSecret2001' "$generated_dir/pjsip.conf" >/dev/null || fail "secret 2001 não foi renderizado"

git -C "$repo_dir" check-ignore -q lab/asterisk/.env || fail "lab/asterisk/.env não está ignorado"
git -C "$repo_dir" check-ignore -q lab/asterisk/data/runtime.db || fail "dados transitórios não estão ignorados"
git -C "$repo_dir" check-ignore -q lab/asterisk/captures/test.pcap || fail "capturas não estão ignoradas"
git -C "$repo_dir" check-ignore -q config/polphone.config.lab.json || fail "configuração local do POLPhone não está ignorada"

grep -F 'module show like chan_sip.so' "$lab_dir/scripts/healthcheck.sh" >/dev/null || fail "healthcheck não valida chan_sip"
grep -F 'dialplan show 9999@from-lab' "$lab_dir/scripts/healthcheck.sh" >/dev/null || fail "healthcheck não valida dialplan"
grep -F 'sip show settings' "$lab_dir/scripts/healthcheck.sh" >/dev/null || fail "healthcheck não valida UDP"
grep -F 'gateway.sh /proc/1/cmdline' "$lab_dir/docker-compose.yml" >/dev/null || fail "healthcheck do relay ausente"
grep -F 'start_sip_relay 5060 5060' "$lab_dir/scripts/gateway.sh" >/dev/null || fail "relay SIP chan_sip ausente"
grep -F 'start_sip_relay 5061 5061' "$lab_dir/scripts/gateway.sh" >/dev/null || fail "relay SIP PJSIP ausente"
grep -F 'UDP4-LISTEN:${listen_port},reuseaddr,fork' "$lab_dir/scripts/gateway.sh" >/dev/null || fail "relay RTP não mantém associação UDP por cliente"
grep -F 'UDP4:asterisk:${target_port}' "$lab_dir/scripts/gateway.sh" >/dev/null || fail "relay RTP não usa destino UDP conectado"
grep -F 'start_rtp_relay "$relay_port" "$relay_port"' "$lab_dir/scripts/gateway.sh" >/dev/null || fail "faixa RTP não usa relay com origem estável"

echo "[OK] validações estáticas e docker compose config concluídas"
