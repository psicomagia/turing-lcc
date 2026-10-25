#!/bin/sh
# Compila o Turing LCC para a MPC (Gen1 armhf, glibc 2.31+) e monta dist/Turing-LCC-MPC.zip
# Precisa: zig (pip install ziglang), zip e os headers do ALSA em ALSA_INC (só .h; a biblioteca é aberta na hora).
set -e
cd "$(dirname "$0")/.."
ALSA_INC=${ALSA_INC:-/usr/include}
mkdir -p build
# tela da MPC: wrapper do mpc-vst-plugins (mpc/mpcvst, MIT) + params.h e skin gerados por mpc/skin/gerar_skin.sh
python3 -m ziglang cc -target arm-linux-gnueabihf.2.31 -mcpu=cortex_a7 -O2 -s -std=gnu11 -fPIC -shared -fvisibility=hidden \
  -Wl,--no-undefined -I"$ALSA_INC" -Icore -Impc/skin/gerado -Impc/mpcvst -o build/turing-lcc.so \
  mpc/tm_engine.c core/tm.c mpc/mpcvst/vst2_wrap.c -lpthread -ldl -lm
P="dist/Turing-LCC-MPC"; F="$P/Alex Sheeny - VST - Turing LCC"
rm -rf "$P" dist/Turing-LCC-MPC.zip && mkdir -p "$F/web"
cp build/turing-lcc.so "$F/"; cp web/tm.html "$F/web/"; cp mpc/pkg/plugin-meta.xml "$F/"
cp -r "mpc/skin/gerado/Plugin Skins" "$F/"; cp mpc/skin/gerado/version.xml "$F/"
cp mpc/pkg/install.sh mpc/pkg/uninstall.sh mpc/pkg/plugin_list.awk mpc/pkg/LEIA-ME.txt "$P/"
(cd "$P" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
(cd dist && zip -qr Turing-LCC-MPC.zip Turing-LCC-MPC)
echo "ok: dist/Turing-LCC-MPC.zip"
