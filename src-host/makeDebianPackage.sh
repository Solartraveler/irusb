#!/bin/bash

BASE=`dirname "$0"`

NAME="irusb-control"
BINARY="${BASE}/${NAME}"
PACKAGEDIR="${BASE}/packages"
CONTROLFILE="${PACKAGEDIR}/DEBIAN/control"
VERSION=1.0.0

mkdir -p "${PACKAGEDIR}/usr/bin"
cp "${BINARY}.py" "${PACKAGEDIR}/usr/bin/${NAME}"
chmod a+x "${PACKAGEDIR}/usr/bin/${NAME}"
mkdir -p "${PACKAGEDIR}/etc/udev/rules.d"
cp "${BASE}/55-irusb.rules" "${PACKAGEDIR}/etc/udev/rules.d/"
mkdir -p "${PACKAGEDIR}/usr/share/doc/${NAME}"
cp "${BASE}/LICENSE" "${PACKAGEDIR}/usr/share/doc/${NAME}/copyright"

rm -f ${CONTROLFILE}

BINARYBYTES=`du -s -k ${PACKAGEDIR} | cut -f1 `

#Its a python script, so architecture is not important
ARCHITECTURE=all

echo "Package: irusb-control" > ${CONTROLFILE}
echo "Version: ${VERSION}" >> ${CONTROLFILE}
echo "Priority: optional" >> ${CONTROLFILE}
echo "Architecture: ${ARCHITECTURE}" >> ${CONTROLFILE}
echo "Essential: no" >> ${CONTROLFILE}
echo "Installed-size: ${BINARYBYTES}" >> ${CONTROLFILE}
echo "Maintainer: Malte Marwedel" >> ${CONTROLFILE}
echo "Homepage: https://github.com/Solartraveler/irusb" >> ${CONTROLFILE}
echo "Description: Program for controlling IRUSB hardware. Can send and receive infrared remote control commands." >> ${CONTROLFILE}
echo "Depends: python3, python3-usb" >> ${CONTROLFILE}
dpkg-deb --root-owner-group --build "${PACKAGEDIR}" "${BASE}/../${NAME}_${VERSION}_${ARCHITECTURE}.deb"
