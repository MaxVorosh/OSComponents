#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/circ_buf.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("number of pipe buffers");
MODULE_VERSION("0.1");

#define DEVICE_NAME "pipebuf"
#define CLASS_NAME  "pipebuf_class"

static struct class *pipebuf_class;
static dev_t module_dev;

static int base_device_size = 4096;
static int devices_number = 1;
#define MAX_DEVICES 16

static DECLARE_WAIT_QUEUE_HEAD(read_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_wait_queue);

struct device_info {
	struct cdev cdev;
	struct device *sdev;
	dev_t dev;

	struct circ_buf data;
	int empty;
	size_t size;
	int creating;

	struct mutex lock;
	atomic_t opened_by;
	atomic_t readers;
	atomic_t writers;
};

static struct device_info devices[MAX_DEVICES];
static DEFINE_MUTEX(global_lock);

static int create_pipebuf_device(int minor);
static void destroy_pipebuf_device(int minor);

static int devices_number_show(char *buffer, const struct kernel_param *kp) {
    return sprintf(buffer, "%d\n", devices_number);
}

static int upscale_devices(int new_count) {
	int ret;
	int created_devices = devices_number;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (!devices[i].data.buf) {
			ret = create_pipebuf_device(i);
			if (ret < 0) {
				pr_err("pipebuf: cannot create device");
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
			destroy_pipebuf_device(i);
		}
	}
	return ret;
}

static int downscale_devices(int new_count) {
	int busy_devices = 0;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		if (devices[i].data.buf && atomic_read(&devices[i].opened_by) > 0) {
			busy_devices++;
		}
	}
	if (busy_devices > new_count) {
		return -EBUSY;
	}
	int destroyed_devices = 0;
	for (int i = 0; i < MAX_DEVICES; ++i) {
		// We are under global lock, so opened by cannot increase between 79 and 89 lines
		if (devices[i].data.buf && atomic_read(&devices[i].opened_by) == 0) {
			destroy_pipebuf_device(i);
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
	mutex_lock(&info->lock);
	ssize_t ret = sprintf(buf, "%zu\n", info->size);
	mutex_unlock(&info->lock);
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
	mutex_lock(&info->lock);
	if (atomic_read(&info->opened_by) > 0) {
		mutex_unlock(&info->lock);
		return -EBUSY;
	}
	char *new_data = kzalloc(new_size, GFP_KERNEL);
	if (!new_data) {
		mutex_unlock(&info->lock);
		return -ENOMEM;
	}
	size_t current_size = CIRC_CNT(info->data.head, info->data.tail, info->size);
	if (!info->empty) {
		if (current_size == 0) {
			current_size = info->size;
		}
		if (info->data.head > info->data.tail) {
			// Simple case
			memcpy(new_data, info->data.buf + info->data.tail, min(current_size, (size_t)new_size));
		}
		else {
			size_t end_fragment = min(info->size - 1 - info->data.tail, current_size);
			size_t begin_fragment = current_size - end_fragment;
			size_t write_first_batch = min(end_fragment, (size_t)new_size);
			if (end_fragment > 0) {
				memcpy(new_data, info->data.buf + info->data.tail, write_first_batch);
			}
			if (begin_fragment > 0 && new_size > end_fragment) {
				memcpy(new_data + write_first_batch, info->data.buf, min(begin_fragment, (size_t)(new_size - end_fragment)));
			}
		}
	}
	kfree(info->data.buf);
	info->data.buf = new_data;
	info->data.tail = 0;
	info->data.head = min(current_size, (size_t)new_size) % new_size;
	info->size = new_size;
	mutex_unlock(&info->lock);
	return count;
}

module_param(base_device_size, int, 0644);
module_param_cb(devices_number, &devices_number_ops, &devices_number, 0644);
DEVICE_ATTR(size, 0644, device_size_show, device_size_store);

static int pipebuf_open(struct inode *inode, struct file *file) {
	struct device_info *info = container_of(inode->i_cdev, struct device_info, cdev);
	mutex_lock(&global_lock); // To avoid situation, where we a trying to open deleting device
	atomic_inc(&info->opened_by);
	if (file->f_mode & FMODE_READ) {
		if (atomic_read(&info->readers) == 1) {
			mutex_unlock(&global_lock);
			return -EACCES;
		}
		atomic_inc(&info->readers);
	}
	if (file->f_mode & FMODE_WRITE) {
		atomic_inc(&info->writers);
	}
	mutex_unlock(&global_lock);
	file->private_data = info;
	return 0;
}

static int pipebuf_release(struct inode *inode, struct file *file) {
	struct device_info *info = file->private_data;
	// No lock, because we cannot delete an open file
	atomic_dec(&info->opened_by);
	if (file->f_mode & FMODE_READ) {
		atomic_dec(&info->readers);
	}
	if (file->f_mode & FMODE_WRITE) {
		atomic_dec(&info->writers);
	}
	return 0;
}

static ssize_t pipebuf_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct device_info *info = file->private_data;
	if (len == 0) {
		return 0;
	}
	mutex_lock(&info->lock);
	while (info->empty) {
		if (atomic_read(&info->writers) == 0) {
			mutex_unlock(&info->lock);
			return 0;
		}
		mutex_unlock(&info->lock);
		if (wait_event_interruptible(read_wait_queue, !info->empty || atomic_read(&info->writers) == 0)) {
			return -ERESTARTSYS;
		}
		mutex_lock(&info->lock);
	}
	size_t read_size = min(len, CIRC_CNT(info->data.head, info->data.tail, info->size));
	if (read_size == 0) {
		read_size = min(len, info->size);
	}
	size_t end_fragment = min(info->size - 1 - info->data.tail, read_size);
	size_t begin_fragment = read_size - end_fragment;
	if (end_fragment > 0 && copy_to_user(buf, info->data.buf + info->data.tail, end_fragment)) {
		mutex_unlock(&info->lock);
		return -EFAULT;
	}
	if (begin_fragment > 0 && copy_to_user(buf, info->data.buf, begin_fragment)) {
		mutex_unlock(&info->lock);
		return -EFAULT;
	}
	info->data.tail += read_size;
	info->data.tail %= info->size;
	if (info->data.head == info->data.tail) {
		info->empty = 1;
	}
	wake_up(&write_wait_queue);
	mutex_unlock(&info->lock);
	return read_size;
}

