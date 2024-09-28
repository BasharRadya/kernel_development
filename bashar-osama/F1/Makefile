CC = gcc
FLAGS = -Wall -pthread
TARGET = test

.PHONY: build clean run all

all: build run

build:
	@$(CC) $(FLAGS) test.c -o $(TARGET)
clean:
	rm $(TARGET)
run:
	@./$(TARGET)
