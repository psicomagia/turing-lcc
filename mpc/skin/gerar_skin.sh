#!/bin/sh
# Gera a tela da MPC (skin) e o params.h a partir de params.json + layout.conf, com as ferramentas do
# projeto mpc-vst-plugins (MIT): MV=/caminho/do/mpc-vst-plugins sh mpc/skin/gerar_skin.sh
# Precisa de Python 3 com Pillow e Playwright (Chromium) para o desenho. O resultado vai para mpc/skin/gerado/.
set -e
cd "$(dirname "$0")"
[ -n "$MV" ] || { echo "defina MV=<checkout do mpc-vst-plugins>"; exit 1; }
rm -rf build/skin
SHADOW_ART="$MV/tools/html_art.py" python3 "$MV/tools/gen_vst.py" vst.json
rm -rf gerado && mkdir -p gerado
cp build/params.h gerado/
cp -r "build/skin/Alex Sheeny - VST - Turing LCC/Plugin Skins" gerado/
cp "build/skin/Alex Sheeny - VST - Turing LCC/version.xml" gerado/ 2>/dev/null || true
echo "ok: mpc/skin/gerado"