static ssize_t pipebuf_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	struct device_info *info = file->private_data;
	if (len == 0) {
		return 0;
	}
	mutex_lock(&info->lock);
	while (!info->empty && CIRC_CNT(info->data.head, info->data.tail, info->size) == 0) {
		mutex_unlock(&info->lock);
		if (wait_event_interruptible(write_wait_queue, info->empty || CIRC_CNT(info->data.head, info->data.tail, info->size) > 0)) {
			return -ERESTARTSYS;
		}
		mutex_lock(&info->lock);
	}
	size_t write_size = min(len, info->size - CIRC_CNT(info->data.head, info->data.tail, info->size));
	size_t end_fragment = min(info->size - 1 - info->data.head, write_size);
	size_t begin_fragment = write_size - end_fragment;
	if (end_fragment > 0 && copy_from_user(info->data.buf + info->data.head, buf, end_fragment)) {
		mutex_unlock(&info->lock);
		return -EFAULT;
	}
	if (begin_fragment > 0 && copy_from_user(info->data.buf, buf, begin_fragment)) {
		mutex_unlock(&info->lock);
		return -EFAULT;
	}
	info->data.head += write_size;
	info->data.head %= info->size;
	info->empty = 0;
	wake_up(&read_wait_queue);
	mutex_unlock(&info->lock);
	return write_size;
}

static const struct file_operations pipebuf_fops = {
	.owner   = THIS_MODULE,
	.open    = pipebuf_open,
	.release = pipebuf_release,
	.read    = pipebuf_read,
	.write   = pipebuf_write,
};

