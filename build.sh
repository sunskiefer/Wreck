#!/usr/bin/env bash
# Wreck (L'Cronx): build for MPC OS / Force (armv7-a hard-float, glibc symbols <= 2.17). The Force is ARM; nothing else is built.
#   MPC_VST=/path/to/mpc-vst-plugins ./build.sh
# Needs: python3 + Pillow, a host gcc (only for the skin tool shadow_art), Zig (pip install ziglang). No Docker.
# MPC_VST = sunskiefer/mpc-vst-plugins branch lcronx-release-quoting @ 3845bea (Q-Link/automation fix, apostrophe-safe release.py).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:?set MPC_VST to your mpc-vst-plugins checkout}"
ZIG="${ZIG:-python3 -m ziglang}"
cd "$HERE"
python3 tools/patch_builder.py "$MPC_VST"
python3 tools/make_knob.py
python3 tools/make_data.py
NAME="$(python3 -c "import json;print(\"L'Cronx - VST - \"+json.load(open('vst.json'))['name'])")"
SO="$(python3 -c "import json;print(json.load(open('vst.json'))['so'])")"
mkdir -p build/tools build/arm
rm -rf build/skin
python3 "$MPC_VST/tools/gen_vst.py" vst.json
[ -f build/params.h ] && [ -d "build/skin/$NAME/Plugin Skins" ]
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11"
OBJS=""
for f in src/wreck.c src/wreck_view.c "$MPC_VST/wrapper/vst2_wrap.c"; do
  o="build/arm/$(basename "$f").o"
  $ZIG cc $TGT $COMMON -Ibuild -Isrc -I"$MPC_VST/wrapper" -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG cc $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  $OBJS -lm -ldl -lpthread -o "build/arm/$SO"
MAXV="$(readelf -V "build/arm/$SO" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)"
if [ "$(printf '%s\n2.17\n' "$MAXV" | sort -V | tail -1)" != "2.17" ]; then echo "ERROR: $SO needs glibc $MAXV (> 2.17)"; exit 1; fi
echo "highest glibc symbol: $MAXV"
PKG="build/package/$NAME"
rm -rf build/package && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp "build/arm/$SO" "$PKG/"
mkdir -p "$PKG/data/wav" "$PKG/data/user" && cp pad_data/layout.txt "$PKG/data/"
echo "-> $PKG"
