// SPDX-License-Identifier: MIT
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>

static const char *string_line = "bashar-osama love linux\n";

static const size_t string_line_len = 24;

static int kdlp_proc_show(struct seq_file *m, void *v)
{
	seq_puts(m, string_line);
	return 0;
}

static int __init proc_kdlp_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create_single("kdlp", 0, NULL, kdlp_proc_show);
	if (!pde)
		return -ENOMEM;
	return 0;
}

static void __exit proc_kdlp_exit(void)
{
	remove_proc_entry("kdlp", NULL);
}

module_init(proc_kdlp_init);
module_exit(proc_kdlp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("bashar-osama");