static int create_pipebuf_device(int minor) {
	dev_t dev = MKDEV(MAJOR(module_dev), minor);
	devices[minor].dev = dev;
	devices[minor].data.buf = kzalloc(base_device_size, GFP_KERNEL);
	if (!devices[minor].data.buf) {
		return -ENOMEM;
	}
	devices[minor].data.head = 0;
	devices[minor].data.tail = 0;
	devices[minor].empty = 1;
	devices[minor].size = base_device_size;
	mutex_init(&devices[minor].lock);
	atomic_set(&devices[minor].opened_by, 0);
	devices[minor].creating = false;

	cdev_init(&devices[minor].cdev, &pipebuf_fops);
	devices[minor].cdev.owner = THIS_MODULE;

	int ret = cdev_add(&devices[minor].cdev, dev, 1);
	if (ret < 0) {
		goto free_buffer;
	}
	devices[minor].sdev = device_create(pipebuf_class, NULL, dev, &devices[minor], "pipebuf_dev%d", minor);
	if (IS_ERR(devices[minor].sdev)) {
		ret = PTR_ERR(devices[minor].sdev);
		goto delete_cdev;
	}
	ret = device_create_file(devices[minor].sdev, &dev_attr_size);
	if (ret < 0) {
		device_destroy(pipebuf_class, devices[minor].dev);
		goto delete_cdev;
	}
	pr_info("pipebuf: device %d:%d created", MAJOR(module_dev), minor);
	return 0;

delete_cdev:
	cdev_del(&devices[minor].cdev);
free_buffer:
	kfree(devices[minor].data.buf);
	return ret;
}

static void destroy_pipebuf_device(int minor) {
	if (!devices[minor].data.buf) {
		return;
	}
	device_destroy(pipebuf_class, devices[minor].dev);
	cdev_del(&devices[minor].cdev);
	kfree(devices[minor].data.buf);
	devices[minor].data.buf = NULL;
	devices[minor].creating = 0;
	pr_info("pipebuf: device #%d destroyed", minor);
}

static int __init pipebuf_init(void)
{
	if (devices_number <= 0 || devices_number > MAX_DEVICES) {
		pr_err("pipebuf: incorrect device number\n");
		return -EINVAL;
	}
	if (base_device_size <= 0) {
		pr_err("pipebuf: incorrect device size\n");
		return -EINVAL;
	}
	int ret;
    if ((ret = alloc_chrdev_region(&module_dev, 0, MAX_DEVICES, DEVICE_NAME)))
	{
		pr_err("pipebuf: failed to allocate region\n");
		return ret;
	}
	pipebuf_class = class_create(CLASS_NAME);
	if (IS_ERR(pipebuf_class)) {
		ret = PTR_ERR(pipebuf_class);
		pr_err("pipebuf: failed to create class\n");
		goto unregister_class;
	}

	int device_id = 0;
	for (; device_id < devices_number; ++device_id) {
		ret = create_pipebuf_device(device_id);
		if (ret < 0) {
			pr_err("pipebuf: failed to create device %d\n", device_id);
			goto delete_devices;
		}
	}
	for (int i = devices_number; i < MAX_DEVICES; ++i) {
		devices[i].data.buf = NULL;
		devices[i].creating = false;
	}
	pr_info("pipebuf: module loaded");
	return 0;

delete_devices:
	device_id--;
	while (device_id > 0) {
		destroy_pipebuf_device(device_id);
		device_id--;
	}
unregister_class:
	unregister_chrdev_region(module_dev, MAX_DEVICES);
	return ret;
}

static void __exit pipebuf_exit(void)
{
	for (int i = 0; i < MAX_DEVICES; ++i) {
		destroy_pipebuf_device(i);
	}

	class_destroy(pipebuf_class);
	unregister_chrdev_region(module_dev, MAX_DEVICES);
	pr_info("pipebuf: module unloaded\n");
}

module_init(pipebuf_init);
module_exit(pipebuf_exit);

