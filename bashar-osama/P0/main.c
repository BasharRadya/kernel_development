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
#include <fcntl.h>

char *get_current_dir_name(void);

struct pipe_handler;

typedef struct pipe_handler *PIPE_HANDLER;

PIPE_HANDLER create_pipe(void);

void clean_after_two_sons_spawned(PIPE_HANDLER ph);

bool set_up_pipe_in_first_son(PIPE_HANDLER ph);

bool set_up_pipe_in_second_son(PIPE_HANDLER ph);

void free_pipe_hanlder(PIPE_HANDLER ph);

struct pipe_handler {
	int p[2];
};

struct string_list {
	char *cur;
	struct string_list *next;
	struct string_list *last;
};

typedef struct string_list *STRING_LIST;

STRING_LIST _get_new_node(void)
{
	return malloc(sizeof(struct string_list));
}

STRING_LIST create_string_list(void)
{
	struct string_list *head = _get_new_node();

	head->cur = NULL;
	head->next = NULL;
	head->last = head;
	return head;
}

char *copy_string(char *to_copy)
{
	int len = strlen(to_copy);
	char *result = malloc(sizeof(char) * (len + 1));

	strcpy(result, to_copy);
	return result;
}

STRING_LIST add_to_string_list(STRING_LIST list, char *c)
{
	STRING_LIST last = _get_new_node();
	char *copied_string = copy_string(c);

	last->cur = copied_string;
	last->next = NULL;
	list->last->next = last;
	list->last = last;
	return list;
}

void free_list(STRING_LIST list)
{
	if (list == NULL) {
		return;
	}
	STRING_LIST cur = list;

	while (cur != NULL) {
		STRING_LIST next = cur->next;

		free(cur->cur);
		free(cur);
		cur = next;
	}
}



int get_list_len(STRING_LIST list)
{
	int i = 0;

	for (STRING_LIST cur = list->next; cur != NULL; cur = cur->next) {
		i++;
	}
	return i;
}

char **convert_to_args_by_stealing(STRING_LIST list, int *len)
{
	int n = get_list_len(list);
	char **args = malloc(sizeof(char *) * (n + 1));

	args[n] = NULL;
	STRING_LIST cur = list->next;

	for (int i = 0; i < n; i++) {
		args[i] = cur->cur;
		cur->cur = NULL;
		cur = cur->next;
	}
	*len = n;
	return args;
}

STRING_LIST convert_to_list(char **args, int n)
{
	STRING_LIST list = create_string_list();

	for (int i = 0; i < n; i++) {
		add_to_string_list(list, args[i]);
	}
	return list;
}

int get_char_count(char *str, char c)
{
	int n = 0;

	for (char *cur = str; *cur != '\0'; cur++) {
		if (*cur == c) {
			n++;
		}
	}
	return n;
}

char *add_char_before_after_symbol(char *str, char to_add, char symbol)
{
	int str_len = strlen(str);
	int symbol_count = get_char_count(str, symbol);
	int new_len = str_len + 2 * symbol_count;
	int new_size = new_len + 1;
	char *new_str = malloc(sizeof(char) * new_size);
	char *cur_read = str;
	char *cur_write = new_str;

	while (*cur_read != '\0') {
		if (*cur_read == symbol) {
			*cur_write = to_add;
			cur_write++;
			*cur_write = symbol;
			cur_write++;
			*cur_write = to_add;

		} else {
			*cur_write = *cur_read;
		}
		cur_read++;
		cur_write++;
	}
	*cur_write = '\0';
	return new_str;
}

struct cmd {
	char **args;
	int n;
	char *in;
	char *out;
};

typedef struct cmd *CMD;

char *copy_and_malloc_string(char *str)
{
	int size = strlen(str) + 1;
	char *result = malloc(sizeof(char) * size);

	strcpy(result, str);
	return result;
}

void copy_str_and_free_prev(char **dest, char *src)
{
	char *new = copy_and_malloc_string(src);

	free(*dest);
	*dest = new;
}

CMD create_cmd(char **args, int n)
{
	STRING_LIST args_list = create_string_list();
	CMD cmd = malloc(sizeof(struct cmd));
	char *in_ptr = NULL;
	char *out_ptr = NULL;

	for (int i = 0; i < n; i++) {
		if (strcmp(args[i], ">") == 0 || strcmp(args[i], "<") == 0) {
			if (i + 1 >= n || strcmp(args[i + 1], ">") == 0 ||
				strcmp(args[i + 1], "<") == 0) {
				free(cmd);
				free(in_ptr);
				free(out_ptr);
				free_list(args_list);
				fprintf(stderr, "Invalid redirection syntax\n");
				return NULL;
			}
			if (strcmp(args[i], ">") == 0) {
				copy_str_and_free_prev(&out_ptr, args[i + 1]);
			} else {
				copy_str_and_free_prev(&in_ptr, args[i + 1]);
			}
			i++;
		} else {
			add_to_string_list(args_list, args[i]);
		}
	}

	int len;
	char **new_args = convert_to_args_by_stealing(args_list, &len);

	free_list(args_list);
	cmd->args = new_args;
	cmd->in = in_ptr;
	cmd->out = out_ptr;
	cmd->n = len;
	return cmd;
}

void free_cmd(CMD cmd)
{
	if (cmd == NULL) {
		return;
	}
	free(cmd->in);
	free(cmd->out);
	for (int i = 0; i < cmd->n + 1; i++) {
		free(cmd->args[i]);
	}
	free(cmd->args);
	free(cmd);
}

bool handle_in_redirection(char *path)
{
	bool result = true;
	int filefd = open(path, O_RDONLY);

	if (filefd == -1) {
		result = false;
	}
	if (dup2(filefd, STDIN_FILENO) == -1) {
		result = false;
	}
	close(filefd);
	return result;
}

bool handle_out_redirection(char *path)
{
	bool result = true;
	int filefd = open(path, O_WRONLY | O_CREAT,
					  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	if (filefd == -1) {
		result = false;
	}
	fflush(stdout);
	if (dup2(filefd, STDOUT_FILENO) == -1) {
		result = false;
	}
	close(filefd);
	return result;
}

void print_pwd(void)
{
	char *pwd = get_current_dir_name();

	printf("%s$ ", pwd);
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

bool pipe_checker(char c)
{
	return (c == '|');
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

void free_all(char **c, int num_of_words);

int get_special_chars_count(char *str, funptr(special))
{
	int count = 0;

	for (char *cur = str; *cur != '\0'; cur++) {
		if (special(*cur)) {
			count++;
		}
	}
	return count;
}

char *rm_w_s(char *str)
{
	int ws_count = get_special_chars_count(str, space_cheker);
	int new_len = strlen(str) - ws_count;
	char *new = (char *)malloc(sizeof(char) * (new_len + 1));
	char *read = str;
	char *write = new;

	while (*read != '\0') {
		if (!space_cheker(*read)) {
			*write = *read;
			write++;
		}
		read++;
	}
	*write = '\0';
	return new;
}

bool check_if_input_valid(char *input)
{
	char *wo_white = rm_w_s(input);

	if (strlen(wo_white) == 0) {
		return true;
	}
	char *space_before_after_pipe =
			add_char_before_after_symbol(wo_white, ' ', '|');
	int args_len;
	char **args = split(space_before_after_pipe, &args_len, space_cheker);
	bool should_be_pipe = false;
	int last;
	bool result = true;

	for (int i = 0; i < args_len; i++) {
		bool is_pipe = strcmp(args[i], "|") == 0;
		bool is_ok = (should_be_pipe && is_pipe) ||
					 (!should_be_pipe && !is_pipe);
		if (!is_ok) {
			result = false;
			break;
		}
		should_be_pipe = !should_be_pipe;
		last = i;
	}
	if (result == true) {
		bool is_pipe = strcmp(args[last], "|") == 0;

		result = !is_pipe;
	}
	free(wo_white);
	free(space_before_after_pipe);
	free_all(args, args_len);
	return result;
}

CMD *get_cmd_arr(char *str, int *y)
{
	int arr_x = -1;
	char **arr = split(str, &arr_x, pipe_checker);
	CMD *cmd_arr = (CMD *)malloc(sizeof(struct cmd) * arr_x);

	for (int i = 0; i < arr_x; i++) {
		int cur_cmd_args_len;
		char **cur_cmd_args = split(arr[i], &cur_cmd_args_len, space_cheker);
		CMD cmd = create_cmd(cur_cmd_args, cur_cmd_args_len);

		cmd_arr[i] = cmd;
		free_all(cur_cmd_args, cur_cmd_args_len);
	}
	*y = arr_x;
	free_all(arr, arr_x);
	return cmd_arr;
}

CMD *get_input(int *x)
{
	char *input = NULL; //force getline to allloc it
	size_t len = 0;
	ssize_t byteread = getline(&input, &len, stdin);

	if (byteread != -1) {
		char *wo_w_s = rm_w_s(input);
		bool empty_cmd = strlen(wo_w_s) == 0;

		free(wo_w_s);
		if (empty_cmd) {
			free(input);
			return NULL;
		}
		if (!check_if_input_valid(input)) {
			fprintf(stderr, "pipe syntax is invalid\n");
			free(input);
			return NULL;
		}
		// delete \n at the end and if tis not their then
		// we are overriding null terminator with null terminator
		input[strcspn(input, "\n")] = '\0';
		char *tmp_str1 = add_char_before_after_symbol(input, ' ', '>');

		free(input);
		char *tmp_str2 = add_char_before_after_symbol(tmp_str1, ' ', '<');

		free(tmp_str1);
		CMD *cmd_arr = get_cmd_arr(tmp_str2, x);

		free(tmp_str2);
		return cmd_arr;
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
	perror("errro in execv cmd");
}

bool handle_command(CMD cmd, int num_words, PIPE_HANDLER first_pipe,
					PIPE_HANDLER second_pipe, bool is_only_command);

void some_name(CMD cmd, int num_words, PIPE_HANDLER first_pipe,
			   PIPE_HANDLER second_pipe)
{
	char **arr = cmd->args;
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
			handle_command(cmd, num_words, first_pipe, second_pipe,
						   false);
			break;
		}
	}
	if (!got_it) {
		fprintf(stderr, "Unrecognized command: %s\n", arr[0]);
	}
	free_all(path_arr, x);
}

void restore_in_out(int spare_in, int spare_out)
{
	dup2(spare_in, STDIN_FILENO);
	fflush(stdout);
	dup2(spare_out, STDOUT_FILENO);
	close(spare_in);
	close(spare_out);
}

bool redirection_preprocess(char *in, char *out);

bool handle_command(CMD cmd, int num_words, PIPE_HANDLER first_pipe,
					PIPE_HANDLER second_pipe, bool is_only_command)
{
	char **arr = cmd->args;

	if (strcmp(arr[0], "exit") == 0) {
		if (num_words != 1) {
			fprintf(stderr, "exit takes no arguments\n");
			return true;
		}
		return false;
	} else if (strcmp(arr[0], "cd") == 0) {
		if (num_words != 2) {
			fprintf(stderr, "Usage cd: cd [dir]\n");
			clean_after_two_sons_spawned(first_pipe);
			return true;
		}
		char *curr_dir = get_current_dir_name();
		if (chdir(arr[1]) == -1) {
			perror("Error changing directory");
		} else if (!is_only_command) {
			if (chdir(curr_dir) == -1) {
				perror("Error changing directory back");
			}
		}
		free(curr_dir);
		clean_after_two_sons_spawned(first_pipe);
		return true;

	} else if (strcmp(arr[0], "exec") == 0) {
		if (num_words < 2) {
			fprintf(stderr, "exec takes at least one argument\n");

			return true;
		}
		int spare_in = dup(STDIN_FILENO);
		int spare_out = dup(STDOUT_FILENO);
		bool red_success = redirection_preprocess(cmd->in, cmd->out);

		if (!red_success) {
			restore_in_out(spare_in, spare_out);
			return true;
		}
		if (!is_only_command) {
			cmd->args = cmd->args + 1;
			cmd->n = cmd->n - 1;
			handle_command(cmd, num_words - 1, first_pipe,
						   second_pipe, false);
			cmd->args = cmd->args - 1;
			cmd->n = cmd->n + 1;
			return true;
		}
		do_execv(arr, num_words, 1);

		restore_in_out(spare_in, spare_out);
	} else if (arr[0][0] == '.' || arr[0][0] == '/') {
		pid_t pid = fork();
		if (pid < 0) {
			perror("Fork failed");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			set_up_pipe_in_first_son(first_pipe);
			set_up_pipe_in_second_son(second_pipe);
			bool result = redirection_preprocess(cmd->in, cmd->out);

			free(first_pipe);
			free(second_pipe);
			if (!result) {
				perror("redirection failed");
				exit(EXIT_FAILURE);
			}
			do_execv(arr, num_words, 0);
			exit(EXIT_FAILURE);
		} else {
			//clean_after_two_sons_spawned(second_pipe);
			clean_after_two_sons_spawned(first_pipe);
			pid_t child_pid = waitpid(pid, NULL, 0);
			if (child_pid == -1) {
				perror("Waitpid failed");
			}
		}
	} else {
		some_name(cmd, num_words, first_pipe, second_pipe);
	}
	return true;
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

char *preprocess_single(char *str)
{
	char *directive = get_sub_directive(str);
	char *subs = get_sub_directory(directive);
	char *new_word = do_sub(str, directive, subs);

	free(directive);
	free(subs);
	return new_word;
}

void preprocess_single_and_replace_free_if_needed(char **str)
{
	if (is_suit_for_sub(*str)) {
		char *new = preprocess_single(*str);

		free(*str);
		*str = new;
	}
}

void preprocess(char **arr, int num_words)
{
	for (int i = 0; i < num_words; i++) {
		preprocess_single_and_replace_free_if_needed(arr + i);
	}
}

bool redirection_preprocess(char *in, char *out)
{
	bool result;

	if (in != NULL) {
		result = handle_in_redirection(in);
		if (!result) {
			perror("error in redirection");
			return false;
		}
	}
	result = false;
	if (out != NULL) {
		result = handle_out_redirection(out);
		if (!result) {
			perror("error in redirection");
			return false;
		}
	}
	return true;
}



PIPE_HANDLER create_pipe(void)
{
	PIPE_HANDLER ph = malloc(sizeof(struct pipe_handler));

	if (pipe(ph->p) == -1) {
		free(ph);
		return NULL;
	}
	return ph;
}

void clean_after_two_sons_spawned(PIPE_HANDLER ph)
{
	if (ph == NULL) {
		return;
	}
	close(ph->p[0]);
	close(ph->p[1]);
}

bool set_up_pipe_in_first_son(PIPE_HANDLER ph)
{
	if (ph == NULL) {
		return true;
	}
	close(ph->p[1]);
	bool success = true;

	if (dup2(ph->p[0], STDIN_FILENO) == -1) {
		success = false;
	}

	close(ph->p[0]);
	return success;
}

bool set_up_pipe_in_second_son(PIPE_HANDLER ph)
{
	if (ph == NULL) {
		return true;
	}
	close(ph->p[0]);
	bool success = true;

	fflush(stdout);
	if (dup2(ph->p[1], STDOUT_FILENO) == -1) {
		success = false;
	}
	close(ph->p[1]);
	return success;
}

void free_pipe_hanlder(PIPE_HANDLER ph)
{
	free(ph);
}

void free_cmd_list(CMD *list, int x)
{
	for (int i = 0; i < x; i++) {
		free_cmd(list[i]);
	}
	free(list);
}

int main(void)
{
	while (1) {
		print_pwd();
		int num_cmds = 0;

		CMD *cmd_arr = get_input(&num_cmds);

		if (cmd_arr == NULL) {
			continue;
		}
		PIPE_HANDLER first_pipe = NULL;
		PIPE_HANDLER second_pipe = NULL;

		if (num_cmds > 1) {
			second_pipe = create_pipe();
		}
		bool is_not_exit = true;

		for (int i = 0; i < num_cmds; i++) {
			if (cmd_arr[i] != NULL) {
				if (cmd_arr[i]->in != NULL) {
					preprocess_single_and_replace_free_if_needed
							(&cmd_arr[i]->in);
				}
				if (cmd_arr[i]->out != NULL) {
					preprocess_single_and_replace_free_if_needed
							(&cmd_arr[i]->out);
				}
				preprocess(cmd_arr[i]->args, cmd_arr[i]->n);
				bool is_only_command = num_cmds == 1;

				is_not_exit = handle_command(
						cmd_arr[i], cmd_arr[i]->n, first_pipe,
						second_pipe, is_only_command);
			} else {
				clean_after_two_sons_spawned(first_pipe);
			}
			free(first_pipe);
			first_pipe = second_pipe;
			if (i >= num_cmds - 2) {
				second_pipe = NULL;
			} else {
				second_pipe = create_pipe();
			}
		}
		//this should be alwys be last
		free_cmd_list(cmd_arr, num_cmds);
		if (!is_not_exit && num_cmds == 1) {
			exit(0);
		}
	}
	return 0;
}
