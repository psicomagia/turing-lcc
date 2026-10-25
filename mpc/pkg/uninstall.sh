#!/bin/sh
# Turing LCC - remove o plugin da MPC (rodar NA MPC, como root):  sh uninstall.sh [-y] [-t <pasta-Synths>]
set -e
cd "$(dirname "$0")"
PASTA='Alex Sheeny - VST - Turing LCC'; SO='turing-lcc.so'; UIDHEX='546d4c63'
SYNTHS=/sdcard/Synths; YES=0
die() { echo "erro: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in -y) YES=1; shift ;; -t) SYNTHS="$2"; shift 2 ;; *) die "uso: sh uninstall.sh [-y] [-t <pasta-Synths>]" ;; esac
done
if [ -z "$MPC_TESTE" ]; then [ "$(id -u)" = 0 ] || die "rode como root"; fi
SETTINGS="${MPC_SETTINGS:-$(ls /media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)}"
[ -n "$SETTINGS" ] && [ -f "$SETTINGS" ] || die "MPC.settings não encontrado"
if [ $YES = 0 ]; then
    printf "Remover o Turing LCC? Projetos que usam o plugin vão abrir sem ele. Continuar? [s/N] "
    read -r ok; case "$ok" in s|S|sim|y|Y) ;; *) echo "cancelado"; exit 1 ;; esac
fi
if systemctl cat inmusic-mpc >/dev/null 2>&1; then SVC=inmusic-mpc; else SVC=acvs; fi
systemctl stop "$SVC"
trap 'systemctl start "$SVC"' EXIT
i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
BAK="$SETTINGS.bak-turing-lcc-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
awk -v mode=remove -v file="$SYNTHS/$PASTA/$SO" -v uid="$UIDHEX" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.novo"
grep -q '</PROPERTIES>' "$SETTINGS.novo" || { rm -f "$SETTINGS.novo"; die "MPC.settings ficaria inválido; ele ficou como estava"; }
mv "$SETTINGS.novo" "$SETTINGS"
rm -rf "$SYNTHS/$PASTA"
sync
echo "Turing LCC removido. Backup das configurações: $BAK"
