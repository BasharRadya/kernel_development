obj-m += kdlp.o
all: build_tests
build_tests:
	gcc -o kdlp_test kdlp_test.c
clean:
	@if [ -f kdlp_test ]; then rm -f kdlp_test; fi