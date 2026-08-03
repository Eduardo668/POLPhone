#!/bin/sh
set -eu

asterisk_cmd() {
    /usr/sbin/asterisk -C /etc/asterisk/asterisk.conf -rx "$1" 2>/dev/null
}

asterisk_cmd "core show version" | grep -F "Asterisk 20.19.0" >/dev/null
asterisk_cmd "module show like chan_sip.so" | grep -E 'chan_sip\.so.*Running' >/dev/null
asterisk_cmd "dialplan show 9999@from-lab" | grep -F "'9999'" >/dev/null
asterisk_cmd "sip show settings" | grep -E 'UDP Bindaddress:.*5060' >/dev/null
