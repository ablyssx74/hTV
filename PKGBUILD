# Maintainer: Kris Beazley (ablyss) <hTV@epluribusunix.net>
#
# Local/dev PKGBUILD: run `makepkg -si` directly from a checkout of this
# repo (no separate tarball/source download -- it builds straight out of
# the working directory via CMakeLists.txt). pkgver() reads VERSION so it
# never needs hand-editing to match a release; makepkg rewrites the pkgver=
# line below in place if it doesn't already match, which is expected.

pkgname=htv
pkgver=1.3.0
pkgrel=1
pkgdesc="SDL2/libmpv media player with a right-click EQ/Limiter/Reverb/Chorus config window (Qt6, X11/Wayland)"
arch=('x86_64')
url="https://github.com/ablyssx74/hTV"
license=('MIT')
depends=('sdl2' 'mpv' 'ffmpeg' 'curl' 'qt6-base' 'qt6-wayland')
makedepends=('cmake')
source=()
sha256sums=()

pkgver() {
    grep -oP 'v\K[0-9.]+' "$startdir/VERSION"
}

build() {
    cmake -B build -S "$startdir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
    install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
