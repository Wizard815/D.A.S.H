#!/usr/bin/env bash
# Pretend to be Atlas so DASH can be tried without the truck.
#   tools/atlas-sim.sh mqtt <broker-host> [port]   publish to dash/v1/atlas/* (needs mosquitto_pub)
#   tools/atlas-sim.sh lines                       print UART lines, e.g. | tee /dev/ttyUSB0
# Runs a short drive: wakes, drives with live gauges, throws a code, parks
# and says bye. Ctrl-C to stop early.
set -euo pipefail

mode="${1:-}"
host="${2:-}"
port="${3:-1883}"
seq=0

usage() {
  sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
}

send() {  # $1 = type, $2 = fields without braces (no leading comma)
  seq=$((seq + 1))
  local json="{\"m\":\"atlas\",\"t\":\"$1\",\"s\":$seq${2:+,$2}}"
  if [ "${#json}" -gt 240 ]; then
    echo "frame over 240 bytes, not sent: $json" >&2
    return
  fi
  case "$mode" in
    mqtt)
      local topic="dash/v1/atlas/$1"
      [ "$1" = dtx ] && topic="$topic/$(sed -E 's/.*"c":"([^"]+)".*/\1/' <<<"$json")"
      mosquitto_pub -h "$host" -p "$port" -r -t "$topic" -m "$json"
      ;;
    lines) printf '%s\n' "$json" ;;
  esac
}

case "$mode" in
  mqtt)
    [ -n "$host" ] || usage
    command -v mosquitto_pub >/dev/null || { echo "mosquitto_pub not found: install mosquitto-clients" >&2; exit 1; }
    ;;
  lines) ;;
  *) usage ;;
esac

now() { date +%s; }

send hi "\"from\":\"atlas-sim\",\"up\":5,\"ign\":1,\"mode\":\"driving\",\"ts\":$(now)"
send sync "\"st\":\"waiting\",\"q\":37,\"last\":$(( $(now) - 3 * 3600 )),\"via\":\"home wifi\",\"err\":\"\""
send wx '"c":21.5,"rh":40,"hpa":1013.2,"txt":"Clear","hi":24,"lo":11'
send pwr '"v":12.9,"soc":80'
send dtc '"mil":0,"codes":[]'

ect=60
for i in $(seq 1 30); do
  [ "$ect" -lt 92 ] && ect=$((ect + 2))
  kph=$(( 60 + (i % 7) * 6 ))
  send obd "\"ect\":$ect,\"iat\":31,\"oil\":$((ect + 8)),\"atf\":$((ect - 5)),\"v\":14.1,\"rpm\":$((1500 + kph * 5)),\"kph\":$kph,\"fuel\":$((62 - i / 10)),\"l100\":13.$((i % 9)),\"l100a\":12.8,\"rng\":$((402 - i))"
  [ $((i % 3)) -eq 0 ] && send gps "\"lat\":44.97$((10 + i)),\"lon\":-93.26$((10 + i)),\"kph\":$kph,\"hdg\":270"
  [ $((i % 5)) -eq 0 ] && send hi "\"from\":\"atlas-sim\",\"up\":$((5 + i * 2)),\"ign\":1,\"mode\":\"driving\",\"ts\":$(now)"
  if [ "$i" -eq 15 ]; then
    send dtc '"mil":1,"codes":["P0420","P0171"]'
    send dtx '"c":"P0171","d":"Engine running lean (bank 1): check for a vacuum leak near the intake"'
  fi
  sleep 2
done

send obd '"ect":90,"v":12.6,"rpm":0,"kph":0,"fuel":60,"rng":372'
send sync "\"st\":\"synced\",\"q\":0,\"last\":$(now),\"via\":\"home wifi\",\"err\":\"\""
send bye '"why":"ign_off"'
echo "done: Atlas said bye" >&2
