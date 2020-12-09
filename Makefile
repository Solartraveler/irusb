all:
	make -j -C src-device

clean:
	make -C src-device clean

test:
	./src-host/irusbControl.py --help