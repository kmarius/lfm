#!/bin/bash

set -e

oldpwd=$PWD
cd "${0%/*}"
cd ..
base=$PWD

pkgver=0.0.$(git rev-list --count HEAD)
pkgrel=1
pkgname=lfm-${pkgver}-${pkgrel}.deb
pkgdir=${base}/build/${pkgname%.deb}

### compile
mkdir -p build
(
cd build

# needed when using make instead of ninja, investigate
mkdir -p lua

cmake .. -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE=RelWithDebInfo
make
)

### install
(
cd build
DESTDIR=${pkgdir} make install

mkdir -p "${pkgdir}"/DEBIAN
sed -e 's/%(ARCH)/amd64/' \
	-e 's/%(VERSION)/'"${pkgver}-${pkgrel}"'/g' \
	"${base}"/pkg/debian/control > "${pkgdir}"/DEBIAN/control
)

### build package
(
cd build
dpkg-deb --build "${pkgdir##*/}"
)

### cleanup
mv "${pkgdir}".deb "$oldpwd"
rm -rf "$pkgdir"
