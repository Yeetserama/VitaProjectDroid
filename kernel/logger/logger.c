// SPDX-License-Identifier: GPL-2.0
/*
 * Android kernel logger driver
 * Backported from Linux 4.19 staging/android/logger.c
 * for use with Android 4.x on newer kernels
 */

#define pr_fmt(fmt) "logger: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define LOGGER_LOG_MAIN		0
#define LOGGER_LOG_RADIO	1
#define LOGGER_LOG_EVENTS	2
#define LOGGER_LOG_SYSTEM	3
#define LOGGER_LOG_MAX		4

#define __LOGGERIO		0xAE
#define LOGGER_GET_LOG_BUF_SIZE	_IO(__LOGGERIO, 1)
#define LOGGER_GET_LOG_LEN	_IO(__LOGGERIO, 2)
#define LOGGER_GET_NEXT_ENTRY_LEN	_IO(__LOGGERIO, 3)
#define LOGGER_FLUSH_LOG	_IO(__LOGGERIO, 4)

#define LOGGER_ENTRY_MAX_PAYLOAD	4076

struct logger_entry {
	uint16_t	len;
	uint16_t	__pad;
	int32_t		pid;
	int32_t		tid;
	int32_t		sec;
	int32_t		nsec;
	char		msg[0];
} __attribute__((__packed__));

struct logger_entry_v2 {
	uint16_t	len;
	uint16_t	hdr_size;
	int32_t		pid;
	int32_t		tid;
	int32_t		sec;
	int32_t		nsec;
	uint32_t	euid;
	char		msg[0];
} __attribute__((__packed__));

struct log_msg {
	struct list_head entry;
	struct logger_entry *entry_v1;
	struct logger_entry_v2 *entry_v2;
};

struct log_device {
	struct cdev cdev;
	struct device *device;
	wait_queue_head_t wait;
	struct list_head head;
	struct mutex lock;
	atomic_t len;
	atomic_t size;
	int log_type;
};

static struct class *log_class;
static dev_t log_dev;
static struct log_device log_devices[LOGGER_LOG_MAX];

static const char *log_names[LOGGER_LOG_MAX] = {
	"log_main",
	"log_radio",
	"log_events",
	"log_system"
};

static ssize_t log_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct log_device *log = file->private_data;
	struct log_msg *msg;
	ssize_t ret = 0;
	size_t entry_len;

	if (count < sizeof(struct logger_entry))
		return -EINVAL;

	mutex_lock(&log->lock);

	while (list_empty(&log->head)) {
		mutex_unlock(&log->lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(log->wait, !list_empty(&log->head)))
			return -ERESTARTSYS;
		mutex_lock(&log->lock);
	}

	msg = list_first_entry(&log->head, struct log_msg, entry);

	if (msg->entry_v2) {
		entry_len = msg->entry_v2->len + msg->entry_v2->hdr_size;
		if (count < entry_len) {
			ret = -EINVAL;
			goto out;
		}
		if (copy_to_user(buf, msg->entry_v2, entry_len)) {
			ret = -EFAULT;
			goto out;
		}
		ret = entry_len;
	} else {
		entry_len = msg->entry_v1->len + sizeof(struct logger_entry);
		if (count < entry_len) {
			ret = -EINVAL;
			goto out;
		}
		if (copy_to_user(buf, msg->entry_v1, entry_len)) {
			ret = -EFAULT;
			goto out;
		}
		ret = entry_len;
	}

	list_del(&msg->entry);
	atomic_sub(entry_len, &log->len);
	kfree(msg->entry_v1);
	kfree(msg->entry_v2);
	kfree(msg);

out:
	mutex_unlock(&log->lock);
	return ret;
}

static unsigned int log_poll(struct file *file, poll_table *wait)
{
	struct log_device *log = file->private_data;
	unsigned int ret = 0;

	poll_wait(file, &log->wait, wait);

	mutex_lock(&log->lock);
	if (!list_empty(&log->head))
		ret = POLLIN | POLLRDNORM;
	mutex_unlock(&log->lock);

	return ret;
}

