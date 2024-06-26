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
#include <stdbool.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/types.h>

char *get_current_dir_name(void);

void print_pwd(void)
{
	char *pwd = get_current_dir_name();

	printf("%s$ ", pwd);
	fflush(stdout);
	free(pwd);
}

#define funptr(fptr) bool (*(fptr))(char)

bool space_cheker(char c)
{
	return isspace(c);
}

bool colon_checker(char c)
{
	return (c == ':');
}

int sub_split(char *s, int start, funptr(checker))
{
	int count = 0;

	while (!checker(s[start]) && s[start] != '\0') {
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

char **split(char *s, int *num_of_words, funptr(checker))
{
	int length = strlen(s);
	char **arr = (char **)malloc(sizeof(char *) * length);
	int num_words = 0;
	int i = 0;

	while (i < length) {
		int tmp = sub_split(s, i, checker);

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

char **get_input(int *x, funptr(checker))
{
	char *input = NULL; //force getline to allloc it
	size_t len = 0;
	ssize_t byteread = getline(&input, &len, stdin);

	if (byteread == 1 && input[0] == '\n') {
		free(input);
		return NULL;
	} else if (byteread != -1) {
		input[strcspn(input, "\n")] = '\0';
		// delete \n at the end and if tis not
		// their then we are overriding null terminator with null terminator
		char **arr = split(input, x, checker);

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
	perror("error in cmd execv");
}

void handle_command(char **arr, int num_words);

void some_name(char **arr, int num_words)
{
	char *path = getenv("PATH");
	int x;
	char **path_arr = split(path, &x, colon_checker);
	bool got_it = false;

	for (int i = 0; i < x; i++) {
		int s1 = strlen(arr[0]) + strlen(path_arr[i]);
		char *new_c = (char *)malloc(s1 + 2);

		sprintf(new_c, "%s/%s", path_arr[i], arr[0]);
		struct stat status;
		int xx = stat(new_c, &status);

		if (xx == -1) {
			free(new_c);
			continue;
		} else {
			got_it = true;
			free(arr[0]);
			arr[0] = new_c;
			handle_command(arr, num_words);
			break;
		}
	}
	if (!got_it) {
		fprintf(stderr, "Unrecognized command: %s\n", arr[0]);
	}
	free_all(path_arr, x);
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
			fprintf(stderr, "exec takes at least one argument\n");
			return;
		}
		do_execv(arr, num_words, 1);
	} else if (arr[0][0] == '.' || arr[0][0] == '/') {
		pid_t pid = fork();
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
		some_name(arr, num_words);
	}
}

bool is_suit_for_sub(char *cur_word)
{
	return cur_word[0] == '~';
}

int till_slash_or_nullptr(char *cur_word)
{
	int i = 0;

	while (cur_word[i] != '/' && cur_word[i] != '\0') {
		i++;
	}
	return i;
}

char *get_sub_directive(char *cur_word)
{
	int x = till_slash_or_nullptr(cur_word);
	char *new_dir = (char *)malloc(sizeof(char) * (x));

	strncpy(new_dir, cur_word + 1, x - 1);
	new_dir[x - 1] = '\0';
	return new_dir;
}

char *get_sub_directory(char *directive)
{
	int length = strlen(directive);

	if (length == 0) {
		char *env = getenv("HOME");
		char *new_env =
			(char *)malloc(sizeof(char) * (strlen(env) + 1));

		strcpy(new_env, env);
		return new_env;

	} else {
		struct passwd *pwd = getpwnam(directive);
		char *new_dir = (char *)malloc(sizeof(char) *
					       (strlen(pwd->pw_dir) + 1));

		strcpy(new_dir, pwd->pw_dir);
		return new_dir;
	}
}

char *do_sub(char *cur_word, char *directive, char *subs)
{
	int new_len = (strlen(cur_word) - strlen(directive) - 1 + strlen(subs));
	char *new_dir = (char *)malloc(sizeof(char) * (new_len + 1));

	if (strlen(cur_word) == (strlen(directive) + 1)) {
		sprintf(new_dir, "%s", subs);
	} else {
		sprintf(new_dir, "%s%s", subs,
			(cur_word + (strlen(directive) + 1)));
	}
	return new_dir;
}

void preprocess(char **arr, int num_words)
{
	for (int i = 0; i < num_words; i++) {
		char *cur_word = arr[i];

		if (is_suit_for_sub(cur_word)) {
			char *directive = get_sub_directive(cur_word);
			char *subs = get_sub_directory(directive);
			char *new_cur_word = do_sub(cur_word, directive, subs);

			free(cur_word);
			free(directive);
			free(subs);
			arr[i] = new_cur_word;
		}
	}
}

int main(void)
{
	while (1) {
		print_pwd();
		int num_words = 0;
		char **arr = get_input(&num_words, space_cheker);

		if (arr == NULL) {
			continue;
		}
		preprocess(arr, num_words);
		handle_command(arr, num_words);
		//this should be alwys be last
		free_all(arr, num_words);
	}
	return 0;
}
