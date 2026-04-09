#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/device.h>      /* class_create, device_create */
#include <linux/types.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesper");
MODULE_DESCRIPTION("Simple virual framebuffer driver");

#define DEVICE_NAME "jfb"
#define WIDTH 640
#define HEIGHT 480

/* ---- Device state ---- */
static void* videomemory;
static u_long videomemorysize = (4 * WIDTH * HEIGHT);
static struct fb_info *info;


static struct fb_fix_screeninfo jfb_fix_si = {
    .id = "jfb",
    .type = FB_TYPE_PACKED_PIXELS,
    .visual = FB_VISUAL_TRUECOLOR,
    .line_length = WIDTH * 4,
    .accel = FB_ACCEL_NONE
};

static struct fb_var_screeninfo jfb_var_si = {
    .xres               = WIDTH,
    .yres               = HEIGHT,
    .xres_virtual       = WIDTH,
    .yres_virtual       = HEIGHT,
    .bits_per_pixel     = 32,
    .red                = { .offset = 16, .length = 8 },
    .green              = { .offset = 8,  .length = 8 },
    .blue               = { .offset = 0,  .length = 8 },
    .transp             = { .offset = 24, .length = 8 }
};

      
static int jfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

    return remap_vmalloc_range(vma, (void *)info->fix.smem_start, vma->vm_pgoff);
}


static const struct fb_ops jfb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_SYSMEM_OPS_RDWR,
	__FB_DEFAULT_SYSMEM_OPS_DRAW,
	.fb_mmap	= jfb_mmap,
};


static int __init jfb_init(void)
{
    int ret;

    videomemory = vmalloc(videomemorysize);
    if (!videomemory)
        return -ENOMEM;

    jfb_fix_si.smem_start = (unsigned long)videomemory;
    jfb_fix_si.smem_len = videomemorysize;

    info = framebuffer_alloc(sizeof(u32) * 16, NULL);
    if (!info) {
        ret = -ENOMEM;
        goto err_vfree;
    }

    info->screen_buffer = videomemory;
    info->fbops = &jfb_ops;
    info->fix = jfb_fix_si;
    info->var =jfb_var_si;
    info->pseudo_palette = info->par;
    info->par = NULL;
    info->flags = FBINFO_VIRTFB;

    ret = register_framebuffer(info);
    if (ret < 0)
        goto err_release;

    
    pr_info("jfb: loaded, /dev/fb%d\n", info->node);
    return 0;

err_release:
    framebuffer_release(info);

err_vfree:
    vfree(videomemory);
    return ret;
}

static void __exit jfb_exit(void)
{

    unregister_framebuffer(info);
    framebuffer_release(info);
    vfree(videomemory);
    pr_info("jfb: unloaded");

}

module_init(jfb_init);
module_exit(jfb_exit);