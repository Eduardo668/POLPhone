#!/bin/sh
set -eu

/usr/local/bin/generate-config.sh /etc/asterisk

echo "POLPhone Asterisk Lab: configuração local gerada; iniciando Asterisk 20.19.0 sem troncos."
echo "POLPhone Asterisk Lab: runtime isolado; não use este ambiente em produção."

# exec torna o Asterisk o PID 1; SIGTERM chega diretamente ao processo, que executa seu shutdown.
exec /usr/sbin/asterisk -f -vvv -C /etc/asterisk/asterisk.conf
