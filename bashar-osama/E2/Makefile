obj-m += kdlp.o
all: build build_tests
build:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
load:
	sudo insmod kdlp.ko
unload:
	sudo rmmod kdlp
build_tests:
	gcc -o kdlp_test kdlp_test.c
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	@if [ -f kdlp_test ]; then rm -f kdlp_test; fi