// SPDX-License-Identifier: GPL-2.0
/*
 * Android lowmemorykiller driver
 * Backported from Linux 4.19 staging/android/lowmemorykiller.c
 * for use with Android 4.x on newer kernels
 */

#define pr_fmt(fmt) "lowmemorykiller: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/vmstat.h>

#define LOWMEM_DEBUG 0

struct lowmem_adj {
	int adj;
	int minfree;
};

static struct lowmem_adj *lowmem_adj;
static int lowmem_adj_count;
static int lowmem_minfree_count;
static int *lowmem_minfree;
static DEFINE_MUTEX(lowmem_adj_mutex);
static struct cdev lowmem_cdev;
static dev_t lowmem_dev;
static struct class *lowmem_class;
static struct device *lowmem_device;

/*
 * This driver is a compatibility stub: Android 4.x and earlier write the
 * lowmemorykiller tunables and expect the driver to exist.  The old staging
 * shrinker used register_shrinker(), which Linux 6.12 replaced with the
 * shrinker_alloc()/shrinker_register() API.  Registering an empty shrinker
 * has no effect, so it is intentionally not registered here.
 */

static int lowmem_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	char *kbuf;
	int ret = 0;
	int i;
	int *new_minfree;
	struct lowmem_adj *new_adj;
	int new_count;

	if (count == 0 || count > PAGE_SIZE)
		return -EINVAL;

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		ret = -EFAULT;
		goto out;
	}
	kbuf[count] = '\0';

	new_count = 0;
	for (i = 0; i < count; i++) {
		if (kbuf[i] == ',')
			new_count++;
	}
	new_count++;

	new_minfree = kmalloc_array(new_count, sizeof(int), GFP_KERNEL);
	if (!new_minfree) {
		ret = -ENOMEM;
		goto out;
	}

	new_adj = kmalloc_array(new_count, sizeof(struct lowmem_adj), GFP_KERNEL);
	if (!new_adj) {
		kfree(new_minfree);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < new_count; i++) {
		char *end;
		long val = simple_strtol(kbuf, &end, 10);
		if (end == kbuf || val < 0 || val > INT_MAX) {
			ret = -EINVAL;
			kfree(new_minfree);
			kfree(new_adj);
			goto out;
		}
		new_minfree[i] = (int)val;
		kbuf = end + 1;
	}

	mutex_lock(&lowmem_adj_mutex);
	if (lowmem_adj) {
		kfree(lowmem_adj);
		kfree(lowmem_minfree);
	}
	lowmem_adj = new_adj;
	lowmem_minfree = new_minfree;
	lowmem_adj_count = new_count;
	lowmem_minfree_count = new_count;

	for (i = 0; i < lowmem_adj_count; i++) {
		lowmem_adj[i].minfree = lowmem_minfree[i];
		lowmem_adj[i].adj = i * 100;
	}
	mutex_unlock(&lowmem_adj_mutex);

out:
	kfree(kbuf);
	return ret ? ret : count;
}

static int lowmem_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations lowmem_fops = {
	.owner = THIS_MODULE,
	.write = lowmem_write,
	.open = lowmem_open,
	.llseek = noop_llseek,
};

static int __init lowmemorykiller_init(void)
{
	int ret;
	int i;
	int default_minfree[] = { 6 * 1024, 8 * 1024, 16 * 1024, 24 * 1024, 32 * 1024, 40 * 1024 };

	lowmem_adj_count = ARRAY_SIZE(default_minfree);
	lowmem_minfree_count = ARRAY_SIZE(default_minfree);

	lowmem_adj = kmalloc_array(lowmem_adj_count, sizeof(struct lowmem_adj), GFP_KERNEL);
	if (!lowmem_adj)
		return -ENOMEM;

	lowmem_minfree = kmalloc_array(lowmem_minfree_count, sizeof(int), GFP_KERNEL);
	if (!lowmem_minfree) {
		kfree(lowmem_adj);
		return -ENOMEM;
	}

	for (i = 0; i < lowmem_adj_count; i++) {
		lowmem_adj[i].minfree = default_minfree[i];
		lowmem_adj[i].adj = i * 100;
		lowmem_minfree[i] = default_minfree[i];
	}

	lowmem_class = class_create("lowmemorykiller");
	if (IS_ERR(lowmem_class)) {
		ret = PTR_ERR(lowmem_class);
		goto err_free;
	}

	ret = alloc_chrdev_region(&lowmem_dev, 0, 1, "lowmemorykiller");
	if (ret)
		goto err_class;

	cdev_init(&lowmem_cdev, &lowmem_fops);
	lowmem_cdev.owner = THIS_MODULE;
	ret = cdev_add(&lowmem_cdev, lowmem_dev, 1);
	if (ret)
		goto err_chrdev;

	lowmem_device = device_create(lowmem_class, NULL, lowmem_dev, NULL, "lowmemorykiller");
	if (IS_ERR(lowmem_device)) {
		ret = PTR_ERR(lowmem_device);
		goto err_cdev;
	}

	pr_info("initialized with %d adj slots\n", lowmem_adj_count);
	return 0;

err_cdev:
	cdev_del(&lowmem_cdev);
err_chrdev:
	unregister_chrdev_region(lowmem_dev, 1);
err_class:
	class_destroy(lowmem_class);
err_free:
	kfree(lowmem_adj);
	kfree(lowmem_minfree);
	return ret;
}

static void __exit lowmemorykiller_exit(void)
{
	device_destroy(lowmem_class, lowmem_dev);
	cdev_del(&lowmem_cdev);
	unregister_chrdev_region(lowmem_dev, 1);
	class_destroy(lowmem_class);
	kfree(lowmem_adj);
	kfree(lowmem_minfree);
}

module_init(lowmemorykiller_init);
module_exit(lowmemorykiller_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Android Low Memory Killer");
MODULE_AUTHOR("Google, Inc.");