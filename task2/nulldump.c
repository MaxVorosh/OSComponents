#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("/dev/null with dumps");
MODULE_VERSION("0.1");

#define DEVICE_NAME "nulldump"
#define CLASS_NAME  "nulldump_class"
#define HEXDUMP_LEN 16

static dev_t dev;
static struct cdev chrdev_cdev;
static struct class *chrdev_class;
static struct device *sdev;

static void hexdump(const void *buf, size_t len, size_t line)
{
	char result_buf[HEXDUMP_LEN * 3 + 9];
	size_t offset = 0;
	offset += sprintf(result_buf, "%07zx ", line);
	for (int i = 0; i < len; i+=2) {
		int cur = ((uint8_t *)(buf))[i];
		int next = 0;
		if (i + 1 < len) {
			next = ((uint8_t *)(buf))[i + 1];
		}
		offset += sprintf(result_buf + offset, "%02x", next);
		offset += sprintf(result_buf + offset, "%02x ", cur);
	}
	pr_info("%s\n", result_buf);
}

static ssize_t nulldump_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	pr_info("nulldump: read with size=%zu by pid=%d (%s)\n", len, current->pid, current->comm);
	return 0;
}

static ssize_t nulldump_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("nulldump: write with size=%zu by pid=%d (%s)\n", len, current->pid, current->comm);
	const char* data[HEXDUMP_LEN];
	int bytes_to_copy = HEXDUMP_LEN;
	for (int i = 0; i < len; i+=HEXDUMP_LEN) {
		if (i + HEXDUMP_LEN >= len) {
			bytes_to_copy = len - i;
		}
		int result = copy_from_user(data, buf + i, bytes_to_copy);
		if (result != 0) {
			return -EFAULT;
		}
		hexdump(data, bytes_to_copy, i);
	}
	hexdump(NULL, 0, len);
	return len;
}

static const struct file_operations nulldump_fops = {
	.owner  = THIS_MODULE,
	.read   = nulldump_read,
	.write  = nulldump_write,
};

static int __init nulldump_init(void)
{
	int ret;
    if ((ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)))
	{
		pr_err("nulldump: failed to allocate device number\n");
		return ret;
	}
	pr_info("nulldump: registered (major=%d, minor=%d)\n", MAJOR(dev), MINOR(dev));

	cdev_init(&chrdev_cdev, &nulldump_fops);
	chrdev_cdev.owner = THIS_MODULE;

	if ((ret = cdev_add(&chrdev_cdev, dev, 1)))
	{
		pr_err("nulldump: cdev_add failed\n");
		goto err_unregister;
	}

    if (IS_ERR(chrdev_class = class_create(CLASS_NAME)))
	{
		pr_err("nulldump: class_create failed\n");
		ret = PTR_ERR(chrdev_class);
		goto err_cdev_del;
	}

	if (IS_ERR(sdev = device_create(chrdev_class, NULL, dev, NULL, DEVICE_NAME))) 
	{
		pr_err("nulldump: device_create failed\n");
		ret = PTR_ERR(sdev);
		goto err_class_destroy;
	}

	pr_info("nulldump: module loaded\n");
	return 0;

err_class_destroy:
	class_destroy(chrdev_class);
err_cdev_del:
	cdev_del(&chrdev_cdev);
err_unregister:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static void __exit nulldump_exit(void)
{
	device_destroy(chrdev_class, dev);
	class_destroy(chrdev_class);
	cdev_del(&chrdev_cdev);
	unregister_chrdev_region(dev, 1);

	pr_info("nulldump: module unloaded\n");
}

module_init(nulldump_init);
module_exit(nulldump_exit);

