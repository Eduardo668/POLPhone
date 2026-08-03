#!/bin/sh
set -eu

source_dir=${LAB_CONFIG_SOURCE_DIR:-/opt/polphone-lab/config}
output_dir=${1:-/etc/asterisk}

required_vars="LAB_ADVERTISED_IP LAB_CHAN_SIP_HOST_PORT LAB_PJSIP_HOST_PORT LAB_RTP_START LAB_RTP_END LAB_SIP_1001_SECRET LAB_SIP_1002_SECRET LAB_PJSIP_2001_SECRET"
for var_name in $required_vars; do
    eval "var_value=\${$var_name:-}"
    if [ -z "$var_value" ]; then
        echo "Configuração ausente: $var_name" >&2
        exit 1
    fi
done

case "$LAB_ADVERTISED_IP" in
    0.0.0.0|*/*|*[!0-9.]*)
        echo "LAB_ADVERTISED_IP deve ser um IPv4 explícito e não pode ser 0.0.0.0." >&2
        exit 1
        ;;
esac

for port_value in "$LAB_CHAN_SIP_HOST_PORT" "$LAB_PJSIP_HOST_PORT" "$LAB_RTP_START" "$LAB_RTP_END"; do
    case "$port_value" in
        ''|*[!0-9]*) echo "Porta inválida na configuração do laboratório." >&2; exit 1 ;;
    esac
    if [ "$port_value" -lt 1024 ] || [ "$port_value" -gt 65535 ]; then
        echo "Porta fora da faixa 1024-65535." >&2
        exit 1
    fi
done
if [ "$LAB_RTP_START" -gt "$LAB_RTP_END" ]; then
    echo "LAB_RTP_START deve ser menor ou igual a LAB_RTP_END." >&2
    exit 1
fi

for secret_value in "$LAB_SIP_1001_SECRET" "$LAB_SIP_1002_SECRET" "$LAB_PJSIP_2001_SECRET"; do
    case "$secret_value" in
        GENERATE_WITH_LAB_INIT|*[!A-Za-z0-9_-]*|'')
            echo "Segredo local inválido ou ainda não gerado." >&2
            exit 1
            ;;
    esac
    if [ "${#secret_value}" -lt 24 ]; then
        echo "Cada segredo local deve possuir ao menos 24 caracteres." >&2
        exit 1
    fi
done

mkdir -p "$output_dir"
for config_name in \
    acl.conf asterisk.conf ccss.conf cdr.conf cel.conf extensions.conf features.conf http.conf \
    indications.conf logger.conf manager.conf modules.conf pjproject.conf stasis.conf udptl.conf; do
    cp "$source_dir/$config_name" "$output_dir/$config_name"
done

render_template() {
    input_file=$1
    output_file=$2
    sed \
        -e "s|\${LAB_ADVERTISED_IP}|$LAB_ADVERTISED_IP|g" \
        -e "s|\${LAB_CHAN_SIP_HOST_PORT}|$LAB_CHAN_SIP_HOST_PORT|g" \
        -e "s|\${LAB_PJSIP_HOST_PORT}|$LAB_PJSIP_HOST_PORT|g" \
        -e "s|\${LAB_RTP_START}|$LAB_RTP_START|g" \
        -e "s|\${LAB_RTP_END}|$LAB_RTP_END|g" \
        -e "s|\${LAB_SIP_1001_SECRET}|$LAB_SIP_1001_SECRET|g" \
        -e "s|\${LAB_SIP_1002_SECRET}|$LAB_SIP_1002_SECRET|g" \
        -e "s|\${LAB_PJSIP_2001_SECRET}|$LAB_PJSIP_2001_SECRET|g" \
        "$input_file" > "$output_file"
}

render_template "$source_dir/sip.conf.template" "$output_dir/sip.conf"
render_template "$source_dir/pjsip.conf.template" "$output_dir/pjsip.conf"
render_template "$source_dir/rtp.conf" "$output_dir/rtp.conf"

if grep -RE '\$\{LAB_(ADVERTISED_IP|CHAN_SIP_HOST_PORT|PJSIP_HOST_PORT|RTP_START|RTP_END|SIP_1001_SECRET|SIP_1002_SECRET|PJSIP_2001_SECRET)\}' "$output_dir" >/dev/null 2>&1; then
    echo "A configuração gerada ainda contém placeholders." >&2
    exit 1
fi

chmod 0640 "$output_dir"/*.conf
