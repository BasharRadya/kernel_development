// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEVICE_PATH "/dev/ctf"
#define SECRET_LENGTH 13

int main(void)
{
	int fd;
	char secret_code[SECRET_LENGTH + 1] = {
		0
	}; // Buffer to store the secret code with space for null terminator
	char ret; // Variable to store the returned character
	int pos = 0; // Position in the buffer

	// Open the device
	fd = open(DEVICE_PATH, O_RDWR);

	if (fd < 0) {
		perror("Failed to open the device");
		return EXIT_FAILURE;
	}

	// Perform the system calls and store the characters in secret_code

	ret = (char)write(fd, NULL, 97);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 97 here
	ret = (char)lseek(fd, -206, SEEK_END);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 50 here
	ret = (char)ioctl(fd, 98, 246);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 50 here

	ret = (char)read(fd, NULL, 17);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 67 here
	ret = (char)ioctl(fd, 156, 18);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos us 67 here
	ret = (char)write(fd, NULL, 175 - 67);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 175 here
	// 2nd read operation with NULL buffer
	ret = (char)read(fd, NULL, 183 - 175);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 183 here
	// 3rd seek operation
	ret = (char)lseek(fd, 70, SEEK_SET);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 70 here
	ret = (char)read(fd, NULL, 187 - 70);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos us 187 here
	ret = (char)lseek(fd, 148 - 187, SEEK_CUR);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos us 148 here
	ret = (char)ioctl(fd, 32, 142);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;

	ret = (char)ioctl(fd, 20, 104);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//fpos is 148 here
	ret = (char)read(fd, NULL, 164 - 148);
	secret_code[pos++] = ret;
	if (pos == 13)
		pos = 0;
	//f[ps is 164 here
	// Close the device
	close(fd);

	// Null-terminate the string
	secret_code[SECRET_LENGTH] = '\0';

	// Print the concatenated secret code
	printf("Secret code: %s\n", secret_code);

	return EXIT_SUCCESS;
}
