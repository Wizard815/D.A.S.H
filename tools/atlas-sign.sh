#!/usr/bin/env bash
# Signs a remote start the way the DASH phone app does, for testing the
# bridge over Bluetooth (for example with nRF Connect) or truckd.
#   tools/atlas-sign.sh <64-hex start key> [seconds ahead, default 90]
# Prints the JSON to write to the bridge's command characteristic.
set -euo pipefail
key=${1:?give the 64-hex start key (BRIDGE_START_KEY)}
ahead=${2:-90}
nonce=$(head -c 12 /dev/urandom | od -An -tx1 | tr -d ' \n')
exp=$(( $(date +%s) + ahead ))
sig=$(printf 'remote_start|%s|%s' "$nonce" "$exp" | openssl dgst -sha256 -mac HMAC -macopt "hexkey:$key" -r | cut -d' ' -f1)
printf '{"c":"remote_start","nonce":"%s","exp":%s,"sig":"%s"}\n' "$nonce" "$exp" "$sig"
