# Maintainer: Saeed Badreldin <saeed@helwanlinux.org>
pkgname=hel-store
pkgver=1.0.0
pkgrel=1
pkgdesc="Application store for Helwan Linux"
arch=('x86_64')
url="https://github.com/helwan-linux/store"
license=('GPL3')

depends=(
    'gtk3'
    'pacman'
    'curl'
    'json-c'
    'libayatana-appindicator'
)

makedepends=(
    'make'
    'gcc'
    'pkgconf'
)

source=("https://github.com/helwan-linux/store/archive/refs/heads/main.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/store-main/store"

    make
}

package() {
    cd "$srcdir/store-main/store"

    install -Dm755 hel-store \
        "$pkgdir/usr/bin/hel-store"

    install -Dm644 data/hel-store.desktop \
        "$pkgdir/usr/share/applications/hel-store.desktop"

    install -Dm644 data/hel-store.png \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps/hel-store.png"

    install -Dm644 data/about-logo.png \
        "$pkgdir/usr/share/helwan/hel-store/about-logo.png"

    install -Dm644 data/style.css \
        "$pkgdir/usr/share/helwan/hel-store/style.css"
}