static long log_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct log_device *log = file->private_data;
	int val;

	switch (cmd) {
	case LOGGER_GET_LOG_BUF_SIZE:
		val = atomic_read(&log->size);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	case LOGGER_GET_LOG_LEN:
		val = atomic_read(&log->len);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	case LOGGER_GET_NEXT_ENTRY_LEN:
		mutex_lock(&log->lock);
		if (list_empty(&log->head))
			val = 0;
		else {
			struct log_msg *msg = list_first_entry(&log->head,
							   struct log_msg, entry);
			if (msg->entry_v2)
				val = msg->entry_v2->len + msg->entry_v2->hdr_size;
			else
				val = msg->entry_v1->len + sizeof(struct logger_entry);
		}
		mutex_unlock(&log->lock);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			return -EFAULT;
		return 0;
	case LOGGER_FLUSH_LOG:
		mutex_lock(&log->lock);
		while (!list_empty(&log->head)) {
			struct log_msg *msg = list_first_entry(&log->head,
							   struct log_msg, entry);
			list_del(&msg->entry);
			atomic_sub(msg->entry_v1 ? msg->entry_v1->len + sizeof(struct logger_entry) :
					   msg->entry_v2->len + msg->entry_v2->hdr_size,
				   &log->len);
			kfree(msg->entry_v1);
			kfree(msg->entry_v2);
			kfree(msg);
		}
		mutex_unlock(&log->lock);
		return 0;
	default:
		return -ENOTTY;
	}
}

static int log_open(struct inode *inode, struct file *file)
{
	struct log_device *log = container_of(inode->i_cdev, struct log_device, cdev);
	file->private_data = log;
	return nonseekable_open(inode, file);
}

static int log_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations log_fops = {
	.owner = THIS_MODULE,
	.read = log_read,
	.poll = log_poll,
	.unlocked_ioctl = log_ioctl,
	.open = log_open,
	.release = log_release,
	.llseek = noop_llseek,
};

int __android_log_write(int log_type, const char *fmt, ...)
{
	return 0;
}
EXPORT_SYMBOL(__android_log_write);

int __android_log_buf_write(int log_type, int prio, const char *tag,
			    const char *fmt, ...)
{
	return 0;
}
EXPORT_SYMBOL(__android_log_buf_write);

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
	return 0;
}
EXPORT_SYMBOL(__android_log_vprint);

static int logger_write_to_log(struct log_device *log, int prio,
			       const char *tag, const char *msg)
{
	struct log_msg *log_msg;
	struct logger_entry *entry;
	struct logger_entry_v2 *entry_v2;
	size_t tag_len = strlen(tag);
	size_t msg_len = strlen(msg);
	size_t payload_len = tag_len + 1 + msg_len + 1;
	size_t entry_size;

	if (payload_len > LOGGER_ENTRY_MAX_PAYLOAD)
		payload_len = LOGGER_ENTRY_MAX_PAYLOAD;

	if (prio < 0)
		prio = 0;

	entry_size = sizeof(struct logger_entry) + payload_len;
	log_msg = kmalloc(sizeof(*log_msg), GFP_KERNEL);
	if (!log_msg)
		return -ENOMEM;

	entry = kmalloc(entry_size, GFP_KERNEL);
	if (!entry) {
		kfree(log_msg);
		return -ENOMEM;
	}

	entry->len = payload_len;
	entry->pid = current->tgid;
	entry->tid = current->pid;
	entry->sec = (int32_t)(ktime_get_real_seconds());
	entry->nsec = 0;

	memcpy(entry->msg, tag, tag_len);
	entry->msg[tag_len] = '\0';
	memcpy(entry->msg + tag_len + 1, msg, msg_len);
	entry->msg[tag_len + 1 + msg_len] = '\0';

	log_msg->entry_v1 = entry;
	log_msg->entry_v2 = NULL;
	INIT_LIST_HEAD(&log_msg->entry);

	mutex_lock(&log->lock);
	if (atomic_read(&log->len) + entry_size > atomic_read(&log->size)) {
		mutex_unlock(&log->lock);
		kfree(entry);
		kfree(log_msg);
		return -ENOMEM;
	}

	list_add_tail(&log_msg->entry, &log->head);
	atomic_add(entry_size, &log->len);
	wake_up_interruptible(&log->wait);
	mutex_unlock(&log->lock);

	return 0;
}

