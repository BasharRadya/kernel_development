#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "constants.h"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

MODULE_LICENSE("GPL");

static dev_t ctf_dev;
static struct cdev *ctf_cdev;
static struct class *ctf_class;
static const u8 bytes[] = { SECRET_MESSAGE };

struct ctf
{
	size_t index;
	u8 message_val, decode_val;
};

static u8 update_value(u8 val, u8 x, u8 y)
{
	u32 factor1 = 1 + (u32)val;
	u32 factor2 = 1 + (u32)x;
	u32 product = (factor1 * factor2) % 257;
	return y ^ (u8)product;
}

static void mystery(const char *func_name, struct ctf *ctf, u8 operation_secret, u8 message_secret, u8 decode_secret)
{
	ctf->message_val = update_value(ctf->message_val, message_secret, operation_secret);
	ctf->decode_val = update_value(ctf->decode_val, decode_secret, operation_secret);
	pr_info("Mystery called by %s updated value to %hhu\n", func_name, ctf->decode_val);
}

static u8 get_message_byte(struct ctf *ctf)
{
	u8 encoded_byte = bytes[ctf->index];
	ctf->index++;
	if(ctf->index == sizeof bytes)
		ctf->index = 0;
	return encoded_byte ^ ctf->message_val;
}

static int ctf_open(struct inode *inode, struct file *file)
{
	struct device *ctf_device = class_find_device_by_devt(ctf_class, ctf_dev);
	struct ctf *ctf = dev_get_drvdata(ctf_device);
	ctf->index = 0;
	ctf->message_val = INIT_MSECRET;
	ctf->decode_val = INIT_DSECRET;
	file->private_data = ctf;
	return 0;
}

static int ctf_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t ctf_read(struct file *file, char __user *data, size_t count, loff_t *f_pos)
{
	struct ctf *ctf = file->private_data;
	if(count > 256 || *f_pos + count > 256)
		return -EIO;
	*f_pos += count;
	mystery("read", ctf, (u8)*f_pos, READ_MSECRET, READ_DSECRET);
	return (ssize_t)get_message_byte(ctf);
}

static ssize_t ctf_write(struct file *file, const char __user *data, size_t count, loff_t *f_pos)
{
	struct ctf *ctf = file->private_data;
	if(count > 256 || *f_pos + count > 256)
		return -EIO;
	*f_pos += count;
	mystery("write", ctf, (u8)*f_pos, WRITE_MSECRET, WRITE_DSECRET);
	return (ssize_t)get_message_byte(ctf);
}

static long ctf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ctf *ctf = file->private_data;
	mystery("ioctl1", ctf, (u8)cmd, IOC_CMD_MSECRET, IOC_CMD_DSECRET);
	mystery("ioctl2", ctf, (u8)arg, IOC_ARG_MSECRET, IOC_ARG_DSECRET);
	return (long)get_message_byte(ctf);
}

static loff_t ctf_llseek(struct file *file, loff_t off, int whence)
{
	struct ctf *ctf = file->private_data;
	u8 msecret, dsecret;
	loff_t base;
	switch(whence)
	{
	case SEEK_SET:
		base = 0;
		msecret = SEEK_SET_MSECRET;
		dsecret = SEEK_SET_DSECRET;
		break;
	case SEEK_CUR:
		base = file->f_pos;
		msecret = SEEK_CUR_MSECRET;
		dsecret = SEEK_CUR_DSECRET;
		break;
	case SEEK_END:
		base = 256;
		msecret = SEEK_END_MSECRET;
		dsecret = SEEK_END_DSECRET;
		break;
	}
	if(base + off < 0 || 256 < base + off)
		return -EINVAL;
	mystery("seek1", ctf, (u8)file->f_pos, msecret, dsecret);
	file->f_pos = base + off;
	mystery("seek2", ctf, (u8)file->f_pos, msecret, dsecret);
	return (loff_t)get_message_byte(ctf);
}


static const struct file_operations ctf_fops = {
	.owner = THIS_MODULE,
	.open = ctf_open,
	.release = ctf_release,
	.read = ctf_read,
	.write = ctf_write,
	.unlocked_ioctl = ctf_ioctl,
	.llseek = ctf_llseek,
};

static char *ctf_node(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int ctf_init(void)
{
	struct device *ctf_device;
	struct ctf *ctf;
	int ret = alloc_chrdev_region(&ctf_dev, 0, 1, "ctf");
	if (ret < 0) {
		pr_err("init: unnable to allocate region %i\n", ret);
		goto fail_region;
	}

	ctf_cdev = cdev_alloc();
	if (!ctf_cdev) {
		ret = -ENOMEM;
		pr_err("init: unnable to allocate char dev %i\n", ret);
		goto fail_cdev;
	}

	cdev_init(ctf_cdev, &ctf_fops);

	ret = cdev_add(ctf_cdev, ctf_dev, 1);
	if (ret < 0) {
		pr_err("init: unnable to add char dev %i\n", ret);
		goto fail_add;
	}

	ctf_class = class_create("ctf");
	if (IS_ERR(ctf_class)) {
		ret = PTR_ERR(ctf_class);
		pr_err("init: unable to create device class %i\n", ret);
		goto fail_add;
	}

	ctf_class->devnode = ctf_node;

	ctf = kzalloc(sizeof(*ctf), GFP_KERNEL);
	if (!ctf) {
		ret = -ENOMEM;
		pr_err("init: failed to allocate ctf struct %i\n", ret);
		goto fail_class;
	}

	ctf_device = device_create(ctf_class, NULL, ctf_dev, ctf, "ctf");
	if (IS_ERR(ctf_device)) {
		ret = PTR_ERR(ctf_device);
		pr_err("init: unable to create device %i\n", ret);
		goto fail_device;
	}

	pr_info("init called\n");

	return 0;

fail_device:
	kfree(ctf);
fail_class:
	class_destroy(ctf_class);
fail_add:
	cdev_del(ctf_cdev);
fail_cdev:
	unregister_chrdev_region(ctf_dev, 1);
fail_region:
	return ret;
}

static void ctf_exit(void)
{
	struct device *ctf_device = class_find_device_by_devt(ctf_class, ctf_dev);
	struct ctf *ctf = dev_get_drvdata(ctf_device);
	pr_info("exit called\n");
	kfree(ctf);
	device_destroy(ctf_class, ctf_dev);
	class_destroy(ctf_class);
	cdev_del(ctf_cdev);
	unregister_chrdev_region(ctf_dev, 1);
}

module_init(ctf_init);
module_exit(ctf_exit);

