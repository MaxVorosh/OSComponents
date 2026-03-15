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

static struct class *membuf_class;
static dev_t module_dev;

static size_t base_device_size = 4096;
static size_t devices_number = 1;
static const size_t MAX_DEVICES = 16;

static struct device_info {
	static struct cdev cdev;
	static struct device *sdev;
	static dev_t dev;

	char* data;
	size_t size;

	struct rw_semaphore lock;
	atomic_t opened_by;
};

static device_info devices[MAX_DEVICES];
static DEFINE_MUTEX(global_lock);

static int create_membuf_device(int minor);
static void destroy_membuf_device(int minor);

static ssize_t devices_number_show(char *buffer, struct kernel_param *kp) {
    return sprintf(buf, "%d\n", devices_number);
}

static ssize_t devices_number_store(const char *val, struct kernel_param *kp) {
    sscanf(buf, "%du", &devices_number);
    return count;
}

static struct kernel_param_ops devices_number_ops = {
	.get = devices_number_show,
	.set = devices_number_store
};

static ssize_t size_show(struct device *dev, struct device_attribute *attr, const char *buf) {}
static ssize_t size_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {}

module_param(base_device_size, int, 0644);
module_param_cb(devices_number, &devices_number_ops, &devices_number, 0644);
DEVICE_ATTR(size, 0644, size_show, size_store);

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

static int create_membuf_device(int minor) {
	dev_t dev = MKDEV(MAJOR(module_dev), minor);
	devices[minor].dev = dev;
	devices[minor].data = kzalloc(base_device_size, GPF_KERNEL);
	if (!devices[minor].data) {
		return -ENOMEM;
	}
	devices[minor].size = base_device_size;
	init_rwsem(&devices[minor].lock);
	atomic_set(&devices[minor].opened_by, 0);

	cdev_init(&devices[minor].cdev, &membuf_fops);
	devices[minor].cdev.owner = THIS_MODULE;

	int ret = cdev_add(&devices[minor].cdev, dev, 1);
	if (ret < 0) {
		goto free_buffer;
	}
	devices[minor].sdev = device_create(membuf_class, NULL, dev, &devices[minor], "membuf_dev%d", minor);
	if (IS_ERR(devices[minor].sdev)) {
		ret = PTR_ERR(devices[minor].sdev);
		goto delete_cdev;
	}
	ret = device_create_file(devices[minor].sdev, &dev_attr_size);
	if (ret < 0) {
		device_destroy(membuf_class, devices[minor].dev);
		goto delete_cdev;
	}
	return 0;

delete_cdev:
	cdev_del(devices[minor].cdev);
free_buffer:
	kfree(devices[minor].data);
	return ret
}

static void destroy_membuf_device(int minor) {
	if (!devices[minor].data) {
		return;
	}
	device_destroy(devices[minor].dev);
	cdev_del(&devices[minor].cdev);
	kfree(devices[minor].data);
	devices[minor] = NULL;
}

static int __init membuf_init(void)
{
	if (devices_number <= 0 || devices_number > MAX_DEVICES) {
		pr_err("membuf: incorrect device number\n");
		return -EINVAL;
	}
	if (base_device_size <= 0) {
		pr_err("membuf: incorrect device size\n");
		return -EINVAL;
	}
	int ret;
    if ((ret = alloc_chrdev_region(&module_dev, 0, MAX_DEVICES, DEVICE_NAME)))
	{
		pr_err("membuf: failed to allocate region\n");
		return ret;
	}
	membuf_class = class_create("membuf");
	if (IS_ERR(membuf_class)) {
		ret = PTR_ERR(membuf_class);
		pr_err("membuf: failed to create class\n");
		goto unregister_class;
	}

	int device_id = 0;
	for (; device_id < devices_number; ++device_id) {
		ret = create_membuf_device(device_id);
		if (ret < 0) {
			pr_err("membuf: failed to create device %d\n", device_id);
			goto delete_devices;
		}
	}
	return 0;

delete_devices:
	device_id--;
	while (device_id > 0) {
		destroy_membuf_device(device_id);
		device_id--;
	}
unregister_class:
	unregister_chrdev_region(module_dev, MAX_DEVICES);
	return ret;
}

static void __exit membuf_exit(void)
{
	for (int i = 0; i < MAX_DEVICES; ++i) {
		destroy_membuf_device(i);
	}

	class_destroy(membuf_class);
	unregister_chrdev_region(module_dev, MAX_DEVICES);
	pr_info("membuf: module unloaded\n");
}

module_init(membuf_init);
module_exit(membuf_exit);

