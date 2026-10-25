#!/bin/sh
# Arma un AppImage de Wolf3D-for-linux: un solo archivo que corre en cualquier
# distro de Linux x86-64, porque lleva dentro SDL2 y sus dependencias.
#
#   ./build-appimage.sh
#
# Resultado: Wolf3D-x86_64.AppImage
# No incluye ningun dato del juego: el usuario sigue poniendo los suyos.
set -e

APP=Wolf3D
OUT=$APP-x86_64.AppImage
DIR=AppDir

echo ">> compilando"
make clean >/dev/null 2>&1 || true
make

echo ">> armando $DIR"
rm -rf "$DIR"
mkdir -p "$DIR/usr/bin" "$DIR/usr/lib"
cp wolf "$DIR/usr/bin/"

# Bibliotecas: se copian las de SDL y las suyas, pero NO las del sistema base
# (glibc, X11, Mesa, drivers), porque esas tienen que ser las de cada maquina.
EXCLUDE='libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|ld-linux|libstdc++|libgcc_s|libX11|libxcb|libXext|libXau|libXdmcp|libGL|libGLX|libEGL|libdrm|libgbm|libwayland|libxkbcommon|libdbus|libudev|libselinux|libresolv'

echo ">> copiando bibliotecas"
ldd wolf | awk '/=> \//{print $3}' | sort -u | while read -r lib; do
    echo "$lib" | grep -Eq "$EXCLUDE" && continue
    cp -L "$lib" "$DIR/usr/lib/" 2>/dev/null || true
done
# dependencias de las que acabamos de copiar (SDL arrastra varias)
for i in 1 2 3; do
    for lib in "$DIR"/usr/lib/*.so*; do
        [ -e "$lib" ] || continue
        ldd "$lib" 2>/dev/null | awk '/=> \//{print $3}' | while read -r dep; do
            echo "$dep" | grep -Eq "$EXCLUDE" && continue
            base=$(basename "$dep")
            [ -e "$DIR/usr/lib/$base" ] || cp -L "$dep" "$DIR/usr/lib/" 2>/dev/null || true
        done
    done
done

echo ">> lanzador y metadatos"
cat > "$DIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"
# si no le pasas carpeta, busca los datos del juego: primero donde estas
# parado, luego junto al AppImage, luego en una subcarpeta "datos"
if [ $# -eq 0 ]; then
    AQUI=$(dirname "$(readlink -f "${APPIMAGE:-$0}")")
    for d in "$PWD" "$PWD/datos" "$AQUI" "$AQUI/datos"; do
        for f in "$d"/MAPHEAD.* "$d"/maphead.*; do
            [ -e "$f" ] && set -- "$d" && break 2
        done
    done
fi
exec "$HERE/usr/bin/wolf" "$@"
EOF
chmod +x "$DIR/AppRun"

cat > "$DIR/$APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Wolf3D for Linux
Comment=Motor propio de Wolfenstein 3D (necesita los archivos originales)
Exec=AppRun
Icon=wolf3d
Categories=Game;ActionGame;
Terminal=false
EOF
cp wolf3d.png "$DIR/wolf3d.png"

echo ">> appimagetool"
TOOL=./appimagetool-x86_64.AppImage
if [ ! -x "$TOOL" ]; then
    curl -L -o "$TOOL" \
      https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$TOOL"
fi

# si no hay FUSE (comun en maquinas viejas), se extrae y se corre asi
if ! "$TOOL" --version >/dev/null 2>&1; then
    echo "   sin FUSE: usando --appimage-extract-and-run"
    ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$DIR" "$OUT"
else
    ARCH=x86_64 "$TOOL" "$DIR" "$OUT"
fi

echo
echo "listo: $OUT"
echo "ponlo junto a tus archivos del juego y dale doble clic, o:"
echo "   ./$OUT /ruta/a/los/datos"
