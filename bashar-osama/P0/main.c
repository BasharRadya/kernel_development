// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
char *get_current_dir_name(void);

void print_pwd(void)
{
	char *pwd = get_current_dir_name();

	printf("%s$ ", pwd);
	fflush(stdout);
	free(pwd);
}

int sub_split(char *s, int start)
{
	int count = 0;
	while (!isspace(s[start]) && s[start] != '\0') {
		count++;
		start++;
	}
	return count;
}

char *make_sub_spliot(char *s, int start, int end)
{
	int sz = end - start + 1;
	char *new_s = (char *)malloc(sizeof(char) * sz);

	for (int i = start; i < end; i++) {
		new_s[i - start] = s[i];
	}
	new_s[sz - 1] = '\0';
	return new_s;
}

char **split(char *s, int *num_of_words)
{
	int length = strlen(s);
	char **arr = (char **)malloc(sizeof(char *) * length);
	int num_words = 0;
	int i = 0;

	while (i < length) {
		int tmp = sub_split(s, i);

		if (tmp != 0) {
			char *c = make_sub_spliot(s, i, i + tmp);

			arr[num_words] = c;
			num_words++;
			i = i + tmp;
		} else {
			i++;
		}
	}
	*num_of_words = num_words;
	return arr;
}

char **get_input(int *x)
{
	char *input = NULL; //force getline to allloc it
	size_t len = 0;
	ssize_t byteread = getline(&input, &len, stdin);

	if (byteread == 1 && input[0] == '\n') {
		free(input);
		return NULL;
	} else if (byteread != -1) {
		// delete \n at the end and if tis not their then
		// we are overriding null terminator with null terminator
		input[strcspn(input, "\n")] = '\0';
		char **arr = split(input, x);

		free(input);
		return arr;
	} else {
		exit(0);
	}
}

void free_all(char **c, int num_of_words)
{
	for (int i = 0; i < num_of_words; i++) {
		free(c[i]);
	}
	free((char *)c);
}

void do_execv(char **arr, int num_words, int start)
{
	//there is enough space in arr
	arr[num_words] = NULL;
	execv(arr[start], &arr[start]);
	perror("error in execv");
	// execv(const char *path, char *const argv[]);
}
void handle_command(char **arr, int num_words)
{
	if (strcmp(arr[0], "exit") == 0) {
		if (num_words != 1) {
			fprintf(stderr, "exit takes no arguments\n");
			return;
		}
		exit(0);
	} else if (strcmp(arr[0], "cd") == 0) {
		if (num_words != 2) {
			fprintf(stderr, "Usage cd: cd [dir]\n");
			return;
		}
		if (chdir(arr[1]) == -1) {
			perror("Error changing directory");
		};

	} else if (strcmp(arr[0], "exec") == 0) {
		if (num_words < 2) {
			fprintf(stderr,
				"exec only takes at least one argument\n");
			return;
		}
		do_execv(arr, num_words, 1);
	} else if (arr[0][0] == '.' || arr[0][0] == '/') {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			perror("Fork failed");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			do_execv(arr, num_words, 0);
			exit(EXIT_FAILURE);
		} else {
			pid_t child_pid = waitpid(pid, NULL, 0);

			if (child_pid == -1) {
				perror("Waitpid failed");
			}
		}
	} else {
		fprintf(stderr, "Unrecognized command: %s\n", arr[0]);
	}
	//this should be alwys be last
}
int main(void)
{
	while (1) {
		print_pwd();
		int num_words = 0;
		char **arr = get_input(&num_words);

		if (arr == NULL) {
			continue;
		}
		handle_command(arr, num_words);
		free_all(arr, num_words);
	}
	return 0;
}
