#!/usr/bin/env bash
set -euo pipefail

# Runs as an unprivileged user inside the architecture's Arch Linux container.
expected_arch=${1:?Usage: build-ci.sh ARCH}
[[ "$(uname -m)" == "$expected_arch" ]]
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
cp /out/PKGBUILD /out/scry-*.tar.gz "$build_dir/"
cd "$build_dir"
printf "source /etc/makepkg.conf\nPKGEXT='.pkg.tar.zst'\n" > makepkg.conf
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software PKGDEST=/out makepkg --config "$PWD/makepkg.conf" --cleanbuild --force --noconfirm
source PKGBUILD
package="/out/$pkgname-$pkgver-$pkgrel-$expected_arch.pkg.tar.zst"
bsdtar -xOf "$package" .PKGINFO | grep -Fx "arch = $expected_arch"
mkdir unpacked
bsdtar -xf "$package" -C unpacked
readelf -h unpacked/usr/bin/scry
case "$expected_arch" in
  x86_64) readelf -h unpacked/usr/bin/scry | grep -F 'Advanced Micro Devices X86-64' ;;
  aarch64) readelf -h unpacked/usr/bin/scry | grep -F 'AArch64' ;;
  *) exit 1 ;;
esac
# Exercise the real OpenGL renderer and STL parser under Mesa/Xvfb.
LIBGL_ALWAYS_SOFTWARE=1 QT_QPA_PLATFORM=xcb xvfb-run -a \
  timeout 30s unpacked/usr/bin/scry --diagnostics /repo/packaging/ci-triangle.stl | tee render.log
grep -Fx 'Model render: passed' render.log
grep -Fx 'Triangles: 1' render.log
unpacked/usr/bin/scry --version | grep -Fx "scry $pkgver"
cd /out
sha256sum "$pkgname-$pkgver.tar.gz" PKGBUILD "$pkgname-$pkgver-$pkgrel-$expected_arch.pkg.tar.zst" > "SHA256SUMS-$expected_arch"
