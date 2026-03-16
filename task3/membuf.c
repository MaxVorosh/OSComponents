#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("number of data buffers");
MODULE_VERSION("0.1");

#define DEVICE_NAME "membuf"
#define CLASS_NAME  "membuf_class"

static struct class *membuf_class;
static dev_t module_dev;

static int base_device_size = 4096;
static int devices_number = 1;
#define MAX_DEVICES 16

struct device_info {
	struct cdev cdev;
	struct device *sdev;
	dev_t dev;

	char* data;
	size_t size;
	int creating;

	struct rw_semaphore lock;
	atomic_t opened_by;
};

static struct device_info devices[MAX_DEVICES];
static DEFINE_MUTEX(global_lock);

static int create_membuf_device(int minor);
static void destroy_membuf_device(int minor);

static int devices_number_show(char *buffer, const struct kernel_param *kp) {
    return sprintf(buffer, "%d\n", devices_number);
}

static int upscale_devices(int new_count) {
	int ret;
	int created_devices = devices_number;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (!devices[i].data) {
			ret = create_membuf_device(i);
			if (ret < 0) {
				pr_err("membuf: cannot create device");
				goto delete_created;
			}
			devices[i].creating = 1;
			created_devices++;
			if (created_devices == new_count) {
				break;
			}
		}
	}
	for (int i = 0; i < MAX_DEVICES; ++i) {
		devices[i].creating = 0;
	}
	return 0;

delete_created:
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (devices[i].creating) {
			destroy_membuf_device(i);
		}
	}
	return ret;
}

static int downscale_devices(int new_count) {
	int busy_devices = 0;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (devices[i].data && atomic_read(&devices[i].opened_by) > 0) {
			busy_devices++;
		}
	}
	if (busy_devices > new_count) {
		return -EBUSY;
	}
	int destroyed_devices = 0;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		// We are under global lock, so opened by cannot increase between 79 and 89 lines
		if (devices[i].data && atomic_read(&devices[i].opened_by) == 0) {
			destroy_membuf_device(i);
			destroyed_devices++;
			if (destroyed_devices + new_count == devices_number) {
				break;
			}
		}
	}
	return 0;
}

static int devices_number_store(const char *val, const struct kernel_param *kp) {
    int new_count;
    int ret = kstrtoint(val, 0, &new_count);
    if (ret) {
        return ret;
    }
    if (new_count <= 0 || new_count > MAX_DEVICES) {
        return -EINVAL;
    }

	mutex_lock(&global_lock);
	if (new_count == devices_number) {
		mutex_unlock(&global_lock);
		return 0;
	}
	if (new_count > devices_number) {
		ret = upscale_devices(new_count);
	}
	else {
		ret = downscale_devices(new_count);
	}
	if (ret == 0) {
		*(int*)kp->arg = new_count;
	}
	mutex_unlock(&global_lock);
	return ret;
}

static struct kernel_param_ops devices_number_ops = {
	.get = devices_number_show,
	.set = devices_number_store
};

static ssize_t device_size_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct device_info *info = dev_get_drvdata(dev);
	if (down_read_interruptible(&info->lock)) {
		return -ERESTARTSYS;
	}
	ssize_t ret = sprintf(buf, "%zu\n", info->size);
	up_read(&info->lock);
	return ret;
}
static ssize_t device_size_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct device_info *info = dev_get_drvdata(dev);
	int new_size;
    int ret = kstrtoint(buf, 0, &new_size);
    if (ret) {
        return ret;
    }
    if (new_size <= 0) {
        return -EINVAL;
    }
	if (down_write_killable(&info->lock)) {
		return -ERESTARTSYS;
	}
	if (atomic_read(&info->opened_by) > 0) {
		up_write(&info->lock);
		return -EBUSY;
	}
	char *new_data = kzalloc(new_size, GFP_KERNEL);
	if (!new_data) {
		up_write(&info->lock);
		return -ENOMEM;
	}
	memcpy(new_data, info->data, min(info->size, (size_t)new_size));
	kfree(info->data);
	info->data = new_data;
	info->size = new_size;
	up_write(&info->lock);
	return count;
}

module_param(base_device_size, int, 0644);
module_param_cb(devices_number, &devices_number_ops, &devices_number, 0644);
DEVICE_ATTR(size, 0644, device_size_show, device_size_store);

static int membuf_open(struct inode *inode, struct file *file) {
	struct device_info *info = container_of(inode->i_cdev, struct device_info, cdev);
	mutex_lock(&global_lock); // To avoid situation, where we a trying to open deleting device
	atomic_inc(&info->opened_by);
	mutex_unlock(&global_lock);
	file->private_data = info;
	pr_info("membuf: opened device");
	return 0;
}

static int membuf_release(struct inode *inode, struct file *file) {
	struct device_info *info = file->private_data;
	// No lock, because we cannot delete an open file
	atomic_dec(&info->opened_by);
	pr_info("membuf: released device");
	return 0;
}

static ssize_t membuf_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct device_info *info = file->private_data;
	if (!down_read_interruptible(&info->lock)) {
		return -ERESTARTSYS;
	}
	if (*off >= info->size) {
		up_read(&info->lock);
		return 0;
	}
	size_t read_size = min(len, info->size - *off);
	if (copy_to_user(buf, info->data + *off, read_size)) {
		up_read(&info->lock);
		return -EFAULT;
	}
	*off += read_size;
	up_read(&info->lock);
	return read_size;
}

static ssize_t membuf_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	struct device_info *info = file->private_data;
	if (len == 0) {
		return 0;
	}
	if (!down_write_killable(&info->lock)) {
		return -ERESTARTSYS;
	}
	if (*off >= info->size) {
		up_write(&info->lock);
		return -ENOSPC;
	}
	size_t write_size = min(len, info->size - *off);
	if (copy_from_user(info->data + *off, buf, write_size)) {
		up_write(&info->lock);
		return -EFAULT;
	}
	*off += write_size;
	up_write(&info->lock);
	return write_size;
}

static const struct file_operations membuf_fops = {
	.owner   = THIS_MODULE,
	.open    = membuf_open,
	.release = membuf_release,
	.read    = membuf_read,
	.write   = membuf_write,
};

static int create_membuf_device(int minor) {
	dev_t dev = MKDEV(MAJOR(module_dev), minor);
	devices[minor].dev = dev;
	devices[minor].data = kzalloc(base_device_size, GFP_KERNEL);
	if (!devices[minor].data) {
		return -ENOMEM;
	}
	devices[minor].size = base_device_size;
	init_rwsem(&devices[minor].lock);
	atomic_set(&devices[minor].opened_by, 0);
	devices[minor].creating = false;

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
	pr_info("membuf: device #%d created", minor);
	return 0;

delete_cdev:
	cdev_del(&devices[minor].cdev);
free_buffer:
	kfree(devices[minor].data);
	return ret;
}

static void destroy_membuf_device(int minor) {
	if (!devices[minor].data) {
		return;
	}
	device_destroy(membuf_class, devices[minor].dev);
	cdev_del(&devices[minor].cdev);
	kfree(devices[minor].data);
	devices[minor].data = NULL;
	devices[minor].creating = 0;
	pr_info("membuf: device #%d destroyed", minor);
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
	membuf_class = class_create(CLASS_NAME);
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
	for (int i = devices_number; i < MAX_DEVICES; ++i) {
		devices[i].data = NULL;
		devices[i].creating = false;
	}
	pr_info("membuf: module loaded");
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

