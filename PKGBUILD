# Maintainer: Saeed Badreldin <saeed@helwanlinux.org>
pkgname=rody-store
pkgver=1.0.0
pkgrel=1
pkgdesc="Application store for Helwan Linux"
arch=('x86_64')
url="https://github.com/helwan-linux/store"
license=('GPL3')
depends=('gtk4' 'libadwaita' 'pacman' 'curl' 'json-c')
makedepends=('git' 'make' 'gcc' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::https://github.com/helwan-linux/store/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "store-$pkgver"
    make
}

package() {
    cd "store-$pkgver"
    
    # تثبيت الملف التنفيذي للمتجر
    install -Dm755 hel-store "$pkgdir/usr/bin/hel-store"
    
    # تثبيت ملف سطح المكتب (Desktop Entry) والأيقونات والملفات التابعة
    install -Dm644 data/hel-store.desktop "$pkgdir/usr/share/applications/hel-store.desktop"
    install -Dm644 data/hel-store.png "$pkgdir/usr/share/icons/hicolor/256x256/apps/hel-store.png"
    install -Dm644 data/about-logo.png "$pkgdir/usr/share/rody-store/about-logo.png"
    install -Dm644 data/style.css "$pkgdir/usr/share/rody-store/style.css"
}
