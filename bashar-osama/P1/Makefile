CC = gcc
AS = as
LD = ld
CFLAGS = -Wall

SRC = test_kdlp.c
TARGET = test_kdlp
SRC_ASM = test_kdlp_asm.s
OBJ_ASM = test_kdlp_asm.o
TARGET_ASM = test_kdlp_asm

all: $(TARGET) $(TARGET_ASM)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

$(TARGET_ASM): $(OBJ_ASM)
	$(LD) -o $(TARGET_ASM) $(OBJ_ASM)
$(OBJ_ASM): $(SRC_ASM)
	$(AS) -o $(OBJ_ASM) $(SRC_ASM)
clean:
	rm -f  $(TARGET) $(TARGET_ASM) $(OBJ_ASM)