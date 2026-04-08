# jbuf — Your First Character Device Driver

## What this is

A kernel module that creates `/dev/jbuf` — a 4KB RAM buffer you can write to and read from using normal shell commands. It's the simplest useful character device you can build.

## Copy to your VM

SCP both files to your VM:

```bash
scp jbuf.c Makefile jesper@<vm-ip>:~/kernel_modules/
```

Or if you set up the 9p shared folder, just drop them there.

## Build, load, test

```bash
cd ~/kernel_modules
make
sudo insmod jbuf.ko
dmesg | tail -5
```

You should see the registration message with a major/minor number, and `/dev/jbuf` should now exist:

```bash
ls -la /dev/jbuf
```

Now try it:

```bash
# Write something
echo "hello from userspace" | sudo tee /dev/jbuf

# Read it back
sudo cat /dev/jbuf

# Check the kernel log to see the open/read/write/close calls
dmesg | tail -20
```

Note: `sudo` is needed because the default permissions on the device node are root-only. You could fix this by adding a udev rule, but that's a topic for later.

## What to look at in the code

Read `jbuf.c` in this order:

1. **`jbuf_fops`** (line ~130) — The vtable. This is the struct that maps syscalls to your functions. Every character device driver has one of these.

2. **`jbuf_init`** (line ~140) — The four-step registration dance:
   - `alloc_chrdev_region` — get a major/minor number
   - `cdev_init` + `cdev_add` — link your fops to that number
   - `class_create` — tell sysfs about your device class
   - `device_create` — make udev create `/dev/jbuf`

3. **`jbuf_read` / `jbuf_write`** — The actual logic. Notice `copy_to_user` and `copy_from_user` — you'll use these everywhere in driver code.

4. **Error handling in init** — The `goto err_*` pattern. This is idiomatic kernel C. Resources are cleaned up in reverse order of allocation, like stack unwinding.

5. **`jbuf_exit`** — Teardown in exact reverse order of init. Always.

## Experiments to try

After you've got it working and have read through the code:

- **Open from two terminals at once** — `cat /dev/jbuf` in one, `echo` in the other. Watch dmesg to see the PIDs. The mutex prevents them from corrupting the buffer.
- **Write more than 4096 bytes** — `dd if=/dev/urandom bs=8192 count=1 | sudo tee /dev/jbuf > /dev/null` — watch it truncate and log a warning.
- **Read with `dd`** — `sudo dd if=/dev/jbuf bs=1 count=5` to read exactly 5 bytes. Watch how `*ppos` advances.
- **Check the sysfs entry** — `ls /sys/class/jbuf/` to see what `class_create` made.
- **Try removing the module while a process has it open** — open it with `sleep 999 < /dev/jbuf &`, then `sudo rmmod jbuf`. What happens?

## Unload

```bash
sudo rmmod jbuf
dmesg | tail -3
```

## Next steps

Once this feels solid, good extensions:

- **Add an ioctl** — e.g. a `JBUF_CLEAR` command that zeroes the buffer, or `JBUF_GET_LEN` that returns the current data length. This teaches you the ioctl interface.
- **Add llseek** — so `dd` with `skip=` works properly.
- **Make write append** instead of replace — then you've got a simple kernel-space log.
- **Add a proc entry** — expose buffer stats via `/proc/jbuf_info`.

All of these build toward the framebuffer driver, which is fundamentally the same pattern but with `fb_ops` instead of `file_operations` and `mmap` for direct memory mapping.
