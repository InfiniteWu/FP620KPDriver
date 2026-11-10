#!/bin/sh
#
# Build the binary Debian package and a clean source tarball.
#
set -eu

VERSION="${VERSION:-0.1.0}"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="${OUT:-$HERE/../release}"
PKG="fp620kp-linux-$VERSION"

cd "$HERE"

if [ "$(dpkg-parsechangelog -S Version)" != "$VERSION" ]; then
    echo "debian/changelog 版本不是 $VERSION" >&2
    echo "请先修改 debian/changelog，或设置 VERSION=$(dpkg-parsechangelog -S Version)" >&2
    exit 1
fi

make clean
rm -rf "$OUT"
mkdir -p "$OUT"

echo "构建 Debian 包 ..."
dpkg-buildpackage -us -uc -b

echo "生成源码包 ..."
STAGE="$OUT/$PKG"
mkdir -p "$STAGE"
tar -C "$HERE" -cf "$OUT/$PKG.tar" \
    --exclude='debian/.debhelper' \
    --exclude='debian/debhelper-build-stamp' \
    --exclude='debian/files' \
    --exclude='debian/tmp' \
    --exclude='debian/*.substvars' \
    --exclude='debian/*.debhelper.log' \
    --exclude='debian/printer-driver-fp620kp' \
    --exclude='rastertofp620kp' \
    --exclude='fp620kp' \
    --exclude='src/*.o' \
    --exclude='tools/lpgstatus' \
    --exclude='tools/pjl_probe' \
    Makefile README.md install.sh release.sh jolimark-fp620kp.ppd \
    jolimark-fp620kp-foomatic.ppd src tests tools debian LICENSE
tar -C "$STAGE" -xf "$OUT/$PKG.tar"
rm -f "$OUT/$PKG.tar"
tar -C "$OUT" -czf "$OUT/$PKG.tar.gz" "$PKG"
rm -rf "$STAGE"

for f in "$HERE/../printer-driver-fp620kp_${VERSION}_"*.deb \
         "$HERE/../printer-driver-fp620kp_${VERSION}_"*.ddeb \
         "$HERE/../printer-driver-fp620kp_${VERSION}_"*.buildinfo \
         "$HERE/../printer-driver-fp620kp_${VERSION}_"*.changes; do
    [ -e "$f" ] && mv "$f" "$OUT/"
done

(cd "$OUT" && sha256sum *.deb *.tar.gz > SHA256SUMS)

echo
echo "发布文件位于：$OUT"
ls -l "$OUT"
