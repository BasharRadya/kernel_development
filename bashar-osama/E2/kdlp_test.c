// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#define NON_ZERO 1
#define LSEEK_OFFSET 10
#define LSEEK_OFFSET_OVERFLOW 999999
#define SMALLER_BUFFER 10
#define STRING_SIZE 24
const char *filename = "/proc/kdlp";

char *expected_string = "bashar-osama love linux\n";

ssize_t read_helper(int file, void *buffer, size_t bytes)
{
	bool read_EINTR = false;
	ssize_t bytes_read;

	while (!read_EINTR) {
		bytes_read = read(file, buffer, bytes);
		if (!(bytes_read == -1 && errno == EINTR)) {
			read_EINTR = true;
		}
	}
	return bytes_read;
}

void file_exists(void)
{
	struct stat buffer;

	if (stat(filename, &buffer) == 0) {
		printf("file exists:Passed\n");
	} else {
		printf("file exists:Failed, File %s does not exist.\n", filename);
		exit(0);
	}
}

void check_file_permissions(void)
{
	struct stat buffer;

	stat(filename, &buffer);
	int is_owner_read_only = (buffer.st_mode & 0400) &&
				 !(buffer.st_mode & 0200) &&
				 !(buffer.st_mode & 0100);

	int is_group_read_only = (buffer.st_mode & 0040) &&
				 !(buffer.st_mode & 0020) &&
				 !(buffer.st_mode & 0010);

	int is_others_read_only = (buffer.st_mode & 0004) &&
				  !(buffer.st_mode & 0002) &&
				  !(buffer.st_mode & 0001);

	if (is_owner_read_only && is_group_read_only && is_others_read_only) {
		printf("check file permissions:Passed\n");
	} else {
		printf("check file permissions:Failed, File %s is not read-only by all\n",
		       filename);
	}
}

void check_write(void)
{
	int file = open(filename, O_RDWR);

	if (file == -1 && errno == EACCES) {
		printf("check write:Passed\n");
	} else {
		printf("check write:Failed, (errno=%d not (EACCES))\n", errno);
	}
end:
	close(file);
}

void check_file_output(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("check file output:Error opening file");
		goto end;
	}
	char buffer[100];

	ssize_t bytes_read = read_helper(file, buffer, sizeof(buffer) - 1);

	if (bytes_read == -1) {
		perror("check file output:Read error");
		goto end;
	}
	buffer[bytes_read] = '\0';
	if (strcmp(buffer, expected_string) == 0) {
		printf("check file output:Passed\n");
	} else {
		printf("check file output:Failed,got:%s-expected:%s\n", buffer,
		       (expected_string + LSEEK_OFFSET));
	}
end:
	close(file);
}

void check_file_output_smaller_buffer(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("check file output smaller buffer:Error opening file");
		goto end;
	}
	char buffer[SMALLER_BUFFER];

	ssize_t bytes_read = read_helper(file, buffer, sizeof(buffer) - 1);

	if (bytes_read == -1) {
		perror("check file output smaller buffer:Read error");
		goto end;
	}
	buffer[bytes_read] = '\0';
	char tmp[STRING_SIZE];

	strcpy(tmp, expected_string);
	tmp[bytes_read] = '\0';

	if (strcmp(buffer, tmp) == 0) {
		printf("check file output smaller buffer:Passed\n");
	} else {
		printf("check file output smaller buffer:Failed,got:%s and expected:%s\n",
		       buffer, tmp);
	}
end:
	close(file);
}

void check_consecutive_read(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("check consecutive read:Error opening file");
		goto end;
	}
	char buffer[STRING_SIZE + 1];

	ssize_t bytes_read = read_helper(file, buffer, STRING_SIZE / 2);

	if (bytes_read == -1) {
		perror("check consecutive read:Read error");
		goto end;
	}
	read_helper(file, buffer + (STRING_SIZE / 2), STRING_SIZE / 2);
	if (bytes_read == -1) {
		perror("check consecutive read:Read error");
		goto end;
	}
	buffer[STRING_SIZE] = '\0';
	if (strcmp(buffer, expected_string) == 0) {
		printf("check consecutive read:Passed\n");
	} else {
		printf("check consecutive read:Failed,got:%s-expected:%s\n",
		       buffer, (expected_string));
	}
end:
	close(file);
}

void check_null_dereference(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("Error opening file");
		exit(0);
	}
	char *buffer = NULL;

	ssize_t bytes_read = read_helper(file, buffer, NON_ZERO);

	if (bytes_read == -1 && errno == 14) {
		printf("check null dereference:Passed\n");
	} else {
		printf("check null dereference:Failed, (errno=%d not 14(EFAULT))\n",
		       errno);
	}
	close(file);
}

void check_invaild_buffer_address(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("Error opening file");
		exit(0);
	}
	char *buffer = (void *)-17;
	ssize_t bytes_read = read_helper(file, buffer, NON_ZERO);

	if (bytes_read == -1 && errno == 14) {
		printf("check invalid buffer address:Passed\n");
	} else {
		printf("check invalid buffer address:Failed, (errno=%d not 14(EFAULT))\n",
		       errno);
	}
	close(file);
}

void check_lseek(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("check lseek offset overflow:Error opening file");
		goto end;
	}
	off_t offset = lseek(file, LSEEK_OFFSET, SEEK_SET);

	if (offset == (off_t)-1) {
		perror("check lseek offset overflow:Seek error");
		goto end;
	}
	char buffer[100];
	ssize_t bytes_read = read_helper(file, buffer, sizeof(buffer) - 1);

	if (bytes_read == -1) {
		perror("check lseek offset overflow:Read error");
		goto end;
	}
	buffer[bytes_read] = '\0';
	if (strcmp(buffer, expected_string + LSEEK_OFFSET) == 0) {
		printf("check lseek:Passed\n");
	} else {
		printf("check lseek:Failed,got:%s-expected:%s\n", buffer,
		       (expected_string + LSEEK_OFFSET));
	}
end:
	close(file);
}

void check_lseek_offset_overflow(void)
{
	int file = open(filename, O_RDONLY);

	if (file == -1) {
		perror("check lseek offset overflow:Error opening file");
		goto end;
	}
	off_t offset = lseek(file, LSEEK_OFFSET_OVERFLOW, SEEK_SET);

	if (offset == (off_t)-1) {
		perror("check lseek offset overflow:Seek error");
		goto end;
	}
	char buffer[100];
	ssize_t bytes_read = read_helper(file, buffer, sizeof(buffer) - 1);

	if (bytes_read == -1) {
		perror("check lseek offset overflow:Read error");
		goto end;
	}
	buffer[bytes_read] = '\0';
	if (bytes_read == 0) {
		printf("check lseek offset overflow:Passed\n");
	} else {
		printf("check lseek offset overflow:Failed,got:%s-expected:%s\n",
		       buffer, (expected_string + LSEEK_OFFSET));
	}
end:
	close(file);
}

int main(void)
{
	file_exists();
	check_file_permissions();
	check_write();
	check_file_output();
	check_file_output_smaller_buffer();
	check_consecutive_read();
	check_null_dereference();
	check_invaild_buffer_address();
	check_lseek();
	check_lseek_offset_overflow();
	return 0;
}
