all:
	make -j -C src-device

clean:
	make -C src-device clean

package:
	./src-host/makeDebianPackage.sh

test:
	./src-host/irusb-control.py --help
