#include <linux/module.h>	/* Для всех модулей	*/
#include <linux/kernel.h>	/* KERN_INFO		*/
#include <linux/init.h>		/* Макросы		*/
#include <linux/fs.h>		/* Макросы для устройств	*/
#include <linux/cdev.h>		/* Функции регистрации символьных устройств	*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("/dev/null with dumps");
MODULE_VERSION("0.1");

#define DEVICE_NAME  "nulldump"
#define CLASS_NAME  "nulldump_class"

static int major;
static dev_t dev;
static struct class *chrdev_class;
static struct device *sdev;

void hexdump(const char *prefix, const void *buf, size_t len)
{
	for (int i = 0; i < len; i++) {
		if (!(i % 16)) {
			if (i)
				pr_info("\n");
			pr_info("%s", prefix);
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
	pr_info("chrdev: write with size=%d by pid=%d (%s)\n", size, current->pid, current->comm);
    hexdump();
	return len;
}

static const struct file_operations chrdev_fops = {
	.owner  = THIS_MODULE,
	.read   = nulldump_read,
	.write  = nulldump_write,
};

static int __init nulldump_init(void)
{
	// "If major == 0 this functions will dynamically allocate a major and return its number."
    major = register_chrdev(0, DEVICE_NAME);
    if (major < 0) {
        pr_error("Cannor register chrdev nulldump");
        return major;
    }

    if (IS_ERR(chrdev_class = class_create(CLASS_NAME)))
	{
		pr_err("chrdev: class_create failed\n");
		ret = PTR_ERR(chrdev_class);
		goto err_unregister_chrdev;
	}

	if (IS_ERR(sdev = device_create(chrdev_class, NULL, dev, NULL, DEVICE_NAME))) 
	{
		pr_err("chrdev: device_create failed\n");
		ret = PTR_ERR(sdev);
		goto err_class_destroy;
	}

	pr_info("chrdev: module loaded\n");
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

