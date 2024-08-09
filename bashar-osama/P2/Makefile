obj-m += ctf.o

.PHONY: build clean load unload

build:
	make -C /lib/modules/$(shell uname -r)/build modules M=$(PWD)
clean:
	make -C /lib/modules/$(shell uname -r)/build clean M=$(PWD)
load:
	sudo insmod ctf.ko
unload:
	-sudo rmmod ctf