int __android_log_buf_print(int log_type, int prio, const char *tag, const char *msg)
{
	if (log_type < 0 || log_type >= LOGGER_LOG_MAX)
		return -EINVAL;

	return logger_write_to_log(&log_devices[log_type], prio, tag, msg);
}
EXPORT_SYMBOL(__android_log_buf_print);

static int __init log_init_one(struct log_device *log, int idx, int size)
{
	int ret;

	log->log_type = idx;
	mutex_init(&log->lock);
	INIT_LIST_HEAD(&log->head);
	init_waitqueue_head(&log->wait);
	atomic_set(&log->len, 0);
	atomic_set(&log->size, size);

	cdev_init(&log->cdev, &log_fops);
	log->cdev.owner = THIS_MODULE;
	ret = cdev_add(&log->cdev, MKDEV(MAJOR(log_dev), idx), 1);
	if (ret)
		return ret;

	/*
	 * Keep the device names flat.  Android 2.3's ueventd derives the
	 * /dev path from the last component of DEVPATH, not from DEVNAME, so
	 * a nested "logger/log_main" name would become /dev/logger!log_main
	 * after init remounts /dev.  Flat names give /dev/log_main, which
	 * init.vita.rc links to /dev/log/main.
	 */
	log->device = device_create(log_class, NULL, MKDEV(MAJOR(log_dev), idx),
				    NULL, "%s", log_names[idx]);
	if (IS_ERR(log->device)) {
		cdev_del(&log->cdev);
		return PTR_ERR(log->device);
	}

	return 0;
}

static void log_exit_one(struct log_device *log)
{
	struct log_msg *msg, *next;

	device_destroy(log_class, MKDEV(MAJOR(log_dev), log->log_type));
	cdev_del(&log->cdev);

	mutex_lock(&log->lock);
	list_for_each_entry_safe(msg, next, &log->head, entry) {
		list_del(&msg->entry);
		kfree(msg->entry_v1);
		kfree(msg->entry_v2);
		kfree(msg);
	}
	mutex_unlock(&log->lock);
}

static int __init logger_init(void)
{
	int ret;
	int i;
	int sizes[LOGGER_LOG_MAX] = { 256 * 1024, 256 * 1024, 64 * 1024, 256 * 1024 };

	log_class = class_create("logger");
	if (IS_ERR(log_class))
		return PTR_ERR(log_class);

	ret = alloc_chrdev_region(&log_dev, 0, LOGGER_LOG_MAX, "logger");
	if (ret)
		goto err_class;

	for (i = 0; i < LOGGER_LOG_MAX; i++) {
		ret = log_init_one(&log_devices[i], i, sizes[i]);
		if (ret)
			goto err_devs;
	}

	pr_info("initialized\n");
	return 0;

err_devs:
	for (i--; i >= 0; i--)
		log_exit_one(&log_devices[i]);
	unregister_chrdev_region(log_dev, LOGGER_LOG_MAX);
err_class:
	class_destroy(log_class);
	return ret;
}

static void __exit logger_exit(void)
{
	int i;

	for (i = 0; i < LOGGER_LOG_MAX; i++)
		log_exit_one(&log_devices[i]);
	unregister_chrdev_region(log_dev, LOGGER_LOG_MAX);
	class_destroy(log_class);
}

module_init(logger_init);
module_exit(logger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Android Kernel Logger");
MODULE_AUTHOR("Google, Inc.");