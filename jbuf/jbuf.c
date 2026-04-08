// SPDX-License-Identifier: GPL-2.0
/*
 * jbuf - A simple character device driver that acts as a kernel-space buffer.
 *
 * Userspace can write data into /dev/jbuf and read it back.
 * Think of it as a tiny RAM file managed entirely by your own kernel code.
 *
 * Concepts demonstrated:
 *   - Character device registration (alloc_chrdev_region, cdev)
 *   - Automatic /dev/ node creation (class_create, device_create)
 *   - file_operations: open, read, write, release
 *   - Safe kernel <-> userspace data transfer (copy_to_user, copy_from_user)
 *   - Concurrency protection with a mutex
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>          /* file_operations, register_chrdev_region */
#include <linux/cdev.h>        /* cdev_init, cdev_add */
#include <linux/device.h>      /* class_create, device_create */
#include <linux/uaccess.h>     /* copy_to_user, copy_from_user */
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesper");
MODULE_DESCRIPTION("Simple character device buffer driver");

#define DEVICE_NAME "jbuf"
#define BUF_SIZE    4096

/* ---- Device state ---- */
static dev_t         dev_num;       /* major/minor number pair */
static struct cdev   jbuf_cdev;     /* the character device struct */
static struct class *jbuf_class;    /* sysfs class (creates /dev node) */

static char          buffer[BUF_SIZE]; /* the kernel-side data buffer */
static size_t        data_len;         /* how many bytes are stored */
static DEFINE_MUTEX(jbuf_mutex);       /* protects buffer + data_len */

/* ---- file_operations callbacks ---- */

/*
 * open() - Called when userspace does open("/dev/jbuf", ...).
 * We don't need to do anything special here, but you could track
 * how many processes have the device open, for example.
 */
static int jbuf_open(struct inode *inode, struct file *filp)
{
	pr_info("jbuf: opened by pid %d\n", current->pid);
	return 0;
}

/*
 * release() - Called when the last fd to the device is closed.
 */
static int jbuf_release(struct inode *inode, struct file *filp)
{
	pr_info("jbuf: closed by pid %d\n", current->pid);
	return 0;
}

/*
 * read() - Called when userspace does read() or `cat /dev/jbuf`.
 *
 * Arguments:
 *   filp  - the open file (we don't use it here)
 *   ubuf  - pointer to USERSPACE memory where we need to copy data
 *   count - how many bytes userspace wants to read
 *   ppos  - pointer to the file position offset
 *
 * Returns: number of bytes actually read, or negative errno.
 *
 * IMPORTANT: We cannot just do `memcpy(ubuf, buffer, ...)` because ubuf
 * is a userspace pointer. The kernel can't dereference it directly (on most
 * architectures it would fault). copy_to_user() does it safely.
 */
static ssize_t jbuf_read(struct file *filp, char __user *ubuf,
			  size_t count, loff_t *ppos)
{
	ssize_t ret;

	mutex_lock(&jbuf_mutex);

	/* If the read position is at or past the end, there's nothing to read */
	if (*ppos >= data_len) {
		ret = 0;   /* returning 0 signals EOF to userspace */
		goto out;
	}

	/* Don't read past the end of our data */
	if (*ppos + count > data_len)
		count = data_len - *ppos;

	/* Copy from kernel buffer to userspace buffer */
	if (copy_to_user(ubuf, buffer + *ppos, count)) {
		ret = -EFAULT;   /* bad userspace pointer */
		goto out;
	}

	*ppos += count;   /* advance the file position */
	ret = count;

	pr_info("jbuf: read %zu bytes (pos now %lld)\n", count, *ppos);

out:
	mutex_unlock(&jbuf_mutex);
	return ret;
}

/*
 * write() - Called when userspace does write() or `echo "hi" > /dev/jbuf`.
 *
 * Each write REPLACES the buffer contents (keeps things simple).
 * A more advanced version could append instead.
 */
static ssize_t jbuf_write(struct file *filp, const char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	ssize_t ret;

	mutex_lock(&jbuf_mutex);

	if (count > BUF_SIZE) {
		pr_warn("jbuf: write too large (%zu > %d), truncating\n",
			count, BUF_SIZE);
		count = BUF_SIZE;
	}

	/* Copy from userspace buffer to kernel buffer */
	if (copy_from_user(buffer, ubuf, count)) {
		ret = -EFAULT;
		goto out;
	}

	data_len = count;
	*ppos = count;
	ret = count;

	pr_info("jbuf: wrote %zu bytes\n", count);

out:
	mutex_unlock(&jbuf_mutex);
	return ret;
}

/*
 * The file_operations struct is the vtable that tells the kernel
 * which functions to call for each syscall on our device.
 * Any operation not listed here returns -EINVAL to userspace.
 */
static const struct file_operations jbuf_fops = {
	.owner   = THIS_MODULE,
	.open    = jbuf_open,
	.release = jbuf_release,
	.read    = jbuf_read,
	.write   = jbuf_write,
};

/* ---- Module init/exit ---- */

static int __init jbuf_init(void)
{
	int ret;

	/*
	 * Step 1: Ask the kernel for a major/minor number.
	 * alloc_chrdev_region picks an unused major number for us
	 * (as opposed to register_chrdev_region where we'd hardcode one).
	 */
	ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
	if (ret < 0) {
		pr_err("jbuf: failed to alloc chrdev region: %d\n", ret);
		return ret;
	}
	pr_info("jbuf: registered with major %d, minor %d\n",
		MAJOR(dev_num), MINOR(dev_num));

	/*
	 * Step 2: Initialize and add our cdev to the kernel.
	 * This links our file_operations to the device number.
	 */
	cdev_init(&jbuf_cdev, &jbuf_fops);
	jbuf_cdev.owner = THIS_MODULE;

	ret = cdev_add(&jbuf_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("jbuf: cdev_add failed: %d\n", ret);
		goto err_cdev;
	}

	/*
	 * Step 3: Create a device class in sysfs.
	 * This is what makes udev (or devtmpfs) automatically create
	 * /dev/jbuf for us. Without this you'd have to mknod by hand.
	 */
	jbuf_class = class_create(DEVICE_NAME);
	if (IS_ERR(jbuf_class)) {
		ret = PTR_ERR(jbuf_class);
		pr_err("jbuf: class_create failed: %d\n", ret);
		goto err_class;
	}

	/*
	 * Step 4: Create the actual device node.
	 * After this call, /dev/jbuf exists and is usable.
	 */
	if (IS_ERR(device_create(jbuf_class, NULL, dev_num, NULL, DEVICE_NAME))) {
		ret = -ENOMEM;
		pr_err("jbuf: device_create failed\n");
		goto err_device;
	}

	/* Initialize buffer state */
	data_len = 0;
	memset(buffer, 0, BUF_SIZE);

	pr_info("jbuf: module loaded, /dev/%s is ready\n", DEVICE_NAME);
	return 0;

	/* Cleanup on error - note the reverse order (unwinding) */
err_device:
	class_destroy(jbuf_class);
err_class:
	cdev_del(&jbuf_cdev);
err_cdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit jbuf_exit(void)
{
	/* Tear down in reverse order of init */
	device_destroy(jbuf_class, dev_num);
	class_destroy(jbuf_class);
	cdev_del(&jbuf_cdev);
	unregister_chrdev_region(dev_num, 1);

	pr_info("jbuf: module unloaded\n");
}

module_init(jbuf_init);
module_exit(jbuf_exit);
