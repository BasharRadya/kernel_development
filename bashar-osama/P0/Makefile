# Makefile for compiling main.c with specific flags and a clean option

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Werror

# Target executable
TARGET = main

# Source file
SRCS = main.c

# Object file
OBJS = $(SRCS:.c=.o)

# Default target to build the executable
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile the source file into object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean