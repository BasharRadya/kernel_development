// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

//char *getcwd(char buf[.size], size_t size);
char *get_current_dir_name(void);

int main(void)
{
	char *pwd = get_current_dir_name();

	printf("%s$\n ", pwd);
	free(pwd);
	return 0;
}
