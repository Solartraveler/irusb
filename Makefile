all:
	make -j -C src-device

clean:
	make -C src-device clean

package:
	/bin/bash ${CURDIR}/src-host/makeDebianPackage.sh

test:
	${CURDIR}/src-host/irusb-control.py --help
