#!/bin/bash

set -e

oldpwd=$PWD
cd "${0%/*}"/..
base=$PWD

pkgver=0.0.$(git rev-list --count HEAD)
pkgrel=1
pkgname=lfm-git
debname=${pkgname}-${pkgver}-${pkgrel}.deb
pkgdir=${base}/build/${debname%.deb}
arch=$(dpkg --print-architecture)
generator=

if command -v ninja >/dev/null; then
  generator=-GNinja
fi

### compile
cmake -B build $generator -DCMAKE_INSTALL_PREFIX="/usr" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build

### install
DESTDIR=${pkgdir} cmake --install build

# TODO: split build dependencies from runtime dependencies
mkdir -p "${pkgdir}"/DEBIAN
cat >"${pkgdir}"/DEBIAN/control <<EOF
Package: ${pkgname}
Version: ${pkgver}-${pkgrel}
Section: base
Priority: optional
Architecture: $arch
Depends: lua-posix, libpcre3-dev, libmagic-dev, luajit, libluajit-5.1-dev, libreadline-dev, zlib1g-dev, libunistring-dev, libev-dev, gcc, g++, pkg-config, libavformat-dev, libswscale-dev, libavcodec-dev, libdeflate-dev, libavdevice-dev
Maintainer: kmarius
Homepage: https://github.com/kmarius/lfm
Description: terminal file manager
EOF

### build package
dpkg-deb --build "${pkgdir}"

### cleanup
rm -rf "${pkgdir}"
mv "${pkgdir}".deb "$oldpwd"
