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

static dev_t dev;
static struct cdev chrdev_cdev;
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

