obj-m += kdlp.o
all: build
build:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
load:
	sudo insmod kdlp.ko
	sudo chmod 666 /dev/kdlp-character-device
unload:
	sudo rmmod kdlp
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
