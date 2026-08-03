#!/bin/sh
set -eu

case "${LAB_RTP_START:-}" in ''|*[!0-9]*) echo "LAB_RTP_START inválido" >&2; exit 1 ;; esac
case "${LAB_RTP_END:-}" in ''|*[!0-9]*) echo "LAB_RTP_END inválido" >&2; exit 1 ;; esac
if [ "$LAB_RTP_START" -gt "$LAB_RTP_END" ]; then
    echo "Faixa RTP inválida" >&2
    exit 1
fi

pids=""
stop_gateway() {
    trap - TERM INT
    for child_pid in $pids; do
        kill "$child_pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    exit 0
}
trap stop_gateway TERM INT

start_sip_relay() {
    listen_port=$1
    target_port=$2
    socat "UDP4-RECVFROM:${listen_port},reuseaddr,fork" "UDP4-SENDTO:asterisk:${target_port}" &
    pids="$pids $!"
}

start_rtp_relay() {
    listen_port=$1
    target_port=$2

    # UDP4-LISTEN mantém uma associação por origem remota. Assim, todos os
    # pacotes RTP da chamada chegam ao Asterisk pela mesma origem UDP e as
    # respostas retornam ao mesmo cliente. Isso é necessário para o Strict RTP.
    socat -T 60 "UDP4-LISTEN:${listen_port},reuseaddr,fork" "UDP4:asterisk:${target_port}" &
    pids="$pids $!"
}

start_sip_relay 5060 5060
start_sip_relay 5061 5061
relay_port=$LAB_RTP_START
while [ "$relay_port" -le "$LAB_RTP_END" ]; do
    start_rtp_relay "$relay_port" "$relay_port"
    relay_port=$((relay_port + 1))
done

echo "POLPhone Asterisk Lab: relay UDP localhost pronto para SIP e RTP."
wait
