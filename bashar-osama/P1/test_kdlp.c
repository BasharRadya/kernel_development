#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>

#define NR_kdlp 452
#define TASK_COMM_LEN 16

long kdlp(char *buf, size_t count)
{
	return syscall(NR_kdlp, buf, count);
}

int main(void)
{
	int buffer_size = 19 + 1 + TASK_COMM_LEN;
	char buffer[buffer_size];
	long result = kdlp(buffer, sizeof(buffer));

	if (result > 0) {
		if (write(STDOUT_FILENO, buffer, result) < 0) {
			fprintf(stderr, "write error\n");
			return 1;
		}
		if (write(STDOUT_FILENO, "\n", 1) < 0) {
			fprintf(stderr, "write error\n");
			return 1;
		}
	} else {
		fprintf(stderr, "kdlp failed\n");
	}

	return 0;
}
