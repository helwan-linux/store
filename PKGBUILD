# Maintainer: Saeed Badreldin <saeed@helwanlinux.org>
pkgname=hel-store
pkgver=1.0.0
pkgrel=1
pkgdesc="Application store for Helwan Linux"
arch=('x86_64')
url="https://github.com/helwan-linux/store"
license=('GPL3')
depends=('gtk3' 'pacman' 'curl' 'json-c' 'libayatana-appindicator')
makedepends=('git' 'make' 'gcc' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::https://github.com/helwan-linux/store/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "store-$pkgver/store"
    make
}

package() {
    cd "store-$pkgver/store"

    # تثبيت الملف التنفيذي
    install -Dm755 hel-store "$pkgdir/usr/bin/hel-store"

    # تثبيت ملف سطح المكتب والأيقونات والأصول
    install -Dm644 data/hel-store.desktop \
        "$pkgdir/usr/share/applications/hel-store.desktop"

    install -Dm644 data/hel-store.png \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps/hel-store.png"

    install -Dm644 data/about-logo.png \
        "$pkgdir/usr/share/hel-store/about-logo.png"

    install -Dm644 data/style.css \
        "$pkgdir/usr/share/hel-store/style.css"
}
