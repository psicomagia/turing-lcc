#!/bin/sh
# Turing LCC - instalador para a MPC (rodar NA MPC, como root):
#   sh install.sh [-y] [-t <pasta-Synths>]
# Copia a pasta do plugin para /sdcard/Synths (ou a pasta de -t), faz backup do MPC.settings,
# registra o plugin na lista de plugins e reinicia o programa da MPC. Pode rodar de novo para atualizar.
set -e
cd "$(dirname "$0")"
PASTA='Alex Sheeny - VST - Turing LCC'; SO='turing-lcc.so'; UIDHEX='546d4c63'
SYNTHS=/sdcard/Synths; YES=0
die() { echo "erro: $*" >&2; exit 1; }
while [ $# -gt 0 ]; do
    case "$1" in
        -y) YES=1; shift ;;
        -t) [ -n "$2" ] || die "-t precisa de uma pasta"; SYNTHS="$2"; shift 2 ;;
        *) die "uso: sh install.sh [-y] [-t <pasta-Synths>]" ;;
    esac
done
case "$SYNTHS" in /*) ;; *) die "-t precisa de um caminho absoluto" ;; esac
case "$SYNTHS" in *"&"*|*"|"*|*"\\"*) die "o caminho não pode ter & | ou barra invertida" ;; esac
if [ -z "$MPC_TESTE" ]; then [ "$(id -u)" = 0 ] || die "rode como root"; fi
[ -n "$MPC_TESTE" ] || case "$(uname -m)" in armv7*) ;; *) die "este plugin é para MPC Gen1 (ARM 32 bits); esta é $(uname -m)" ;; esac
command -v systemctl >/dev/null || die "systemctl não encontrado"
SETTINGS="${MPC_SETTINGS:-$(ls /media/az01-internal/Settings/*/MPC.settings 2>/dev/null | head -n 1)}"
[ -n "$SETTINGS" ] && [ -f "$SETTINGS" ] || die "MPC.settings não encontrado"
[ -f "$PASTA/$SO" ] || die "pacote incompleto: falta $PASTA/$SO"
if command -v sha256sum >/dev/null; then sha256sum -c SHA256SUMS >/dev/null 2>&1 || die "arquivos corrompidos (SHA256SUMS): copie a pasta de novo"; fi
mkdir -p "$SYNTHS" || die "não criei $SYNTHS"
MP=$(df -kP "$SYNTHS" 2>/dev/null | awk 'NR==2 {m = $6; for (i = 7; i <= NF; i++) m = m " " $i; print m}' | sed 's/ /\\040/g')
if [ -n "$MP" ] && MP="$MP" awk '$2 == ENVIRON["MP"] {print $4}' /proc/mounts 2>/dev/null | grep -q '\(^\|,\)noexec\(,\|$\)'; then
    die "$SYNTHS está num disco sem permissão de executar: a MPC não carregaria o plugin. Use -t com outra pasta."
fi

echo "Instalando Turing LCC em: $SYNTHS/$PASTA"
if [ $YES = 0 ]; then
    printf "O programa da MPC vai fechar e abrir de novo. Salve o projeto antes. Continuar? [s/N] "
    read -r ok; case "$ok" in s|S|sim|y|Y) ;; *) echo "cancelado"; exit 1 ;; esac
fi

servico() {   # acvs no firmware da Akai, inmusic-mpc no Hakai
    if systemctl cat inmusic-mpc >/dev/null 2>&1; then echo inmusic-mpc
    elif systemctl cat acvs >/dev/null 2>&1; then echo acvs
    else echo acvs; fi
}
SVC=$(servico)
systemctl stop "$SVC"
trap 'systemctl start "$SVC"' EXIT
i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
pidof MPC >/dev/null && die "a MPC não fechou"

# 1. a pasta (preparada ao lado e trocada de uma vez)
NOVA="$SYNTHS/.$PASTA.nova"; VELHA="$SYNTHS/.$PASTA.velha"; DEST="$SYNTHS/$PASTA"
rm -rf "$NOVA" "$VELHA"
cp -a "$PASTA" "$NOVA"
[ -e "$DEST" ] && mv "$DEST" "$VELHA"
mv "$NOVA" "$DEST"
rm -rf "$VELHA"
chmod 755 "$DEST/$SO"

# 2. a entrada na lista de plugins (troca uma instalação anterior, sem duplicar)
FILE="$SYNTHS/$PASTA/$SO"
BAK="$SETTINGS.bak-turing-lcc-$(date +%Y%m%d-%H%M%S)"
cp "$SETTINGS" "$BAK"
SYN_SED=$(printf '%s' "$SYNTHS" | sed 's/[|&\\]/\\&/g')
sed "s|%payload-path%|$SYN_SED|g" "$PASTA/plugin-meta.xml" > "$SETTINGS.entrada"
awk -v mode=add -v file="$FILE" -v uid="$UIDHEX" -v entryfile="$SETTINGS.entrada" -f plugin_list.awk "$SETTINGS" > "$SETTINGS.novo"
rm -f "$SETTINGS.entrada"
n=$(grep -c "file=\"$FILE\"" "$SETTINGS.novo" || true)
u=$(grep -c " uid=\"$UIDHEX\"" "$SETTINGS.novo" || true)
[ "$n" = 1 ] && [ "$u" = 1 ] || { rm -f "$SETTINGS.novo"; die "a edição do MPC.settings falhou; ele ficou como estava"; }
grep -q '<PROPERTIES' "$SETTINGS.novo" && grep -q '</PROPERTIES>' "$SETTINGS.novo" || { rm -f "$SETTINGS.novo"; die "MPC.settings ficaria inválido; ele ficou como estava"; }
mv "$SETTINGS.novo" "$SETTINGS"
sync
echo "Pronto. Backup das configurações: $BAK"
echo "A MPC vai abrir de novo. Crie uma trilha de plugin e escolha 'Turing LCC'."
echo "A página fica em http://<ip-da-mpc>:8100 (o parâmetro 'Pagina' do plugin mostra o endereço)."
