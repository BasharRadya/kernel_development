// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

char *get_current_dir_name(void);

void print_pwd(void)
{
	char *pwd = get_current_dir_name();
	printf("%s$ ", pwd);
	free(pwd);
}

void get_input(void)
{
	char *input = NULL; //force getline to allloc it
	size_t len = 0;
	ssize_t byteread = getline(&input, &len, stdin);

	if (byteread == -1) {
		exit(0);
	}
	free(input);
}

int main(void)
{
	while (1) {
		print_pwd();
		get_input();
	}
	return 0;
}
