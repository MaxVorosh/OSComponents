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
#define HEXDUMP_LEN 32

static int major;
static dev_t dev;
static struct class *chrdev_class;
static struct device *sdev;

void hexdump(const void *buf)
{
	for (int i = 0; i < HEXDUMP_LEN; i++) {
		if (!(i % 16)) {
			if (i)
				pr_info("\n");
		}
		if (i && !(i % 8) && (i % 16))
			pr_info("\t");
		pr_info("%02X ", ((uint8_t *)(buf))[i]);
	}
}

static ssize_t nulldump_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	pr_info("nulldump: read with size=%d by pid=%d (%s)\n", len, current->pid, current->comm);
	return 0;
}

static ssize_t nulldump_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("chrdev: write with size=%d by pid=%d (%s)\n", len, current->pid, current->comm);
	const char* data[HEXDUMP_LEN];
	int bytes_to_copy = HEXDUMP_LEN;
	for (int i = 0; i < len; i+=HEXDUMP_LEN) {
		if (i + HEXDUMP_LEN >= len) {
			bytes_to_copy = len - i;
		}
		int result = copy_from_user(data, buf + i, len);
		if (result != 0) {
			return -EFAULT;
		}
		hexdump(data);
	}
	return len;
}

static const struct file_operations nulldump_fops = {
	.owner  = THIS_MODULE,
	.read   = nulldump_read,
	.write  = nulldump_write,
};

static int __init nulldump_init(void)
{
	// "If major == 0 this functions will dynamically allocate a major and return its number."
    major = register_chrdev(0, DEVICE_NAME, &nulldump_fops);
    if (major < 0) {
        pr_err("Cannor register chrdev nulldump");
        return major;
    }

	int ret;
    if (IS_ERR(chrdev_class = class_create(CLASS_NAME)))
	{
		pr_err("nulldump: class_create failed\n");
		ret = PTR_ERR(chrdev_class);
		goto err_unregister_chrdev;
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
err_unregister_chrdev:
	unregister_chrdev(major, DEVICE_NAME);
	return ret;
}

static void __exit nulldump_exit(void)
{
	device_destroy(chrdev_class, dev);
	class_destroy(chrdev_class);
	unregister_chrdev(major, DEVICE_NAME);

	pr_info("chrdev: module unloaded\n");
}

module_init(nulldump_init);
module_exit(nulldump_exit);

