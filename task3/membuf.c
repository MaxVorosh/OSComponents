#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("/dev/null with dumps");
MODULE_VERSION("0.1");

#define DEVICE_NAME "membuf"
#define CLASS_NAME  "membuf_class"

static dev_t dev;
static struct cdev chrdev_cdev;
static struct class *chrdev_class;
static struct device *sdev;

static size_t base_device_size;
static size_t devices_number;

static ssize_t base_device_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", base_device_size);
}

static ssize_t base_device_size_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count) {
    sscanf(buf, "%du", &base_device_size);
    return count;
}

static ssize_t devices_number_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", devices_number);
}

static ssize_t devices_number_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count) {
    sscanf(buf, "%du", &devices_number);
    return count;
}

DEVICE_ATTR(base_device_size, 0660, base_device_size_show, (void *)base_device_size_store); 
DEVICE_ATTR(devices_number, 0660, devices_number_show, (void *)devices_number_store); 

static ssize_t membuf_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	
	return 0;
}

static ssize_t membuf_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	
	return len;
}

static const struct file_operations membuf_fops = {
	.owner  = THIS_MODULE,
	.read   = membuf_read,
	.write  = membuf_write,
};

static int __init membuf_init(void)
{
	int ret;
    if ((ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME)))
	{
		pr_err("membuf: failed to allocate device number\n");
		return ret;
	}
	pr_info("membuf: registered (major=%d, minor=%d)\n", MAJOR(dev), MINOR(dev));

	cdev_init(&chrdev_cdev, &membuf_fops);
	chrdev_cdev.owner = THIS_MODULE;

	if ((ret = cdev_add(&chrdev_cdev, dev, 1)))
	{
		pr_err("membuf: cdev_add failed\n");
		goto err_unregister;
	}

    if (IS_ERR(chrdev_class = class_create(CLASS_NAME)))
	{
		pr_err("membuf: class_create failed\n");
		ret = PTR_ERR(chrdev_class);
		goto err_cdev_del;
	}

	if (IS_ERR(sdev = device_create(chrdev_class, NULL, dev, NULL, DEVICE_NAME))) 
	{
		pr_err("membuf: device_create failed\n");
		ret = PTR_ERR(sdev);
		goto err_class_destroy;
	}

	pr_info("membuf: module loaded\n");
	return 0;

err_class_destroy:
	class_destroy(chrdev_class);
err_cdev_del:
	cdev_del(&chrdev_cdev);
err_unregister:
	unregister_chrdev_region(dev, 1);
	return ret;
}

static void __exit membuf_exit(void)
{
	device_destroy(chrdev_class, dev);
	class_destroy(chrdev_class);
	cdev_del(&chrdev_cdev);
	unregister_chrdev_region(dev, 1);

	pr_info("membuf: module unloaded\n");
}

module_init(membuf_init);
module_exit(membuf_exit);

