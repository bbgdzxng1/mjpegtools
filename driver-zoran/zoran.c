#define MAX_KMALLOC_MEM (128*1024)
/*
   Miro/Pinnacle Systems Inc. DC10/DC10plus and
   Linux Media Labs LML33 video capture boards driver
   now with IOMega BUZ support!
   
   Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>

   Changes for BUZ by Wolfgang Scherr <scherr@net4you.net>

   Changes for DC10 by Laurent Pinchart <laurent.pinchart@skynet.be>
   
   Changes for videodev2/v4l2 by Ronald Bultje <rbultje@ronald.bitfreak.net>

   Based on
   
    Miro DC10 driver
    Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
   
    Iomega Buz driver version 1.0
    Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>

    buz.0.0.3
    Copyright (C) 1998 Dave Perks <dperks@ibm.net>

    bttv - Bt848 frame grabber driver
    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)

    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// #include <linux/config.h>
// 
// #ifndef EXPORT_SYMTAB
// #define EXPORT_SYMTAB
// #endif

#include <linux/version.h>

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/wrapper.h>

#include <linux/i2c-algo-bit.h>

/* temp hack */
#ifndef I2C_DRIVERID_ADV717X
#warning Using temporary hack for missing I2C driver-ID for adv717x
#define I2C_DRIVERID_ADV717X 48 /* same as in 2.5.x */
#endif
/* /temp hack */

#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#define     MAP_NR(x)       virt_to_page(x)
#define     ZORAN_HARDWARE  VID_HARDWARE_ZR36067
#define     ZORAN_VID_TYPE  ( \
                            VID_TYPE_CAPTURE | \
                            VID_TYPE_OVERLAY | \
                            VID_TYPE_CLIPPING | \
                            VID_TYPE_FRAMERAM | \
                            VID_TYPE_SCALES | \
                            VID_TYPE_MJPEG_DECODER | \
                            VID_TYPE_MJPEG_ENCODER \
                            )

#include <linux/videodev.h>
#include "videocodec.h"

#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include <linux/video_decoder.h>
#include <linux/video_encoder.h>
#include "zoran.h"

#ifdef HAVE_V4L2
	/* we declare some card type definitions here, they mean
	   the same as the v4l1 ZORAN_VID_TYPE above, except it's v4l2 */
#define ZORAN_V4L2_VID_FLAGS ( \
				V4L2_CAP_STREAMING |\
				V4L2_CAP_VIDEO_CAPTURE |\
				V4L2_CAP_VIDEO_OUTPUT |\
				V4L2_CAP_VIDEO_OVERLAY \
                             )
#endif

#include <asm/byteorder.h>

static const struct zoran_format zoran_formats[] = {
	{
		name: "15-bit RGB",
		palette: VIDEO_PALETTE_RGB555,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		fourcc: V4L2_PIX_FMT_RGB555,
#else
		fourcc: V4L2_PIX_FMT_RGB555X,
#endif
		colorspace: V4L2_COLORSPACE_SRGB,
#endif
		depth: 15,
		flags: ZORAN_FORMAT_CAPTURE |
		       ZORAN_FORMAT_OVERLAY,
	},{
		name: "16-bit RGB",
		palette: VIDEO_PALETTE_RGB565,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		fourcc: V4L2_PIX_FMT_RGB565,
#else
		fourcc: V4L2_PIX_FMT_RGB565X,
#endif
		colorspace: V4L2_COLORSPACE_SRGB,
#endif
		depth: 16,
		flags: ZORAN_FORMAT_CAPTURE |
		       ZORAN_FORMAT_OVERLAY,
	},{
		name: "24-bit RGB",
		palette: VIDEO_PALETTE_RGB24,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		fourcc: V4L2_PIX_FMT_BGR24,
#else
		fourcc: V4L2_PIX_FMT_RGB24,
#endif
		colorspace: V4L2_COLORSPACE_SRGB,
#endif
		depth: 24,
		flags: ZORAN_FORMAT_CAPTURE |
		       ZORAN_FORMAT_OVERLAY,
	},{
		name: "32-bit RGB",
		palette: VIDEO_PALETTE_RGB32,
#ifdef HAVE_V4L2
#ifdef __LITTLE_ENDIAN
		fourcc: V4L2_PIX_FMT_BGR32,
#else
		fourcc: V4L2_PIX_FMT_RGB32,
#endif
		colorspace: V4L2_COLORSPACE_SRGB,
#endif
		depth: 32,
		flags: ZORAN_FORMAT_CAPTURE |
		       ZORAN_FORMAT_OVERLAY,
	},{
		name: "4:2:2, packed, YUYV",
		palette: VIDEO_PALETTE_YUV422,
#ifdef HAVE_V4L2
		fourcc: V4L2_PIX_FMT_YUYV,
		colorspace: V4L2_COLORSPACE_SMPTE170M,
#endif
		depth: 16,
		flags: ZORAN_FORMAT_CAPTURE,
	},{
		name: "Hardware-encoded Motion-JPEG",
		palette: -1,
#ifdef HAVE_V4L2
		fourcc: V4L2_PIX_FMT_MJPEG,
		colorspace: V4L2_COLORSPACE_SMPTE170M,
#endif
		depth: 0,
		flags: ZORAN_FORMAT_CAPTURE |
		       ZORAN_FORMAT_PLAYBACK |
		       ZORAN_FORMAT_COMPRESSED,
	}
};
static const int ZORAN_FORMATS = (sizeof(zoran_formats)/sizeof(struct zoran_format));

// RJ: Test only - want to test BUZ_USE_HIMEM even when CONFIG_BIGPHYS_AREA is defined
#if !defined(CONFIG_BIGPHYS_AREA)
//#undef CONFIG_BIGPHYS_AREA
#define BUZ_USE_HIMEM
#endif

#if defined(CONFIG_BIGPHYS_AREA)
#   include <linux/bigphysarea.h>
#endif

#define IRQ_MASK ( ZR36057_ISR_GIRQ0 | ZR36057_ISR_GIRQ1 | ZR36057_ISR_JPEGRepIRQ )

#define MAJOR_VERSION 0		/* driver major version */
#define MINOR_VERSION 9		/* driver minor version */
#define RELEASE_VERSION 1	/* release version */

#define ZORAN_NAME    "ZORAN"	/* name of the device */

static int __init debug = 0;

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
		       printk(format, ##args);	\
	} while (0)

/* The parameters for this driver */

/*
   The video mem address of the video card.
   The driver has a little database for some videocards
   to determine it from there. If your video card is not in there
   you have either to give it to the driver as a parameter
   or set in in a VIDIOCSFBUF ioctl
 */

static unsigned long vidmem = 0;	/* Video memory base address */

/* Special purposes only: */

static int triton = 0;		/* 0=no, 1=yes */
static int natoma = 0;		/* 0=no, 1=yes */

/*
   Number and size of grab buffers for Video 4 Linux
   The vast majority of applications should not need more than 2,
   the very popular BTTV driver actually does ONLY have 2.
   Time sensitive applications might need more, the maximum
   is VIDEO_MAX_FRAME (defined in <linux/videodev.h>).

   The size is set so that the maximum possible request
   can be satisfied. Decrease  it, if bigphys_area alloc'd
   memory is low. If you don't have the bigphys_area patch,
   set it to 128 KB. Will you allow only to grab small
   images with V4L, but that's better than nothing.

   v4l_bufsize has to be given in KB !

*/

static int v4l_nbufs = 2;
static int v4l_bufsize = 128;	/* Everybody should be able to work with this setting */

/*
   Default input and video norm at startup of the driver.
*/

static int default_input = 0;	/* 0=Composite, 1=S-VHS */
static int default_norm = 0;	/* 0=PAL, 1=NTSC 2=SECAM */
static int lock_norm = 0;	/* 1=Don't change TV standard (norm) */

static int pass_through = 0;	/* 1=Pass through TV signal when device is not used */
                                /* 0=Show color bar when device is not used (LML33: only if lml33dpath=1) */

static int lml33dpath = 0;	/* 1 will use digital path in capture mode instead of analog.
                                   It can be used for picture adjustments using tool like xawtv
                                   while watching image on TV monitor connected to the output.
                                   However, due to absence of 75 Ohm load on Bt819 input, there
                                   will be some image imperfections */

MODULE_PARM(vidmem, "i");
MODULE_PARM(triton, "i");
MODULE_PARM(natoma, "i");
MODULE_PARM(v4l_nbufs, "i");
MODULE_PARM_DESC(v4l_nbufs, "Number of V4L buffers to use");
MODULE_PARM(v4l_bufsize, "i");
MODULE_PARM_DESC(v4l_bufsize, "Size per V4L buffer (in kB)");
MODULE_PARM(default_input, "i");
MODULE_PARM_DESC(default_input, "Default input (0=Composite, 1=S-Video, 2=Internal)");
MODULE_PARM(default_norm, "i");
MODULE_PARM_DESC(default_norm, "Default norm (0=PAL, 1=NTSC, 2=SECAM)");
MODULE_PARM(lock_norm, "i");
MODULE_PARM_DESC(lock_norm, "Users can't change norm");
MODULE_PARM(pass_through, "i");
MODULE_PARM_DESC(pass_through, "Pass TV signal through to TV-out when idling");
MODULE_PARM(lml33dpath, "i");
MODULE_PARM_DESC(lml33dpath, "Use digital path capture mode (on LML33 cards)");
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "debug level");

MODULE_DESCRIPTION("Zoran-36057/36067 JPEG codec driver");
MODULE_AUTHOR("Serguei Miridonov");
MODULE_LICENSE("GPL");

/* Anybody who uses more than four? */
#define BUZ_MAX 4

static int zoran_num;		/* number of Buzs in use */
static struct zoran zoran[BUZ_MAX];

#ifdef HAVE_V4L2
	/* small helper function for calculating buffersizes for v4l2
	 * we calculate the nearest higher power-of-two, which
	 * will be the recommended buffersize */
static __u32
zoran_v4l2_calc_bufsize (struct zoran_jpg_settings *settings)
{
	__u8 div = settings->VerDcm * settings->HorDcm * settings->TmpDcm;
	__u32 num = (1024 * 512) / (div);
	__u32 result = 2;

	num--;
	while (num) {
		num >>= 1;
		result <<= 1;
	}

	return result;
}
#endif

/* forward references */

static void v4l_fbuffer_free(struct file *file);
static void jpg_fbuffer_free(struct file *file);
static void zoran_feed_stat_com(struct zoran *zr);

/*
 *   Allocate the V4L grab buffers
 *
 *   These have to be pysically contiguous.
 *   If v4l_bufsize <= MAX_KMALLOC_MEM we use kmalloc
 *   else we try to allocate them with bigphysarea_alloc_pages
 *   if the bigphysarea patch is present in the kernel,
 *   else we try to use high memory (if the user has bootet
 *   Linux with the necessary memory left over).
 */

#if defined(BUZ_USE_HIMEM) && !defined(CONFIG_BIGPHYS_AREA)
static unsigned long get_high_mem(unsigned long size)
{
/*
 * Check if there is usable memory at the end of Linux memory
 * of at least size. Return the physical address of this memory,
 * return 0 on failure.
 *
 * The idea is from Alexandro Rubini's book "Linux device drivers".
 * The driver from him which is downloadable from O'Reilly's
 * web site misses the "virt_to_phys(high_memory)" part
 * (and therefore doesn't work at all - at least with 2.2.x kernels).
 *
 * It should be unnecessary to mention that THIS IS DANGEROUS,
 * if more than one driver at a time has the idea to use this memory!!!!
 */

	volatile unsigned char *mem;
	unsigned char c;
	unsigned long hi_mem_ph;
	unsigned long i;

	/* Map the high memory to user space */

	hi_mem_ph = virt_to_phys(high_memory);

	mem = ioremap(hi_mem_ph, size);
	if (!mem) {
		dprintk(0, KERN_ERR "MJPEG: get_high_mem: ioremap failed\n");
		return 0;
	}

	for (i = 0; i < size; i++) {
		/* Check if it is memory */
		c = i & 0xff;
		mem[i] = c;
		if (mem[i] != c)
			break;
		c = 255 - c;
		mem[i] = c;
		if (mem[i] != c)
			break;
		mem[i] = 0;	/* zero out memory */

		/* give the kernel air to breath */
		if ((i & 0x3ffff) == 0x3ffff)
			schedule();
	}

	iounmap((void *) mem);

	if (i != size) {
		dprintk(0, KERN_ERR "MJPEG: get_high_mem - requested %lu, avail %lu\n", size, i);
		return 0;
	}

	return hi_mem_ph;
}
#endif

static int
v4l_fbuffer_alloc (struct file *file)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;
#if defined(BUZ_USE_HIMEM) && !defined(CONFIG_BIGPHYS_AREA)
	unsigned long pmem = 0;
#endif

	for (i = 0; i < fh->v4l_buffers.num_buffers; i++) {
		if (fh->v4l_buffers.buffer[i].fbuffer)
			dprintk(0, KERN_WARNING "%s: v4l_fbuffer_alloc: buffer %d allready allocated ???\n", zr->name, i);

		//udelay(20);
		if (fh->v4l_buffers.buffer_size <= MAX_KMALLOC_MEM) {
			/* Use kmalloc */

			mem = (unsigned char *) kmalloc(fh->v4l_buffers.buffer_size, GFP_KERNEL);
			if (mem == 0) {
				dprintk(0, KERN_ERR "%s: kmalloc for V4L bufs failed\n", zr->name);
				v4l_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->v4l_buffers.buffer[i].fbuffer = mem;
			fh->v4l_buffers.buffer[i].fbuffer_phys = virt_to_phys(mem);
			fh->v4l_buffers.buffer[i].fbuffer_bus = virt_to_bus(mem);
			for (off = 0; off < fh->v4l_buffers.buffer_size; off += PAGE_SIZE)
				mem_map_reserve(MAP_NR(mem + off));
			dprintk(1, KERN_INFO "%s: V4L frame %d mem 0x%lx (bus: 0x%lx)\n", zr->name,
				      i, (unsigned long) mem, virt_to_bus(mem));
		} else {
#if defined(CONFIG_BIGPHYS_AREA)
			/* Use bigphysarea_alloc_pages */

			int n = (fh->v4l_buffers.buffer_size + PAGE_SIZE - 1) / PAGE_SIZE;
			mem = (unsigned char *) bigphysarea_alloc_pages(n, 0, GFP_KERNEL);
			if (mem == 0) {
				dprintk(0, KERN_ERR "%s: bigphysarea_alloc_pages for V4L bufs failed\n", zr->name);
				v4l_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->v4l_buffers.buffer[i].fbuffer = mem;
			fh->v4l_buffers.buffer[i].fbuffer_phys = virt_to_phys(mem);
			fh->v4l_buffers.buffer[i].fbuffer_bus = virt_to_bus(mem);
			dprintk(1, KERN_INFO "%s: Bigphysarea frame %d mem 0x%x (bus: 0x%x)\n", zr->name,
				      i, (unsigned) mem, (unsigned) virt_to_bus(mem));

			/* Zero out the allocated memory */
			memset(fh->v4l_buffers.buffer[i].fbuffer, 0, fh->v4l_buffers.buffer_size);
#elif defined(BUZ_USE_HIMEM)

			/* Use high memory which has been left at boot time */

			/* Ok., Ok. this is an evil hack - we make the assumption that
			   physical addresses are the same as bus addresses (true at least
			   for Intel processors). The whole method of obtaining and using
			   this memory is not very nice - but I hope it saves some
			   poor users from kernel hacking, which might have even more
			   evil results */

			if (i == 0) {
				pmem = get_high_mem(fh->v4l_buffers.num_buffers * fh->v4l_buffers.buffer_size);
				if (pmem == 0) {
					dprintk(0, KERN_ERR "%s: get_high_mem for V4L bufs failed\n", zr->name);
					return -ENOBUFS;
				}
				fh->v4l_buffers.buffer[0].fbuffer = 0;
				fh->v4l_buffers.buffer[0].fbuffer_phys = pmem;
				fh->v4l_buffers.buffer[0].fbuffer_bus = pmem;
				dprintk(0, KERN_INFO "%s: Using %d KB high memory\n",
					zr->name, (fh->v4l_buffers.num_buffers * fh->v4l_buffers.buffer_size) >> 10);
			} else {
				fh->v4l_buffers.buffer[i].fbuffer = 0;
				fh->v4l_buffers.buffer[i].fbuffer_phys = pmem + i * fh->v4l_buffers.buffer_size;
				fh->v4l_buffers.buffer[i].fbuffer_bus = pmem + i * fh->v4l_buffers.buffer_size;
			}
#else
			/* No bigphysarea present, usage of high memory disabled,
			   but user wants buffers of more than MAX_KMALLOC_MEM */
			dprintk(0, KERN_ERR "%s: No bigphysarea_patch present, usage of high memory disabled,\n", zr->name);
			dprintk(0, KERN_ERR "%s: sorry, could not allocate V4L buffers of size %d KB.\n",
				zr->name, fh->v4l_buffers.buffer_size >> 10);
			return -ENOBUFS;
#endif
		}
	}

	fh->v4l_buffers.allocated = 1;
	fh->v4l_buffers.secretly_allocated = 0;
	return 0;
}

/* free the V4L grab buffers */
static void
v4l_fbuffer_free (struct file *file)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, off;
	unsigned char *mem;

	dprintk(1, KERN_INFO "%s: v4l_fbuffer_free\n", zr->name);

	for (i = 0; i < fh->v4l_buffers.num_buffers; i++) {
		if (!fh->v4l_buffers.buffer[i].fbuffer)
			continue;

		if (fh->v4l_buffers.buffer_size <= MAX_KMALLOC_MEM) {
			mem = fh->v4l_buffers.buffer[i].fbuffer;
			for (off = 0; off < fh->v4l_buffers.buffer_size; off += PAGE_SIZE)
				mem_map_unreserve(MAP_NR(mem + off));
			kfree((void *) fh->v4l_buffers.buffer[i].fbuffer);
		}
#if defined(CONFIG_BIGPHYS_AREA)
		else
			bigphysarea_free_pages((void *) fh->v4l_buffers.buffer[i].fbuffer);
#endif
		fh->v4l_buffers.buffer[i].fbuffer = NULL;
	}

	fh->v4l_buffers.allocated = 0;
	fh->v4l_buffers.secretly_allocated = 0;
}

/*
 *   Allocate the MJPEG grab buffers.
 *
 *   If the requested buffer size is smaller than MAX_KMALLOC_MEM,
 *   kmalloc is used to request a physically contiguous area,
 *   else we allocate the memory in framgents with get_free_page.
 *
 *   If a Natoma chipset is present and this is a revision 1 zr36057,
 *   each MJPEG buffer needs to be physically contiguous.
 *   (RJ: This statement is from Dave Perks' original driver,
 *   I could never check it because I have a zr36067)
 *   The driver cares about this because it reduces the buffer
 *   size to MAX_KMALLOC_MEM in that case (which forces contiguous allocation).
 *
 *   RJ: The contents grab buffers needs never be accessed in the driver.
 *       Therefore there is no need to allocate them with vmalloc in order
 *       to get a contiguous virtual memory space.
 *       I don't understand why many other drivers first allocate them with
 *       vmalloc (which uses internally also get_free_page, but delivers you
 *       virtual addresses) and then again have to make a lot of efforts
 *       to get the physical address.
 *
 */

static int jpg_fbuffer_alloc(struct file *file)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i, j, off; //alloc_contig;
	unsigned long mem;

	/* Decide if we should alloc contiguous or fragmented memory */
	/* This has to be identical in jpg_fbuffer_alloc and jpg_fbuffer_free */

	//alloc_contig = (fh->jpg_buffers.buffer_size <= MAX_KMALLOC_MEM);

	for (i = 0; i < fh->jpg_buffers.num_buffers; i++) {
		if (fh->jpg_buffers.buffer[i].frag_tab)
			dprintk(0, KERN_WARNING "%s: jpg_fbuffer_alloc: buffer %d allready allocated ???\n", zr->name, i);

		/* Allocate fragment table for this buffer */

		mem = get_free_page(GFP_KERNEL);
		if (mem == 0) {
			dprintk(0, KERN_ERR "%s: jpg_fbuffer_alloc: get_free_page (frag_tab) failed for buffer %d\n", zr->name, i);
			jpg_fbuffer_free(file);
			return -ENOBUFS;
		}
		memset((void *) mem, 0, PAGE_SIZE);
		fh->jpg_buffers.buffer[i].frag_tab = (u32 *) mem;
		fh->jpg_buffers.buffer[i].frag_tab_bus = virt_to_bus((void *) mem);

		//if (alloc_contig) {
		if (fh->jpg_buffers.need_contiguous) {
			mem = (unsigned long) kmalloc(fh->jpg_buffers.buffer_size, GFP_KERNEL);
			if (mem == 0) {
				dprintk(0, KERN_ERR "%s: jpg_fbuffer_alloc: kmalloc failed for buffer %d\n", zr->name, i);
				jpg_fbuffer_free(file);
				return -ENOBUFS;
			}
			fh->jpg_buffers.buffer[i].frag_tab[0] = virt_to_bus((void *) mem);
			fh->jpg_buffers.buffer[i].frag_tab[1] = ((fh->jpg_buffers.buffer_size / 4) << 1) | 1;
			for (off = 0; off < fh->jpg_buffers.buffer_size; off += PAGE_SIZE)
				mem_map_reserve(MAP_NR(mem + off));
		} else {
			/* jpg_bufsize is allreay page aligned */
			for (j = 0; j < fh->jpg_buffers.buffer_size / PAGE_SIZE; j++) {
				mem = get_free_page(GFP_KERNEL);
				if (mem == 0) {
					dprintk(0, KERN_ERR "%s: jpg_fbuffer_alloc: get_free_page failed for buffer %d\n", zr->name, i);
					jpg_fbuffer_free(file);
					return -ENOBUFS;
				}

				fh->jpg_buffers.buffer[i].frag_tab[2 * j] = virt_to_bus((void *) mem);
				fh->jpg_buffers.buffer[i].frag_tab[2 * j + 1] = (PAGE_SIZE / 4) << 1;
				mem_map_reserve(MAP_NR(mem));
			}

			fh->jpg_buffers.buffer[i].frag_tab[2 * j - 1] |= 1;
		}
	}

	dprintk(1, KERN_DEBUG "%s: jpg_fbuffer_alloc: %d KB allocated\n", zr->name,
		      (fh->jpg_buffers.num_buffers * fh->jpg_buffers.buffer_size) >> 10);
	fh->jpg_buffers.allocated = 1;
	fh->jpg_buffers.secretly_allocated = 0;
	return 0;
}

/* free the MJPEG grab buffers */
static void jpg_fbuffer_free(struct file *file)
{
 	struct zoran_fh *fh = file->private_data;
	//struct zoran *zr = fh->zr;
	int i, j, off; // alloc_contig;
	unsigned char *mem;

	/* Decide if we should alloc contiguous or fragmented memory */
	/* This has to be identical in jpg_fbuffer_alloc and jpg_fbuffer_free */

	//alloc_contig = (zr->jpg_buffers.buffer_size <= MAX_KMALLOC_MEM);

	for (i = 0; i < fh->jpg_buffers.num_buffers; i++) {
		if (!fh->jpg_buffers.buffer[i].frag_tab)
			continue;

		//if (alloc_contig) {
		if (fh->jpg_buffers.need_contiguous) {
			if (fh->jpg_buffers.buffer[i].frag_tab[0]) {
				mem = (unsigned char *) bus_to_virt(fh->jpg_buffers.buffer[i].frag_tab[0]);
				for (off = 0; off < fh->jpg_buffers.buffer_size; off += PAGE_SIZE)
					mem_map_unreserve(MAP_NR(mem + off));
				kfree((void *) mem);
				fh->jpg_buffers.buffer[i].frag_tab[0] = 0;
				fh->jpg_buffers.buffer[i].frag_tab[1] = 0;
			}
		} else {
			for (j = 0; j < fh->jpg_buffers.buffer_size / PAGE_SIZE; j++) {
				if (!fh->jpg_buffers.buffer[i].frag_tab[2 * j])
					break;
				mem_map_unreserve(MAP_NR(bus_to_virt(fh->jpg_buffers.buffer[i].frag_tab[2 * j])));
				free_page((unsigned long) bus_to_virt(fh->jpg_buffers.buffer[i].frag_tab[2 * j]));
				fh->jpg_buffers.buffer[i].frag_tab[2 * j] = 0;
				fh->jpg_buffers.buffer[i].frag_tab[2 * j + 1] = 0;
			}
		}

		free_page((unsigned long) fh->jpg_buffers.buffer[i].frag_tab);
		fh->jpg_buffers.buffer[i].frag_tab = NULL;
	}
	fh->jpg_buffers.allocated = 0;
	fh->jpg_buffers.secretly_allocated = 0;
}


/*
 * General Purpose I/O and Guest bus access
 */

/*
 * This is a bit tricky. When a board lacks a GPIO function, the corresponding
 * GPIO bit number in the card_info structure is set to 0.
 */
static void GPIO(struct zoran *zr, int bit, unsigned int value)
{
	u32 reg;
	u32 mask;

	/* Make sure the bit number is legal
	 * A bit number of -1 (lacking) gives a mask of 0, making it harmless */
	mask = (1 << (24 + bit)) & 0xff000000;
	reg = btread(ZR36057_GPPGCR1) & ~mask;
	if (value) {
		reg |= mask;
	}
	btwrite(reg, ZR36057_GPPGCR1);
	udelay(1);
}

/*
 * Wait til post office is no longer busy
 */
static int post_office_wait(struct zoran *zr)
{
	u32 por;

//	while (((por = btread(ZR36057_POR)) & (ZR36057_POR_POPen | ZR36057_POR_POTime)) == ZR36057_POR_POPen) {
	while ((por = btread(ZR36057_POR)) & ZR36057_POR_POPen) {
		/* wait for something to happen */
	}
	if ((por & ZR36057_POR_POTime) && !zr->card->gws_not_connected) {
		/* In LML33/BUZ \GWS line is not connected, so it has always timeout set */
                dprintk(0, KERN_INFO "%s: pop timeout %08x\n", zr->name, por);
		return -1;
	}
	return 0;
}

static int post_office_write(struct zoran *zr, unsigned guest, unsigned reg, unsigned value)
{
	u32 por;

	por = ZR36057_POR_PODir | ZR36057_POR_POTime | ((guest & 7) << 20) | ((reg & 7) << 16) | (value & 0xFF);
	btwrite(por, ZR36057_POR);
	return post_office_wait(zr);
}

static int post_office_read(struct zoran *zr, unsigned guest, unsigned reg)
{
	u32 por;

	por = ZR36057_POR_POTime | ((guest & 7) << 20) | ((reg & 7) << 16);
	btwrite(por, ZR36057_POR);
	if (post_office_wait(zr) < 0) {
		return -1;
	}
	return btread(ZR36057_POR) & 0xFF;
}



/* videocodec bus functions ZR36060 */
static __u32 zr36060_read(struct videocodec *codec, __u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 0, 1, reg >> 8)
	    || post_office_write(zr, 0, 2, reg & 0xff)) {
		return -1;
	}

	data = post_office_read(zr, 0, 3) & 0xff;
	return data;
}

static void zr36060_write(struct videocodec *codec, __u16 reg, __u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 0, 1, reg >> 8)
	    || post_office_write(zr, 0, 2, reg & 0xff)) {
		return;
	}

	post_office_write(zr, 0, 3, val & 0xff);
}

/* videocodec bus functions ZR36050 */
static __u32 zr36050_read(struct videocodec *codec, __u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 1, 0, reg >> 2)) {  // reg. HIGHBYTES
		return -1;
	}

	data = post_office_read(zr, 0, reg&0x03) & 0xff; // reg. LOWBYTES + read
	return data;
}

static void zr36050_write(struct videocodec *codec, __u16 reg, __u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr)
	    || post_office_write(zr, 1, 0, reg >> 2)) {  // reg. HIGHBYTES
		return;
	}

	post_office_write(zr, 0, reg&0x03, val&0xff); // reg. LOWBYTES + wr. data
}

/* videocodec bus functions ZR36016 */
static __u32 zr36016_read(struct videocodec *codec, __u16 reg)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;
	__u32 data;

	if (post_office_wait(zr)) {
		return -1;
	}

	data = post_office_read(zr, 2, reg&0x03) & 0xff;      // read
	return data;
}

static void zr36016_write(struct videocodec *codec, __u16 reg, __u32 val)
{
	struct zoran *zr = (struct zoran *)codec->master_data->data;

	if (post_office_wait(zr)) {
		return;
	}

	post_office_write(zr, 2, reg&0x03, val&0x0ff); // wr. data
}



/*
 * JPEG Codec access
 */

static void jpeg_codec_sleep(struct zoran *zr, int sleep)
{
	GPIO(zr, zr->card->gpio[GPIO_JPEG_SLEEP], !sleep);
	if (!sleep)
	{
		dprintk(2, KERN_DEBUG "%s: jpeg_codec_sleep wake GPIO=0x%08x\n", zr->name, btread(ZR36057_GPPGCR1));
		udelay(500);
	}
	else
	{
		dprintk(2, KERN_DEBUG "%s: jpeg_codec_sleep sleep GPIO=0x%08x\n", zr->name, btread(ZR36057_GPPGCR1));
		udelay(2);
	}
}

static int jpeg_codec_reset(struct zoran *zr)
{
	/* Take the codec out of sleep */
	jpeg_codec_sleep(zr, 0);

	if (zr->card->gpcs[GPCS_JPEG_RESET] != 0xff)
	{
		post_office_write(zr, zr->card->gpcs[GPCS_JPEG_RESET], 0, 0);
		udelay(2);
	}
	else
	{
		GPIO(zr, zr->card->gpio[GPIO_JPEG_RESET], 0);
		udelay(2);
		GPIO(zr, zr->card->gpio[GPIO_JPEG_RESET], 1);
		udelay(2);
	}
	return 0;
}


/*
 * Board specific information
 */

static void dc10_init (struct zoran *zr)
{
	dprintk(2, KERN_DEBUG "%s: dc10_init\n", zr->name);
	/* Pixel clock selection */
	GPIO(zr, 4, 0);
	GPIO(zr, 5, 1);
	/* Enable the video bus sync signals */
	GPIO(zr, 7, 0);
}

static void dc10plus_init (struct zoran *zr)
{
	dprintk(2, KERN_DEBUG "%s: dc10plus_init\n", zr->name);
}

static void buz_init (struct zoran *zr)
{
	dprintk(2, KERN_DEBUG "%s: buz_init\n", zr->name);
}

static void lml33_init (struct zoran *zr)
{
	dprintk(2, KERN_DEBUG "%s: lml33_init\n", zr->name);
	GPIO(zr, 2, 1);	// Set Composite input/output
}

// struct tvnorm {
// 	u16 Wt, Wa, HStart, HSyncStart, Ht, Ha, VStart;
// };

/* The DC10 (57/16/50) uses VActive as HSync, so HStart must be 0 */

static struct tvnorm f50sqpixel = { 944, 768, 83, 880, 625, 576, 16 };
static struct tvnorm f60sqpixel = { 780, 640, 51, 716, 525, 480, 12 };
static struct tvnorm f50ccir601 = { 864, 720, 75, 804, 625, 576, 18 };
static struct tvnorm f60ccir601 = { 858, 720, 57, 788, 525, 480, 16 };

static struct tvnorm f50sqpixel_dc10 = { 944, 768, 0, 880, 625, 576, 16 };
static struct tvnorm f60sqpixel_dc10 = { 780, 640, 0, 716, 525, 480, 12 };
static struct tvnorm f50ccir601_dc10 = { 864, 720, 0, 804, 625, 576, 18 };
static struct tvnorm f60ccir601_dc10 = { 858, 720, 0, 788, 525, 480, 16 };

static struct card_info dc10_info = {
  type: DC10,
  inputs: 3,
  input: { { 1, "Composite" }, { 2, "SVHS" } , { 0, "Internal/comp" } },
  norms: 3,
//  tvn: { &f50ccir601_dc10, &f60ccir601_dc10, &f50ccir601_dc10 },
  tvn: { &f50sqpixel_dc10, &f60sqpixel_dc10, &f50sqpixel_dc10 },
  jpeg_int: 0,
  vsync_int: ZR36057_ISR_GIRQ1,
  gpio: { 2, 1, -1, 3, 7, 0, 4, 5 },
  gpio_pol: { 0, 0, 0, 1, 0, 0, 0, 0 },
  gpcs: { -1, 0 },
  vfe_pol: { 0, 0, 0, 0, 0, 0, 0, 0 },
  gws_not_connected: 0,
  init: &dc10_init,
};

static struct card_info dc10plus_info = {
  type: DC10plus,
  inputs: 3,
  input: { { 0, "Composite" }, { 7, "SVHS" } , { 5, "Internal/comp" } },
  norms: 3,
  tvn: { &f50sqpixel, &f60sqpixel, &f50sqpixel },
  jpeg_int: ZR36057_ISR_GIRQ0,
  vsync_int: ZR36057_ISR_GIRQ1,
  gpio: { 3, 0, 6, 1, 2, -1, 4, 5 },
  gpio_pol: { 0, 0, 0, 0, 0, 0, 0, 0 },
  gpcs: { -1, 1 },
  vfe_pol: { 1, 1, 1, 1, 0, 0, 0, 0 },
  gws_not_connected: 0,
  init: &dc10plus_init,
};

static struct card_info lml33_info = {
  type: LML33,
  inputs: 2,
  input: { { 0, "Composite" }, { 7, "SVHS" } },
  norms: 2,
  tvn: { &f50ccir601, &f60ccir601 },
  jpeg_int: ZR36057_ISR_GIRQ1,
  vsync_int: ZR36057_ISR_GIRQ0,
  gpio: { 1, -1, 3, 5, 7, -1, -1, -1 },
  gpio_pol: { 0, 0, 0, 0, 1, 0, 0, 0 },
  gpcs: { 3, 1 },
  vfe_pol: { 1, 0, 0, 0, 0, 1, 0, 0 },
  gws_not_connected: 1,
  init: &lml33_init,
};

static struct card_info buz_info = {
  type: BUZ,
  inputs: 2,
  input: { { 3, "Composite" }, { 7, "SVHS" } },
  norms: 3,
  tvn: { &f50ccir601, &f60ccir601, &f50ccir601 },
  jpeg_int: ZR36057_ISR_GIRQ1,
  vsync_int: ZR36057_ISR_GIRQ0,
  gpio: { 1, -1, 3, -1, -1, -1, -1, -1 },
  gpio_pol: { 0, 0, 0, 0, 0, 0, 0, 0 },
  gpcs: { 3, 1 },
  vfe_pol: { 1, 1, 0, 0, 0, 1, 0, 0 },
  gws_not_connected: 1,
  init: &buz_init,
};


/*
 * I2C functions
 */

/* software I2C functions */

static int zoran_i2c_getsda (void *data)
{
	struct zoran *zr = (struct zoran *)data;

	return (btread(ZR36057_I2CBR) >> 1) & 1;
}
 
static int zoran_i2c_getscl (void *data)
{
	struct zoran *zr = (struct zoran *)data;

	return btread(ZR36057_I2CBR) & 1;
}
 
static void zoran_i2c_setsda (void *data, int state)
{
	struct zoran *zr = (struct zoran *)data;

	if (state)
		zr->i2cbr |= 2;
	else
		zr->i2cbr &= ~2;

	btwrite(zr->i2cbr, ZR36057_I2CBR);
}

static void zoran_i2c_setscl (void *data, int state)
{
	struct zoran *zr = (struct zoran *)data;

	if (state)
		zr->i2cbr |= 1;
	else
		zr->i2cbr &= ~1;

	btwrite(zr->i2cbr, ZR36057_I2CBR);
}

static void zoran_i2c_inc_use (struct i2c_adapter *adapter)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}
 
static void zoran_i2c_dec_use (struct i2c_adapter *adapter)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static int zoran_i2c_client_register (struct i2c_client *client)
{
        struct zoran *zr = (struct zoran *)(client->adapter->data);

	dprintk(1, KERN_DEBUG "%s: i2c_client_register (driver id: %d)\n", zr->name, client->driver->id);

	switch (client->driver->id) {
	case I2C_DRIVERID_VPX32XX:
		zr->card = &dc10_info;
		sprintf(zr->name, "DC10(old)[%u]", zr->id);
		zr->decoder = client;
		break;

	case I2C_DRIVERID_SAA7110:
		if (zr->revision < 2) {
			zr->card = &dc10plus_info;
			sprintf(zr->name, "DC10(new)[%u]", zr->id);
		} else {
			zr->card = &dc10plus_info;
			sprintf(zr->name, "DC10plus[%u]", zr->id);
		}
		zr->decoder = client;
		break;

	case I2C_DRIVERID_BT819:
		zr->card = &lml33_info;
                sprintf(zr->name, "LML33[%u]", zr->id);
		zr->decoder = client;
		break;

	case I2C_DRIVERID_SAA7111A:
		zr->card = &buz_info;
		sprintf(zr->name, "Buz[%u]", zr->id);
		zr->decoder = client;
		break;

	case I2C_DRIVERID_SAA7185B:
	case I2C_DRIVERID_BT856:
	case I2C_DRIVERID_ADV717X:
		zr->encoder = client;
		break;
	}

	return 0;
}
 
static int zoran_i2c_client_unregister (struct i2c_client *client)
{
	struct zoran *zr = (struct zoran *)client->adapter->data;
	dprintk(1, KERN_DEBUG "%s: i2c_client_unregister\n", zr->name);

	return 0;
}

static struct i2c_algo_bit_data zoran_i2c_bit_data_template = {
	setsda:			zoran_i2c_setsda,
	setscl:			zoran_i2c_setscl,
	getsda:			zoran_i2c_getsda,
	getscl:			zoran_i2c_getscl,
	udelay:			10,
	mdelay:			0,
	timeout:		100,
};

static struct i2c_adapter zoran_i2c_adapter_template = {
	name:			"zr36057",
	id:			I2C_HW_B_BT848,
	algo:			NULL,
	inc_use:		zoran_i2c_inc_use,
	dec_use:		zoran_i2c_dec_use,
	client_register:	zoran_i2c_client_register,
	client_unregister:	zoran_i2c_client_unregister,
};

static int zoran_register_i2c (struct zoran *zr)
{
	memcpy(&zr->i2c_algo, &zoran_i2c_bit_data_template, sizeof(struct i2c_algo_bit_data));
	zr->i2c_algo.data = zr;

	memcpy(&zr->i2c_adapter, &zoran_i2c_adapter_template, sizeof(struct i2c_adapter));
	strcpy(zr->i2c_adapter.name, zr->name);
	zr->i2c_adapter.data = zr;
	zr->i2c_adapter.algo_data = &zr->i2c_algo;

	return i2c_bit_add_bus(&zr->i2c_adapter);
}

static inline void zoran_unregister_i2c (struct zoran *zr)
{
	i2c_bit_del_bus((&zr->i2c_adapter));
}

// Interface to decoder and encoder chips using i2c bus

static inline int decoder_command(struct zoran *zr, int cmd, void *data)
{
	if (zr->decoder == NULL)
		return -1;

        if (zr->card->type == LML33 && (cmd == DECODER_SET_NORM || DECODER_SET_INPUT)) {
		int res;
                // Bt819 needs to reset its FIFO buffer using #FRST pin and
                // LML33 card uses GPIO(7) for that.
                GPIO(zr, 7, 0); 
		res = zr->decoder->driver->command(zr->decoder, cmd, data);
                // Pull #FRST high.
                GPIO(zr, 7, 1);
                return res;
        } else
                return zr->decoder->driver->command(zr->decoder, cmd, data);
}

static inline int encoder_command(struct zoran *zr, int cmd, void *data)
{
	if (zr->encoder == NULL)
		return -1;

	return zr->encoder->driver->command(zr->encoder, cmd, data);
}

/*
 *   Set the registers for the size we have specified. Don't bother
 *   trying to understand this without the ZR36057 manual in front of
 *   you [AC].
 *
 *   PS: The manual is free for download in .pdf format from
 *   www.zoran.com - nicely done those folks.
 */

static void zr36057_adjust_vfe(struct zoran *zr, enum zoran_codec_mode mode)
{
	u32 reg;
	switch (mode) {
	case BUZ_MODE_MOTION_DECOMPRESS:
		btand(~ZR36057_VFESPFR_ExtFl, ZR36057_VFESPFR);
		reg = btread(ZR36057_VFEHCR);
		if (reg & (1 << 10)) {
			reg += ((1 << 10) | 1);
		}
		btwrite(reg, ZR36057_VFEHCR);
		break;
	case BUZ_MODE_MOTION_COMPRESS:
	case BUZ_MODE_IDLE:
	default:
		if (zr->norm == VIDEO_MODE_NTSC)
			btand(~ZR36057_VFESPFR_ExtFl, ZR36057_VFESPFR);
		else
			btor(ZR36057_VFESPFR_ExtFl, ZR36057_VFESPFR);
		reg = btread(ZR36057_VFEHCR);
		if (!(reg & (1 << 10))) {
			reg -= ((1 << 10) | 1);
		}
		btwrite(reg, ZR36057_VFEHCR);
		break;
	}
}

/*
 * set geometry
 */
static void
zr36057_set_vfe (struct zoran              *zr,
                 int                        video_width,
                 int                        video_height,
                 const struct zoran_format *format)
{
	struct tvnorm *tvn;
	unsigned HStart, HEnd, VStart, VEnd;
	unsigned DispMode;
	unsigned VidWinWid, VidWinHt;
	unsigned hcrop1, hcrop2, vcrop1, vcrop2;
	unsigned Wa, We, Ha, He;
	unsigned X, Y, HorDcm, VerDcm;
	u32 reg;
	unsigned mask_line_size;

	tvn = zr->timing;

	Wa = tvn->Wa;
	Ha = tvn->Ha;

	dprintk(1, KERN_INFO "%s: width = %d, height = %d\n", zr->name, video_width, video_height);

	if (zr->norm != VIDEO_MODE_PAL && zr->norm != VIDEO_MODE_NTSC && zr->norm != VIDEO_MODE_SECAM) {
		dprintk(0, KERN_ERR "%s: set_vfe: video_norm = %d not valid\n", zr->name, zr->norm);
		return;
	}
	if (video_width < BUZ_MIN_WIDTH || video_height < BUZ_MIN_HEIGHT || video_width > Wa || video_height > Ha) {
		dprintk(0, KERN_ERR "%s: set_vfe: w=%d h=%d not valid\n", zr->name, video_width, video_height);
		return;
	}

	/**** zr36057 ****/

	/* horizontal */
	VidWinWid = video_width;
	X = (VidWinWid * 64 + tvn->Wa - 1) / tvn->Wa;
	We = (VidWinWid * 64) / X;
	HorDcm = 64 - X;
	hcrop1 = 2 * ((tvn->Wa - We) / 4);
	hcrop2 = tvn->Wa - We - hcrop1;
	HStart = tvn->HStart | 1; // | 1 Doesn't have any effect, tested on both a DC10 and a DC10+
/*        if (zr->card->type == LML33)
		HStart += 62;
        if (zr->card->type == BUZ) {   //HStart += 67;
                HStart += 44;
        }
This is not needed anymore (I think) as HSYNC pol has been changed for the BUZ and LML33
*/
	HEnd = HStart + tvn->Wa - 1;
	HStart += hcrop1;
	HEnd -= hcrop2;
	reg = ((HStart & ZR36057_VFEHCR_Hmask) << ZR36057_VFEHCR_HStart)
	    | ((HEnd & ZR36057_VFEHCR_Hmask) << ZR36057_VFEHCR_HEnd);
	if (zr->card->vfe_pol.hsync_pol)
                reg |= ZR36057_VFEHCR_HSPol;
	btwrite(reg, ZR36057_VFEHCR);

	/* Vertical */
	DispMode = !(video_height > BUZ_MAX_HEIGHT / 2);
	VidWinHt = DispMode ? video_height : video_height / 2;
	Y = (VidWinHt * 64 * 2 + tvn->Ha - 1) / tvn->Ha;
	He = (VidWinHt * 64) / Y;
	VerDcm = 64 - Y;
	vcrop1 = (tvn->Ha / 2 - He) / 2;
	vcrop2 = tvn->Ha / 2 - He - vcrop1;
	VStart = tvn->VStart;
	VEnd = VStart + tvn->Ha / 2; // - 1; FIXME SnapShot times out with -1 in 768*576 on the DC10 - LP
	VStart += vcrop1;
	VEnd -= vcrop2;
	reg = ((VStart & ZR36057_VFEVCR_Vmask) << ZR36057_VFEVCR_VStart)
	    | ((VEnd & ZR36057_VFEVCR_Vmask) << ZR36057_VFEVCR_VEnd);
	if (zr->card->vfe_pol.vsync_pol)
		reg |= ZR36057_VFEVCR_VSPol;
	btwrite(reg, ZR36057_VFEVCR);

	/* scaler and pixel format */
	reg = 0;
	reg |= (HorDcm << ZR36057_VFESPFR_HorDcm);
	reg |= (VerDcm << ZR36057_VFESPFR_VerDcm);
	reg |= (DispMode << ZR36057_VFESPFR_DispMode);
	if (format->palette != VIDEO_PALETTE_YUV422)
		reg |= ZR36057_VFESPFR_LittleEndian;
	/* RJ: I don't know, why the following has to be the opposite
	   of the corresponding ZR36060 setting, but only this way
	   we get the correct colors when uncompressing to the screen  */
	//reg |= ZR36057_VFESPFR_VCLKPol; /**/
	/* RJ: Don't know if that is needed for NTSC also */
	if (zr->norm != VIDEO_MODE_NTSC)
		reg |= ZR36057_VFESPFR_ExtFl;	// NEEDED!!!!!!! Wolfgang
	reg |= ZR36057_VFESPFR_TopField;
	switch (format->palette) {

	case VIDEO_PALETTE_YUV422:
		reg |= ZR36057_VFESPFR_YUV422;
		break;

	case VIDEO_PALETTE_RGB555:
		reg |= ZR36057_VFESPFR_RGB555 | ZR36057_VFESPFR_ErrDif;
		break;

	case VIDEO_PALETTE_RGB565:
		reg |= ZR36057_VFESPFR_RGB565 | ZR36057_VFESPFR_ErrDif;
		break;

	case VIDEO_PALETTE_RGB24:
		reg |= ZR36057_VFESPFR_RGB888 | ZR36057_VFESPFR_Pack24;
		break;

	case VIDEO_PALETTE_RGB32:
		reg |= ZR36057_VFESPFR_RGB888;
		break;

	default:
		dprintk(0, KERN_INFO "%s: Unknown color_fmt=%x\n", zr->name,
			format->palette);
		return;

	}
	if (HorDcm >= 48) {
		reg |= 3 << ZR36057_VFESPFR_HFilter;	/* 5 tap filter */
	} else if (HorDcm >= 32) {
		reg |= 2 << ZR36057_VFESPFR_HFilter;	/* 4 tap filter */
	} else if (HorDcm >= 16) {
		reg |= 1 << ZR36057_VFESPFR_HFilter;	/* 3 tap filter */
	}
	btwrite(reg, ZR36057_VFESPFR);

	/* display configuration */

	reg = (16 << ZR36057_VDCR_MinPix)
	    | (VidWinHt << ZR36057_VDCR_VidWinHt)
	    | (VidWinWid << ZR36057_VDCR_VidWinWid);
	if (triton) // || zr->revision < 1) // Revision 1 has also Triton support
		reg &= ~ZR36057_VDCR_Triton;
	else
		reg |= ZR36057_VDCR_Triton;
	btwrite(reg, ZR36057_VDCR);

	/* (Ronald) don't write this if overlay_mask = NULL */
	if (zr->overlay_mask) {
		/* Write overlay clipping mask data, but don't enable overlay clipping */
		/* RJ: since this makes only sense on the screen, we use 
		   zr->overlay_settings.width instead of video_width */

		mask_line_size = (BUZ_MAX_WIDTH + 31) / 32;
		reg = virt_to_bus(zr->overlay_mask);
		btwrite(reg, ZR36057_MMTR);
		reg = virt_to_bus(zr->overlay_mask + mask_line_size);
		btwrite(reg, ZR36057_MMBR);
		reg = mask_line_size - (zr->overlay_settings.width + 31) / 32;
		if (DispMode == 0)
			reg += mask_line_size;
		reg <<= ZR36057_OCR_MaskStride;
		btwrite(reg, ZR36057_OCR);
	}

	zr36057_adjust_vfe(zr, zr->codec_mode);

}

/*
 * Switch overlay on or off
 */

static void zr36057_overlay(struct zoran *zr, int on)
{
	const struct zoran_format *format = NULL;
	int fmt, i;
	u32 reg;

	if (on) {
		/* do the necessary settings ... */

		btand(~ZR36057_VDCR_VidEn, ZR36057_VDCR);	/* switch it off first */

		switch (zr->buffer.depth) {
		case 15:
			fmt = VIDEO_PALETTE_RGB555;
			break;
		case 16:
			fmt = VIDEO_PALETTE_RGB565;
			break;
		case 24:
			fmt = VIDEO_PALETTE_RGB24;
			break;
		case 32:
			fmt = VIDEO_PALETTE_RGB32;
			break;
		default:
			fmt = 0;
		}

		if (fmt) {
			for (i=0;i<ZORAN_FORMATS;i++) {
				if (zoran_formats[i].palette == fmt &&
				    zoran_formats[i].flags & ZORAN_FORMAT_OVERLAY)
					break;
			}
			if (i != ZORAN_FORMATS) {
				format = &zoran_formats[i];
				zr36057_set_vfe(zr, zr->overlay_settings.width, zr->overlay_settings.height, format);
			}
		}

		/* Start and length of each line MUST be 4-byte aligned.
		   This should be allready checked before the call to this routine.
		   All error messages are internal driver checking only! */

		/* video display top and bottom registers */

		reg = (u32) zr->buffer.base + zr->overlay_settings.x * ((format->depth+7)/8) + zr->overlay_settings.y * zr->buffer.bytesperline;
		btwrite(reg, ZR36057_VDTR);
		if (reg & 3)
			dprintk(0, KERN_ERR "%s: zr36057_overlay: video_address not aligned\n", zr->name);
		if (zr->overlay_settings.height > BUZ_MAX_HEIGHT / 2)
			reg += zr->buffer.bytesperline;
		btwrite(reg, ZR36057_VDBR);

		/* video stride, status, and frame grab register */

		reg = zr->buffer.bytesperline - zr->overlay_settings.width * ((format->depth+7)/8);
		if (zr->overlay_settings.height > BUZ_MAX_HEIGHT / 2)
			reg += zr->buffer.bytesperline;
		if (reg & 3)
			dprintk(0, KERN_ERR "%s: zr36057_overlay: video_stride not aligned\n", zr->name);
		reg = (reg << ZR36057_VSSFGR_DispStride);
		reg |= ZR36057_VSSFGR_VidOvf;	/* clear overflow status */
		btwrite(reg, ZR36057_VSSFGR);

		/* Set overlay clipping */

		if (zr->overlay_settings.clipcount > 0)
			btor(ZR36057_OCR_OvlEnable, ZR36057_OCR);

		/* ... and switch it on */

		btor(ZR36057_VDCR_VidEn, ZR36057_VDCR);
	} else {
		/* Switch it off */

		btand(~ZR36057_VDCR_VidEn, ZR36057_VDCR);
	}
}

/*
 * The overlay mask has one bit for each pixel on a scan line,
 *  and the maximum window size is BUZ_MAX_WIDTH * BUZ_MAX_HEIGHT pixels.
 */
static void
write_overlay_mask (struct file       *file,
                    struct video_clip *vp,
                    int                count)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned mask_line_size = (BUZ_MAX_WIDTH + 31) / 32;
	u32 *mask;
	int x, y, width, height;
	unsigned i, j, k;
	u32 reg;

	/* fill mask with one bits */
	memset(fh->overlay_mask, ~0, mask_line_size * 4 * BUZ_MAX_HEIGHT);
	reg = 0;

	for (i = 0; i < count; ++i) {
		/* pick up local copy of clip */
		x = vp[i].x;
		y = vp[i].y;
		width = vp[i].width;
		height = vp[i].height;

		/* trim clips that extend beyond the window */
		if (x < 0) {
			width += x;
			x = 0;
		}
		if (y < 0) {
			height += y;
			y = 0;
		}
		if (x + width > fh->overlay_settings.width) {
			width = fh->overlay_settings.width - x;
		}
		if (y + height > fh->overlay_settings.height) {
			height = fh->overlay_settings.height - y;
		}

		/* ignore degenerate clips */
		if (height <= 0) {
			continue;
		}
		if (width <= 0) {
			continue;
		}

		/* apply clip for each scan line */
		for (j = 0; j < height; ++j) {
			/* reset bit for each pixel */
			/* this can be optimized later if need be */
			mask = fh->overlay_mask + (y + j) * mask_line_size;
			for (k = 0; k < width; ++k) {
				mask[(x + k) / 32] &= ~((u32) 1 << (x + k) % 32);
			}
		}
	}
}

/* Enable/Disable uncompressed memory grabbing of the 36057 */

static void zr36057_set_memgrab(struct zoran *zr, int mode)
{
	if (mode) {
		if (btread(ZR36057_VSSFGR) & (ZR36057_VSSFGR_SnapShot | ZR36057_VSSFGR_FrameGrab))
			dprintk(0, KERN_WARNING "%s: zr36057_set_memgrab_on with SnapShot or FrameGrab on ???\n", zr->name);

		/* switch on VSync interrupts */

		btwrite(IRQ_MASK, ZR36057_ISR);	// Clear Interrupts
		btor(zr->card->vsync_int, ZR36057_ICR);	// SW

		/* enable SnapShot */

		btor(ZR36057_VSSFGR_SnapShot, ZR36057_VSSFGR);

		/* Set zr36057 video front end  and enable video */

#ifdef XAWTV_HACK
		zr36057_set_vfe(zr, zr->v4l_settings.width > 720 ? 720 : zr->v4l_settings.width, zr->v4l_settings.height, zr->v4l_settings.format);
#else
		zr36057_set_vfe(zr, zr->v4l_settings.width, zr->v4l_settings.height, zr->v4l_settings.format);
#endif

		zr->v4l_memgrab_active = 1;
	} else {
		zr->v4l_memgrab_active = 0;

		/* switch off VSync interrupts */

		btand(~zr->card->vsync_int, ZR36057_ICR); // SW

		/* reenable grabbing to screen if it was running */

		if (zr->v4l_overlay_active) {
			zr36057_overlay(zr, 1);
		} else {
			btand(~ZR36057_VDCR_VidEn, ZR36057_VDCR);
			btand(~ZR36057_VSSFGR_SnapShot, ZR36057_VSSFGR);
		}
	}
}

static int wait_grab_pending(struct zoran *zr)
{
	unsigned long flags;

	/* wait until all pending grabs are finished */

	if (!zr->v4l_memgrab_active)
		return 0;

	while (zr->v4l_pend_tail != zr->v4l_pend_head) {
		interruptible_sleep_on(&zr->v4l_capq);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	spin_lock_irqsave(&zr->spinlock, flags);
	zr36057_set_memgrab(zr, 0);
	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

/*
 *   V4L Buffer grabbing
 */

static int
zoran_v4l_set_format (struct file               *file,
                      int                        width,
                      int                        height,
                      const struct zoran_format *format)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int bpp;

	/* Check size and format of the grab wanted */

	if (height < BUZ_MIN_HEIGHT || width < BUZ_MIN_WIDTH
	    || height > BUZ_MAX_HEIGHT || width > BUZ_MAX_WIDTH) {
		dprintk(0, KERN_ERR "%s: Wrong frame size (%dx%d)\n",
			zr->name, width, height);
		return -EINVAL;
	}

	bpp = (format->depth+7)/8;

	/* Check against available buffer size */
	if (height * width * bpp > fh->v4l_buffers.buffer_size) {
		dprintk(2, KERN_ERR "%s: Video buffer size (%d kB) is too small\n", zr->name,
			fh->v4l_buffers.buffer_size >> 10);
		return -EINVAL;
	}

	/* The video front end needs 4-byte alinged line sizes */

	if ((bpp == 2 && (width & 1)) || (bpp == 3 && (width & 3))) {
		dprintk(2, KERN_ERR "%s: Wrong frame alingment\n", zr->name);
		return -EINVAL;
	}

	fh->v4l_settings.width = width;
	fh->v4l_settings.height = height;
	fh->v4l_settings.format = format;
	fh->v4l_settings.bytesperline = bpp * fh->v4l_settings.width;

	return 0;
}

static int
zoran_v4l_queue_frame (struct file *file,
                       int          num)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	if (!fh->v4l_buffers.allocated) {
		dprintk(0, KERN_ERR "%s: zoran_v4l_queue_frame(): buffers not yet allocated\n", zr->name);
		res = -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->v4l_buffers.num_buffers || num < 0) {
		dprintk(2, KERN_ERR "%s: zoran_v4l_queue_frame() - buffer %d is out of range\n", zr->name, num);
		res = -EINVAL;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (fh->v4l_buffers.active == ZORAN_FREE) {
		if (zr->v4l_buffers.active == ZORAN_FREE) {
			zr->v4l_buffers = fh->v4l_buffers;
			fh->v4l_buffers.active = ZORAN_ACTIVE;
		}
		else {
			dprintk(0, KERN_ERR "%s: v4l_grab(): another session is already capturing\n", zr->name);
			res = -EBUSY;
		}
	}

	/* make sure a grab isn't going on currently with this buffer */
	if (!res) {
		switch (zr->v4l_buffers.buffer[num].state) {
			default:
			case BUZ_STATE_PEND:
				if (zr->v4l_buffers.active == ZORAN_FREE) {
					fh->v4l_buffers.active = ZORAN_FREE;
					zr->v4l_buffers.allocated = 0;
				}
				res = -EBUSY;	/* what are you doing? */
				break;
			case BUZ_STATE_DONE:
				dprintk(1, KERN_WARNING "%s: warning - v4l_grab: queueing buffer %d in state DONE\n",
					zr->name, num);
			case BUZ_STATE_USER:
				/* since there is at least one unused buffer there's room for at least
				 * one more pend[] entry */
				zr->v4l_pend[zr->v4l_pend_head++ & V4L_MASK_FRAME] = num;
				zr->v4l_buffers.buffer[num].state = BUZ_STATE_PEND;
				zr->v4l_buffers.buffer[num].bs.length = fh->v4l_settings.bytesperline * zr->v4l_settings.height;
				fh->v4l_buffers.buffer[num] = zr->v4l_buffers.buffer[num];
				break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->v4l_buffers.active == ZORAN_FREE)
		zr->v4l_buffers.active = fh->v4l_buffers.active;

	return res;
}

static int
v4l_grab (struct file       *file,
          struct video_mmap *mp)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int res=0, i;

	for (i=0;i<ZORAN_FORMATS;i++) {
		if (zoran_formats[i].palette == mp->format &&
		    zoran_formats[i].flags & ZORAN_FORMAT_CAPTURE &&
		    !(zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED))
			break;
	}
	if (i == ZORAN_FORMATS ||
	    zoran_formats[i].depth == 0) {
		dprintk(2, KERN_ERR "%s: Wrong bytes-per-pixel format\n", zr->name);
		return -EINVAL;
	}

	/*
	 * To minimize the time spent in the IRQ routine, we avoid setting up
	 * the video front end there.
	 * If this grab has different parameters from a running streaming capture
	 * we stop the streaming capture and start it over again.
	 */
	if (zr->v4l_memgrab_active &&
	    (zr->v4l_settings.width != mp->width ||
	     zr->v4l_settings.height != mp->height ||
	     zr->v4l_settings.format->palette != mp->format)) {
		res = wait_grab_pending(zr);
		if (res)
			return res;
	}
	if ((res = zoran_v4l_set_format(file,
		     mp->width, mp->height, &zoran_formats[i])))
		return res;
	zr->v4l_settings = fh->v4l_settings;

	/* queue the frame in the pending queue */
	if ((res = zoran_v4l_queue_frame(file, mp->frame))) {
		fh->v4l_buffers.active = ZORAN_FREE;
		return res;
	}

	/* put the 36057 into frame grabbing mode */
	if (!res && !zr->v4l_memgrab_active)
		zr36057_set_memgrab(zr, 1);

	//dprintk(2, KERN_INFO "%s: Frame grab 3...\n", zr->name);

	return res;
}

/*
 * Sync on a V4L buffer
 */

static int
v4l_sync (struct file *file,
          int          frame)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;

	if (fh->v4l_buffers.active == ZORAN_FREE) {
		dprintk(0, KERN_ERR "%s: v4l_sync: no grab active for this session\n", zr->name);
		return -EINVAL;
	}

	/* check passed-in frame number */
	if (frame >= fh->v4l_buffers.num_buffers || frame < 0) {
		dprintk(0, KERN_ERR "%s: v4l_sync: frame %d is invalid\n", zr->name, frame);
		return -EINVAL;
	}

	/* Check if is buffer was queued at all */
	if (zr->v4l_buffers.buffer[frame].state == BUZ_STATE_USER) {
		dprintk(0, KERN_ERR "%s: v4l_sync: Attempt to sync on a buffer which was not queued?\n", zr->name);
		return -EPROTO;
	}

	/* wait on this buffer to get ready */
	while (zr->v4l_buffers.buffer[frame].state == BUZ_STATE_PEND) {
		interruptible_sleep_on(&zr->v4l_capq);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	/* buffer should now be in BUZ_STATE_DONE */
	if (zr->v4l_buffers.buffer[frame].state != BUZ_STATE_DONE)
		dprintk(0, KERN_ERR "%s: v4l_sync - internal error\n", zr->name);

	zr->v4l_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->v4l_buffers.buffer[frame] = zr->v4l_buffers.buffer[frame];

	spin_lock_irqsave(&zr->spinlock, flags);

	/* Check if streaming capture has finished */
	if (zr->v4l_pend_tail == zr->v4l_pend_head) {
		zr36057_set_memgrab(zr, 0);
		if (zr->v4l_buffers.active == ZORAN_ACTIVE) {
			fh->v4l_buffers.active = zr->v4l_buffers.active = ZORAN_FREE;
			zr->v4l_buffers.allocated = 0;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

/*****************************************************************************
 *                                                                           *
 *  Set up the Buz-specific MJPEG part                                       *
 *                                                                           *
 *****************************************************************************/


static void set_frame(struct zoran *zr, int val)
{
	GPIO(zr, zr->card->gpio[GPIO_JPEG_FRAME], val);
}

static void set_videobus_dir(struct zoran *zr, int val)
{
	switch (zr->card->type) {
	case LML33:
		if (lml33dpath == 0)
			GPIO(zr, 5, val);
		else
			GPIO(zr, 5, 1);
		break;
	default:
		GPIO(zr,
			zr->card->gpio[GPIO_VID_DIR], 
			zr->card->gpio_pol[GPIO_VID_DIR] ? !val : val);
		break;
	}
}

static void set_videobus_enable(struct zoran *zr, int val)
{
	GPIO(zr, zr->card->gpio[GPIO_VID_EN],
	     zr->card->gpio_pol[GPIO_VID_EN] ? val : !val);
}

static void init_jpeg_queue(struct zoran *zr)
{
	int i;
	/* re-initialize DMA ring stuff */
	zr->jpg_que_head = 0;
	zr->jpg_dma_head = 0;
	zr->jpg_dma_tail = 0;
	zr->jpg_que_tail = 0;
	zr->jpg_seq_num = 0;
	zr->JPEG_error = 0;
	zr->num_errors = 0;
	zr->jpg_err_seq = 0;
	zr->jpg_err_shift = 0;
	zr->jpg_queued_num = 0;
	for (i = 0; i < zr->jpg_buffers.num_buffers; i++) {
		zr->jpg_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
	}
	for (i = 0; i < BUZ_NUM_STAT_COM; i++) {
		zr->stat_com[i] = 1;	/* mark as unavailable to zr36057 */
	}
}

static void zr36057_set_jpg(struct zoran *zr, enum zoran_codec_mode mode)
{
	struct tvnorm *tvn;
	u32 reg;

	tvn = zr->timing;

	/* assert P_Reset, disable code transfer, deassert Active */
	btwrite(0, ZR36057_JPC);

	/* MJPEG compression mode */
	switch (mode) {

	case BUZ_MODE_MOTION_COMPRESS:
	default:
		reg = ZR36057_JMC_MJPGCmpMode;
		break;

	case BUZ_MODE_MOTION_DECOMPRESS:
		reg = ZR36057_JMC_MJPGExpMode;
		reg |= ZR36057_JMC_SyncMstr;
		/* RJ: The following is experimental - improves the output to screen */
		//if(zr->jpg_settings.VFIFO_FB) reg |= ZR36057_JMC_VFIFO_FB; // No, it doesn't. SM
		break;

	case BUZ_MODE_STILL_COMPRESS:
		reg = ZR36057_JMC_JPGCmpMode;
		break;

	case BUZ_MODE_STILL_DECOMPRESS:
		reg = ZR36057_JMC_JPGExpMode;
		break;

	}
	reg |= ZR36057_JMC_JPG;
	if (zr->jpg_settings.field_per_buff == 1)
		reg |= ZR36057_JMC_Fld_per_buff;
	btwrite(reg, ZR36057_JMC);

	/* vertical */
	btor(ZR36057_VFEVCR_VSPol, ZR36057_VFEVCR);
	reg = (6 << ZR36057_VSP_VsyncSize) | (tvn->Ht << ZR36057_VSP_FrmTot);
	btwrite(reg, ZR36057_VSP);
	reg = ((zr->jpg_settings.img_y + tvn->VStart) << ZR36057_FVAP_NAY)
	    | (zr->jpg_settings.img_height << ZR36057_FVAP_PAY);
	btwrite(reg, ZR36057_FVAP);

	/* horizontal */
	btor(ZR36057_VFEHCR_HSPol, ZR36057_VFEHCR);
	reg = ((tvn->HSyncStart) << ZR36057_HSP_HsyncStart) | (tvn->Wt << ZR36057_HSP_LineTot);
	btwrite(reg, ZR36057_HSP);
	reg = ((zr->jpg_settings.img_x + tvn->HStart + 4) << ZR36057_FHAP_NAX)
	    | (zr->jpg_settings.img_width << ZR36057_FHAP_PAX);
	btwrite(reg, ZR36057_FHAP);

	/* field process parameters */
	if (zr->jpg_settings.odd_even)
		reg = ZR36057_FPP_Odd_Even;
	else
		reg = 0;

	btwrite(reg, ZR36057_FPP);

	/* Set proper VCLK Polarity, else colors will be wrong during playback */
	//btor(ZR36057_VFESPFR_VCLKPol, ZR36057_VFESPFR);

	/* code base address */
	reg = virt_to_bus(zr->stat_com);
	btwrite(reg, ZR36057_JCBA);

	/* FIFO threshold (FIFO is 160. double words) */
	/* NOTE: decimal values here */
	switch (mode) {

	case BUZ_MODE_STILL_COMPRESS:
	case BUZ_MODE_MOTION_COMPRESS:
		if (zr->card->type != BUZ)
                        reg = 140;
                else
                        reg = 60;
		break;

	case BUZ_MODE_STILL_DECOMPRESS:
	case BUZ_MODE_MOTION_DECOMPRESS:
		reg = 20;
		break;

	default:
		reg = 80;
		break;

	}
	btwrite(reg, ZR36057_JCFT);
	zr36057_adjust_vfe(zr, mode);

}

static void dump_guests(struct zoran *zr)
{
	if (debug > 2)
	{
		int i, guest[8];

		for (i = 1; i < 8; i++) {	// Don't read jpeg codec here
			guest[i] = post_office_read(zr, i, 0);
		}

		printk(KERN_INFO "%s: Guests:", zr->name);

		for (i = 1; i < 8; i++) {
			printk(" 0x%02x", guest[i]);
		}
		printk("\n");
	}
}

static unsigned long get_time(void)
{
	struct timeval tv;
	do_gettimeofday(&tv);
	return (1000000 * tv.tv_sec + tv.tv_usec);
}

static void detect_guest_activity(struct zoran *zr)
{
	int timeout, i, j, res, guest[8], guest0[8], change[8][3];
	unsigned long t0, t1;

	dump_guests(zr);
	printk(KERN_INFO "%s: Detecting guests activity, please wait...\n", zr->name);
	for (i = 1; i < 8; i++) {	// Don't read jpeg codec here
		guest0[i] = guest[i] = post_office_read(zr, i, 0);
	}

	timeout = 0;
	j = 0;
	t0 = get_time();
	while (timeout < 10000) {
		udelay(10);
		timeout++;
		for (i = 1; (i < 8) && (j < 8); i++) {
			res = post_office_read(zr, i, 0);
			if (res != guest[i]) {
				t1 = get_time();
				change[j][0] = (t1 - t0);
				t0 = t1;
				change[j][1] = i;
				change[j][2] = res;
				j++;
				guest[i] = res;
			}
		}
		if (j >= 8)
			break;
	}
	printk(KERN_INFO "%s: Guests:", zr->name);

	for (i = 1; i < 8; i++) {
		printk(" 0x%02x", guest0[i]);
	}
	printk("\n");
	if (j == 0) {
		printk(KERN_INFO "%s: No activity detected.\n", zr->name);
		return;
	}
	for (i = 0; i < j; i++) {
		printk(KERN_INFO "%s: %6d: %d => 0x%02x\n", zr->name, change[i][0], change[i][1], change[i][2]);
	}
}

static void print_interrupts(struct zoran *zr)
{
	int res, noerr;
	noerr = 0;
	printk(KERN_INFO "%s: interrupts received:", zr->name);
	if ((res = zr->field_counter) < -1 || res > 1) {
		printk(" FD:%d", res);
	}
	if ((res = zr->intr_counter_GIRQ1) != 0) {
		printk(" GIRQ1:%d", res);
		noerr++;
	}
	if ((res = zr->intr_counter_GIRQ0) != 0) {
		printk(" GIRQ0:%d", res);
		noerr++;
	}
	if ((res = zr->intr_counter_CodRepIRQ) != 0) {
		printk(" CodRepIRQ:%d", res);
		noerr++;
	}
	if ((res = zr->intr_counter_JPEGRepIRQ) != 0) {
		printk(" JPEGRepIRQ:%d", res);
		noerr++;
	}
	if (zr->JPEG_max_missed) {
		printk(" JPEG delays: max=%d min=%d", zr->JPEG_max_missed, zr->JPEG_min_missed);
	}
	if (zr->END_event_missed) {
		printk(" ENDs missed: %d", zr->END_event_missed);
	}
	//if (zr->jpg_queued_num) {
	printk(" queue_state=%ld/%ld/%ld/%ld", zr->jpg_que_tail, zr->jpg_dma_tail, zr->jpg_dma_head, zr->jpg_que_head);
	//}
	if (!noerr) {
		printk(": no interrupts detected.");
	}
	printk("\n");
}

static void clear_interrupt_counters(struct zoran *zr)
{
	zr->intr_counter_GIRQ1 = 0;
	zr->intr_counter_GIRQ0 = 0;
	zr->intr_counter_CodRepIRQ = 0;
	zr->intr_counter_JPEGRepIRQ = 0;
	zr->field_counter = 0;
	zr->IRQ1_in = 0;
	zr->IRQ1_out = 0;
	zr->JPEG_in = 0;
	zr->JPEG_out = 0;
	zr->JPEG_0 = 0;
	zr->JPEG_1 = 0;
	zr->END_event_missed = 0;
	zr->JPEG_missed = 0;
	zr->JPEG_max_missed = 0;
	zr->JPEG_min_missed = 0x7fffffff;
}

static u32 count_reset_interrupt(struct zoran *zr)
{
	u32 isr;
	if ((isr = btread(ZR36057_ISR) & 0x78000000)) {
		if (isr & ZR36057_ISR_GIRQ1) {
			btwrite(ZR36057_ISR_GIRQ1, ZR36057_ISR);
			zr->intr_counter_GIRQ1++;
		}
		if (isr & ZR36057_ISR_GIRQ0) {
			btwrite(ZR36057_ISR_GIRQ0, ZR36057_ISR);
			zr->intr_counter_GIRQ0++;
		}
		if (isr & ZR36057_ISR_CodRepIRQ) {
			btwrite(ZR36057_ISR_CodRepIRQ, ZR36057_ISR);
			zr->intr_counter_CodRepIRQ++;
		}
		if (isr & ZR36057_ISR_JPEGRepIRQ) {
			btwrite(ZR36057_ISR_JPEGRepIRQ, ZR36057_ISR);
			zr->intr_counter_JPEGRepIRQ++;
		}
	}
	return isr;
}

static void jpeg_start(struct zoran *zr)
{
	int reg;
	zr->frame_num = 0;

        /* deassert P_reset, disable code transfer, deassert Active */
        btwrite(ZR36057_JPC_P_Reset, ZR36057_JPC);
	/* stop flushing the internal code buffer */
	btand(~ZR36057_MCTCR_CFlush, ZR36057_MCTCR);
	/* enable code transfer */
	btor(ZR36057_JPC_CodTrnsEn, ZR36057_JPC);

        /* clear IRQs */
	btwrite(IRQ_MASK, ZR36057_ISR);
	/* enable the JPEG IRQs */
	btwrite(zr->card->jpeg_int | ZR36057_ICR_JPEGRepIRQ | ZR36057_ICR_IntPinEn, ZR36057_ICR);

	set_frame(zr, 0);                                       // \FRAME

	/* set the JPEG codec guest ID */
	reg = (zr->card->gpcs[1] << ZR36057_JCGI_JPEGuestID) | (0 << ZR36057_JCGI_JPEGuestReg);
	btwrite(reg, ZR36057_JCGI);

        if (zr->card->type == DC10 || zr->card->type == DC30plus)
	{
            /* Enable processing on the ZR36016 */
//            zr36016_write(zr, 0, 1);  // Why does this never return ?
            post_office_write(zr, 2, 0, 1);

            /* load the address of the GO register in the ZR36050 latch */
            post_office_write(zr, 0, 0, 0);
        }

        /* assert Active */
	btor(ZR36057_JPC_Active, ZR36057_JPC);

        /* enable the Go generation */
	btor(ZR36057_JMC_Go_en, ZR36057_JMC);
	udelay(30);

	set_frame(zr, 1);                               // /FRAME

        dprintk(2, KERN_DEBUG "%s: jpeg_start\n", zr->name);
}

static void zr36057_enable_jpg(struct zoran *zr, enum zoran_codec_mode mode)
{
	static int zero = 0;
	static int one = 1;
	struct vfe_settings cap;
	int field_size = zr->jpg_buffers.buffer_size / zr->jpg_settings.field_per_buff;

	zr->codec_mode = mode;

	cap.x = zr->jpg_settings.img_x;
	cap.y = zr->jpg_settings.img_y;
	cap.width = zr->jpg_settings.img_width;
	cap.height = zr->jpg_settings.img_height;
	cap.decimation = zr->jpg_settings.HorDcm | (zr->jpg_settings.VerDcm << 8);
	cap.quality = zr->jpg_settings.jpg_comp.quality;

	switch (mode) {

	case BUZ_MODE_MOTION_COMPRESS:
		/* In motion compress mode, the decoder output must be enabled, and
		 * the video bus direction set to input.
		 */
		// set_videobus_enable(zr, 0);
                set_videobus_dir(zr, 0);
		decoder_command(zr, DECODER_ENABLE_OUTPUT, &one);
		encoder_command(zr, ENCODER_SET_INPUT, &zero);
		// set_videobus_enable(zr, 1);

		/* Take the JPEG codec and the VFE out of sleep */
		jpeg_codec_sleep(zr, 0);
		/* Setup the VFE */
		if (zr->vfe) {
			zr->vfe->control(zr->vfe, CODEC_S_JPEG_TDS_BYTE,
					sizeof(int), &field_size);
			zr->vfe->set_video(zr->vfe, zr->timing,
					&cap, &zr->card->vfe_pol);
			zr->vfe->set_mode(zr->vfe, CODEC_DO_COMPRESSION);
		}
		/* Setup the JPEG codec */
		zr->codec->control(zr->codec, CODEC_S_JPEG_TDS_BYTE,
				sizeof(int), &field_size);
		zr->codec->set_video(zr->codec, zr->timing,
				&cap, &zr->card->vfe_pol);
		zr->codec->set_mode(zr->codec, CODEC_DO_COMPRESSION);

		init_jpeg_queue(zr);
		zr36057_set_jpg(zr, mode);	// \P_Reset, ... Video param, FIFO

	        clear_interrupt_counters(zr);
		dprintk(1, KERN_INFO "%s: enable_jpg MOTION_COMPRESS\n", zr->name);
		break;

	case BUZ_MODE_MOTION_DECOMPRESS:
		/* In motion decompression mode, the decoder output must be disabled, and
		 * the video bus direction set to output.
		 */
		// set_videobus_enable(zr, 0);
		decoder_command(zr, DECODER_ENABLE_OUTPUT, &zero);
		set_videobus_dir(zr, 1);
		encoder_command(zr, ENCODER_SET_INPUT, &one);
		// set_videobus_enable(zr, 1);

		/* Take the JPEG codec and the VFE out of sleep */
		jpeg_codec_sleep(zr, 0);
		/* Setup the VFE */
		if (zr->vfe) {
			zr->vfe->set_video(zr->vfe, zr->timing,
				           &cap, &zr->card->vfe_pol);
			zr->vfe->set_mode(zr->vfe, CODEC_DO_EXPANSION);
		}
		/* Setup the JPEG codec */
		zr->codec->set_video(zr->codec, zr->timing,
				     &cap, &zr->card->vfe_pol);
		zr->codec->set_mode(zr->codec, CODEC_DO_EXPANSION);

		init_jpeg_queue(zr);
		zr36057_set_jpg(zr, mode);	// \P_Reset, ... Video param, FIFO

	        clear_interrupt_counters(zr);
		dprintk(1, KERN_INFO "%s: enable_jpg MOTION_DECOMPRESS\n", zr->name);
		break;

	case BUZ_MODE_IDLE:
	default:
		/* shut down processing */
		btand(~(zr->card->jpeg_int | ZR36057_ICR_JPEGRepIRQ), ZR36057_ICR);
		btwrite(zr->card->jpeg_int | ZR36057_ICR_JPEGRepIRQ, ZR36057_ISR);
		btand(~ZR36057_JMC_Go_en, ZR36057_JMC);	// \Go_en

		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(HZ / 20);

		set_videobus_dir(zr, 0);
		set_frame(zr, 1); // /FRAME
                btor(ZR36057_MCTCR_CFlush, ZR36057_MCTCR);	// /CFlush
		btwrite(0, ZR36057_JPC);	// \P_Reset,\CodTrnsEn,\Active
		btand(~ZR36057_JMC_VFIFO_FB, ZR36057_JMC);
		btand(~ZR36057_JMC_SyncMstr, ZR36057_JMC);
		jpeg_codec_reset(zr);
		jpeg_codec_sleep(zr, 1);
		zr36057_adjust_vfe(zr, mode);

		// set_videobus_enable(zr, 0);
		decoder_command(zr, DECODER_ENABLE_OUTPUT, &one);
		encoder_command(zr, ENCODER_SET_INPUT, &zero);
		// set_videobus_enable(zr, 1);
		dprintk(1, KERN_INFO "%s: enable_jpg IDLE\n", zr->name);
		break;

	}
}

/*
 *   Queue a MJPEG buffer for capture/playback
 */

static int
zoran_jpg_queue_frame (struct file           *file,
                       int                    num,
                       enum zoran_codec_mode  mode)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int res = 0;

	/* Check if buffers are allocated */
	if (!fh->jpg_buffers.allocated) {
		printk(KERN_ERR "%s: jpg_qbuf: buffers not yet allocated\n", zr->name);
		return -ENOMEM;
	}

	/* No grabbing outside the buffer range! */
	if (num >= fh->jpg_buffers.num_buffers || num < 0) {
		printk(KERN_ERR "%s: jpg_qbuf: buffer %d out of range\n", zr->name, num);
		return -EINVAL;
	}

	/* what is the codec mode right now? */
	if (zr->codec_mode == BUZ_MODE_IDLE) {
		zr->jpg_settings = fh->jpg_settings;
	} else if (zr->codec_mode != mode) {
		/* wrong codec mode active - invalid */
		printk(KERN_ERR "%s: jpg_qbuf - codec in wrong mode\n", zr->name);
		return -EINVAL;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (fh->jpg_buffers.active == ZORAN_FREE) {
		if (zr->jpg_buffers.active == ZORAN_FREE) {
			zr->jpg_buffers = fh->jpg_buffers;
			fh->jpg_buffers.active = ZORAN_ACTIVE;
		}
		else {
			printk(KERN_ERR "%s: jpg_qbuf(): another session is already capturing\n", zr->name);
			res = -EBUSY;
		}
	}

	if (!res && zr->codec_mode == BUZ_MODE_IDLE) {
		/* Ok load up the jpeg codec */
		zr36057_enable_jpg(zr, mode);
	}

	if (!res) {
		switch (zr->jpg_buffers.buffer[num].state) {
			case BUZ_STATE_DONE:
				dprintk(1, KERN_WARNING "%s: Warning: queing frame in BUZ_STATE_DONE state\n",
					zr->name);
			case BUZ_STATE_USER:
				/* since there is at least one unused buffer there's room for at
				 *least one more pend[] entry */
				zr->jpg_pend[zr->jpg_que_head++ & BUZ_MASK_FRAME] = num;
				zr->jpg_buffers.buffer[num].state = BUZ_STATE_PEND;
				fh->jpg_buffers.buffer[num] = zr->jpg_buffers.buffer[num];
				zoran_feed_stat_com(zr);
				break;
			default:
			case BUZ_STATE_DMA:
			case BUZ_STATE_PEND:
				if (zr->jpg_buffers.active == ZORAN_FREE) {
					fh->jpg_buffers.active = ZORAN_FREE;
					zr->jpg_buffers.allocated = 0;
				}
				res = -EBUSY;	/* what are you doing? */
				break;
		}
	}

	spin_unlock_irqrestore(&zr->spinlock, flags);

	if (!res && zr->jpg_buffers.active == ZORAN_FREE) {
		zr->jpg_buffers.active = fh->jpg_buffers.active;
	}

	return res;
}

static int
jpg_qbuf (struct file          *file,
          int                   frame,
          enum zoran_codec_mode mode)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int res=0;

	/* Does the user want to stop streaming? */
	if (frame < 0) {
		if (zr->codec_mode == mode) {
			if (fh->jpg_buffers.active == ZORAN_FREE) {
				printk(KERN_ERR "%s: jpg_qbuf(-1): session not active\n", zr->name);
				return -EINVAL;
			}
			fh->jpg_buffers.active = zr->jpg_buffers.active = ZORAN_FREE;
			zr->jpg_buffers.allocated = 0;
			zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
			return 0;
		} else {
			printk(KERN_ERR "%s: jpg_qbuf - stop streaming but not in streaming mode\n", zr->name);
			return -EINVAL;
		}
	}

	if ((res = zoran_jpg_queue_frame(file, frame, mode)))
		return res;

	/* Start the jpeg codec when the first frame is queued  */
	if (!res && zr->jpg_que_head == 1)
		jpeg_start(zr);

	return res;
}

/*
 *   Sync on a MJPEG buffer
 */

static int
jpg_sync (struct file       *file,
          struct zoran_sync *bs)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long flags;
	int frame, timeout;

	if (fh->jpg_buffers.active == ZORAN_FREE) {
		printk(KERN_ERR "%s: jpg_sync: capture is not currently active\n", zr->name);
		return -EINVAL;
	}
	if (zr->codec_mode != BUZ_MODE_MOTION_DECOMPRESS && zr->codec_mode != BUZ_MODE_MOTION_COMPRESS) {
		printk(KERN_ERR "%s: BUZIOCSYNC: - codec not in streaming mode\n", zr->name);
		return -EINVAL;
	}
	while (zr->jpg_que_tail == zr->jpg_dma_tail) {
		if (zr->jpg_dma_tail == zr->jpg_dma_head)
			break;

		timeout = interruptible_sleep_on_timeout(&zr->jpg_capq, 10 * HZ);
		if (!timeout) {
			int isr;

			btand(~ZR36057_JMC_Go_en, ZR36057_JMC);
			udelay(1);
			zr->codec->control(zr->codec, CODEC_G_STATUS, sizeof(isr), &isr);
			printk(KERN_ERR "%s: timeout: codec isr=0x%02x\n", zr->name, isr);

			return -ETIME;

		} else if (signal_pending(current))
			return -ERESTARTSYS;
	}

	spin_lock_irqsave(&zr->spinlock, flags);

	if (zr->jpg_dma_tail != zr->jpg_dma_head)
		frame = zr->jpg_pend[zr->jpg_que_tail++ & BUZ_MASK_FRAME];
	else
		frame = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];

	/* buffer should now be in BUZ_STATE_DONE */
	if (debug > 0)
		if (zr->jpg_buffers.buffer[frame].state != BUZ_STATE_DONE)
			printk(KERN_ERR "%s: jpg_sync - internal error\n", zr->name);

	*bs = zr->jpg_buffers.buffer[frame].bs;
	bs->frame = frame;
	zr->jpg_buffers.buffer[frame].state = BUZ_STATE_USER;
	fh->jpg_buffers.buffer[frame] = zr->jpg_buffers.buffer[frame];

	spin_unlock_irqrestore(&zr->spinlock, flags);

	return 0;
}

/* when this is called the spinlock must be held */
static void zoran_feed_stat_com(struct zoran *zr)
{
	/* move frames from pending queue to DMA */

	int frame, i, max_stat_com;

	max_stat_com = (zr->jpg_settings.TmpDcm == 1) ? BUZ_NUM_STAT_COM : (BUZ_NUM_STAT_COM >> 1);

	while ((zr->jpg_dma_head - zr->jpg_dma_tail) < max_stat_com && zr->jpg_dma_head < zr->jpg_que_head) {

		frame = zr->jpg_pend[zr->jpg_dma_head & BUZ_MASK_FRAME];
		if (zr->jpg_settings.TmpDcm == 1) {
			/* fill 1 stat_com entry */
			i = (zr->jpg_dma_head - zr->jpg_err_shift) & BUZ_MASK_STAT_COM;
			if (!(zr->stat_com[i] & 1))
				break;
			zr->stat_com[i] = zr->jpg_buffers.buffer[frame].frag_tab_bus;
		} else {
			/* fill 2 stat_com entries */
			i = ((zr->jpg_dma_head - zr->jpg_err_shift) & 1) * 2;
			if (!(zr->stat_com[i] & 1))
				break;
			zr->stat_com[i] = zr->jpg_buffers.buffer[frame].frag_tab_bus;
			zr->stat_com[i + 1] = zr->jpg_buffers.buffer[frame].frag_tab_bus;
		}
		zr->jpg_buffers.buffer[frame].state = BUZ_STATE_DMA;
		zr->jpg_dma_head++;

	}
	if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS)
		zr->jpg_queued_num++;
}

/* when this is called the spinlock must be held */
static void zoran_reap_stat_com(struct zoran *zr)
{
	/* move frames from DMA queue to done queue */

	int i;
	u32 stat_com;
	unsigned int seq;
	unsigned int dif;
	struct zoran_jpg_buffer *buffer;
	int frame;

	/* In motion decompress we don't have a hardware frame counter,
	   we just count the interrupts here */

	if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS) {
		zr->jpg_seq_num++;
	}
	while (zr->jpg_dma_tail < zr->jpg_dma_head) {
		if (zr->jpg_settings.TmpDcm == 1)
			i = (zr->jpg_dma_tail - zr->jpg_err_shift) & BUZ_MASK_STAT_COM;
		else
			i = ((zr->jpg_dma_tail - zr->jpg_err_shift) & 1) * 2 + 1;

		stat_com = zr->stat_com[i];

		if ((stat_com & 1) == 0) {
			return;
		}
		frame = zr->jpg_pend[zr->jpg_dma_tail & BUZ_MASK_FRAME];
		buffer = &zr->jpg_buffers.buffer[frame];
		do_gettimeofday(&buffer->bs.timestamp);

		if (zr->codec_mode == BUZ_MODE_MOTION_COMPRESS) {
			buffer->bs.length = (stat_com & 0x7fffff) >> 1;

			/* update sequence number with the help of the counter in stat_com */

			seq = ((stat_com >> 24) + zr->jpg_err_seq) & 0xff;
			dif = (seq - zr->jpg_seq_num) & 0xff;
			zr->jpg_seq_num += dif;
		} else {
			buffer->bs.length = 0;
		}
		buffer->bs.seq = zr->jpg_settings.TmpDcm == 2 ? (zr->jpg_seq_num >> 1) : zr->jpg_seq_num;
		buffer->state = BUZ_STATE_DONE;

		zr->jpg_dma_tail++;
	}
}

static void error_handler(struct zoran *zr, u32 astat, u32 stat)
{
	/* This is JPEG error handling part */
	if ((zr->codec_mode != BUZ_MODE_MOTION_COMPRESS)
	    && (zr->codec_mode != BUZ_MODE_MOTION_DECOMPRESS)) {
		//printk(KERN_ERR "%s: Internal error: error handling request in mode %d\n", zr->name, zr->codec_mode);
		return;
	}
        if ((stat & 1) == 0
         && zr->codec_mode == BUZ_MODE_MOTION_COMPRESS
         && zr->jpg_dma_tail - zr->jpg_que_tail >= zr->jpg_buffers.num_buffers) {
                /* No free buffers... */
		zoran_reap_stat_com(zr);
		zoran_feed_stat_com(zr);
		wake_up_interruptible(&zr->jpg_capq);
                zr->JPEG_missed = 0;
                return;
        }
	if (zr->JPEG_error != 1) {
		/*
		 * First entry: error just happened during normal operation
		 * 
		 * In BUZ_MODE_MOTION_COMPRESS:
		 * 
		 * Possible glitch in TV signal. In this case we should
		 * stop the codec and wait for good quality signal before
		 * restarting it to avoid further problems
		 * 
		 * In BUZ_MODE_MOTION_DECOMPRESS:
		 * 
		 * Bad JPEG frame: we have to mark it as processed (codec crashed
		 * and was not able to do it itself), and to remove it from queue.
		 */
		btand(~ZR36057_JMC_Go_en, ZR36057_JMC);
                udelay(1);
/*TODO*/                stat = stat | (post_office_read(zr, 7, 0) & 3) << 8; // | jpeg_codec_read_8(zr, 0x008);
		btwrite(0, ZR36057_JPC);
		btor(ZR36057_MCTCR_CFlush, ZR36057_MCTCR);
		jpeg_codec_reset(zr);
		jpeg_codec_sleep(zr, 1);
		zr->JPEG_error = 1;
		zr->num_errors++;

		/* Report error */
		if (debug > 1 && zr->num_errors <= 8) {
			long frame;
			frame = zr->jpg_pend[zr->jpg_dma_tail & BUZ_MASK_FRAME];
			printk(KERN_ERR "%s: JPEG error stat=0x%08x(0x%08x) queue_state=%ld/%ld/%ld/%ld seq=%ld frame=%ld. Codec stopped. ",
			       zr->name, stat, zr->last_isr, zr->jpg_que_tail, zr->jpg_dma_tail, zr->jpg_dma_head, zr->jpg_que_head,
			       zr->jpg_seq_num, frame);
			printk("stat_com frames:");
			{
				int i, j;
				for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
					for (i = 0; i < zr->jpg_buffers.num_buffers; i++) {
						if (zr->stat_com[j] == zr->jpg_buffers.buffer[i].frag_tab_bus) {
							printk("% d->%d", j, i);
						}
					}
				}
				printk("\n");
			}
		}

		/* Find an entry in stat_com and rotate contents */
		{
			int i;

			if (zr->jpg_settings.TmpDcm == 1)
				i = (zr->jpg_dma_tail - zr->jpg_err_shift) & BUZ_MASK_STAT_COM;
			else
				i = ((zr->jpg_dma_tail - zr->jpg_err_shift) & 1) * 2;
			if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS) {
				/* Mimic zr36067 operation */
				zr->stat_com[i] |= 1;
				if (zr->jpg_settings.TmpDcm != 1)
					zr->stat_com[i + 1] |= 1;
				/* Refill */
				zoran_reap_stat_com(zr);
				zoran_feed_stat_com(zr);
				wake_up_interruptible(&zr->jpg_capq);
				/* Find an entry in stat_com again after refill */
				if (zr->jpg_settings.TmpDcm == 1)
					i = (zr->jpg_dma_tail - zr->jpg_err_shift) & BUZ_MASK_STAT_COM;
				else
					i = ((zr->jpg_dma_tail - zr->jpg_err_shift) & 1) * 2;
			}
			if (i) {
				/* Rotate stat_comm entries to make current entry first */
				int j;
				u32 bus_addr[BUZ_NUM_STAT_COM];

				memcpy(bus_addr, zr->stat_com, sizeof(bus_addr));
				for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
					zr->stat_com[j] = bus_addr[(i + j) & BUZ_MASK_STAT_COM];
				}
				zr->jpg_err_shift += i;
				zr->jpg_err_shift &= BUZ_MASK_STAT_COM;
			}
			if (zr->codec_mode == BUZ_MODE_MOTION_COMPRESS)
				zr->jpg_err_seq = zr->jpg_seq_num;	/* + 1; */
		}
	}
	/* Now the stat_comm buffer is ready for restart */
	{
		int status;

		status = 0;
		if (zr->codec_mode == BUZ_MODE_MOTION_COMPRESS)
			decoder_command(zr, DECODER_GET_STATUS, &status);
		if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS || (status & DECODER_STATUS_GOOD)) {
	    /********** RESTART code *************/
			jpeg_codec_reset(zr);
			zr->codec->set_mode(zr->codec, CODEC_DO_EXPANSION);
			zr36057_set_jpg(zr, zr->codec_mode);
			jpeg_start(zr);

			if (debug > 1 && zr->num_errors <= 8)
				printk(KERN_INFO "%s: Restart\n", zr->name);

			zr->JPEG_missed = 0;
			zr->JPEG_error = 2;
	    /********** End RESTART code ***********/
		}
	}
}

static void zoran_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	u32 stat, astat;
	int count;
	struct zoran *zr;
	unsigned long flags;

	zr = (struct zoran *) dev_id;
	count = 0;

	if (zr->testing) {
		/* Testing interrupts */
		spin_lock_irqsave(&zr->spinlock, flags);
		while ((stat = count_reset_interrupt(zr))) {
			if (count++ > 100) {
				btand(~ZR36057_ICR_IntPinEn, ZR36057_ICR);
				printk(KERN_ERR "%s: IRQ lockup while testing, isr=0x%08x, cleared int mask\n",
				       zr->name, stat);
				wake_up_interruptible(&zr->test_q);
			}
		}
		zr->last_isr = stat;
		spin_unlock_irqrestore(&zr->spinlock, flags);
                return;
	}

	spin_lock_irqsave(&zr->spinlock, flags);
	while (1) {
		/* get/clear interrupt status bits */
		stat = count_reset_interrupt(zr);
		astat = stat & IRQ_MASK;
		if (!astat) {
			break;
		}
//		dprintk(4, KERN_DEBUG "zoran_irq: astat: 0x%08x, mask: 0x%08x\n", astat, btread(ZR36057_ICR));
		if (astat & zr->card->vsync_int) {	// SW

			if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS || zr->codec_mode == BUZ_MODE_MOTION_COMPRESS) {
				/* count missed interrupts */
				zr->JPEG_missed++;
			}
			//post_office_read(zr,1,0);
			/* Interrupts may still happen when zr->v4l_memgrab_active is switched off.
			   We simply ignore them */

			if (zr->v4l_memgrab_active) {

/* A lot more checks should be here ... */
				if ((btread(ZR36057_VSSFGR) & ZR36057_VSSFGR_SnapShot) == 0)
					printk(KERN_WARNING "%s: BuzIRQ with SnapShot off ???\n", zr->name);

				if (zr->v4l_grab_frame != NO_GRAB_ACTIVE) {
					/* There is a grab on a frame going on, check if it has finished */

					if ((btread(ZR36057_VSSFGR) & ZR36057_VSSFGR_FrameGrab) == 0) {
						/* it is finished, notify the user */

						zr->v4l_buffers.buffer[zr->v4l_grab_frame].state = BUZ_STATE_DONE;
						zr->v4l_buffers.buffer[zr->v4l_grab_frame].bs.seq = zr->v4l_grab_seq;
						do_gettimeofday(&zr->v4l_buffers.buffer[zr->v4l_grab_frame].bs.timestamp);
						zr->v4l_grab_frame = NO_GRAB_ACTIVE;
						zr->v4l_grab_seq++;
						zr->v4l_pend_tail++;
					}
				}

				if (zr->v4l_grab_frame == NO_GRAB_ACTIVE)
					wake_up_interruptible(&zr->v4l_capq);

				/* Check if there is another grab queued */

				if (zr->v4l_grab_frame == NO_GRAB_ACTIVE && zr->v4l_pend_tail != zr->v4l_pend_head) {

					int frame = zr->v4l_pend[zr->v4l_pend_tail & V4L_MASK_FRAME];
					u32 reg;

					zr->v4l_grab_frame = frame;

					/* Set zr36057 video front end and enable video */

					/* Buffer address */

					reg = zr->v4l_buffers.buffer[frame].fbuffer_bus;
					btwrite(reg, ZR36057_VDTR);
					if (zr->v4l_settings.height > BUZ_MAX_HEIGHT / 2)
						reg += zr->v4l_settings.bytesperline;
					btwrite(reg, ZR36057_VDBR);

					/* video stride, status, and frame grab register */

#ifdef XAWTV_HACK
					reg = (zr->v4l_settings.width > 720) ? ((zr->v4l_settings.width & ~3) - 720) * zr->v4l_settings.bytesperline / zr->v4l_settings.width : 0;
#else
					reg = 0;
#endif
					if (zr->v4l_settings.height > BUZ_MAX_HEIGHT / 2)
						reg += zr->v4l_settings.bytesperline;
					reg = (reg << ZR36057_VSSFGR_DispStride);
					reg |= ZR36057_VSSFGR_VidOvf;
					reg |= ZR36057_VSSFGR_SnapShot;
					reg |= ZR36057_VSSFGR_FrameGrab;
					btwrite(reg, ZR36057_VSSFGR);

					btor(ZR36057_VDCR_VidEn, ZR36057_VDCR);
				}
			}
		}

#if (IRQ_MASK & ZR36057_ISR_CodRepIRQ)
		if (astat & ZR36057_ISR_CodRepIRQ) {
			zr->intr_counter_CodRepIRQ++;
			IDEBUG(printk(KERN_DEBUG "%s: ZR36057_ISR_CodRepIRQ\n", zr->name));
			btand(~ZR36057_ICR_CodRepIRQ, ZR36057_ICR);
		}
#endif				/* (IRQ_MASK & ZR36057_ISR_CodRepIRQ) */

#if (IRQ_MASK & ZR36057_ISR_JPEGRepIRQ)
		if (astat & ZR36057_ISR_JPEGRepIRQ) {

			if (zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS || zr->codec_mode == BUZ_MODE_MOTION_COMPRESS) {
				if (debug > 1 && (!zr->frame_num || zr->JPEG_error)) {
					printk(KERN_INFO
					       "%s: first frame ready: state=0x%08x odd_even=%d field_per_buff=%d delay=%d\n",
					       zr->name, stat, zr->jpg_settings.odd_even, zr->jpg_settings.field_per_buff,
					       zr->JPEG_missed);
					{
						char sc[] = "0000";
						char sv[5];
						int i;
						strcpy(sv, sc);
						for (i = 0; i < 4; i++) {
							if (zr->stat_com[i] & 1)
								sv[i] = '1';
						}
						sv[4] = 0;
						printk(KERN_INFO "%s: stat_com=%s queue_state=%ld/%ld/%ld/%ld\n", zr->name, sv,
						       zr->jpg_que_tail, zr->jpg_dma_tail, zr->jpg_dma_head, zr->jpg_que_head);
					}
				} else {
					if (zr->JPEG_missed > zr->JPEG_max_missed)	// Get statistics
						zr->JPEG_max_missed = zr->JPEG_missed;
					if (zr->JPEG_missed < zr->JPEG_min_missed)
						zr->JPEG_min_missed = zr->JPEG_missed;
				}

				if (debug > 2 && zr->frame_num < 6) {
					int i;
					printk("%s: seq=%ld stat_com:", zr->name, zr->jpg_seq_num);
					for (i = 0; i < 4; i++) {
						printk(" %08x", zr->stat_com[i]);
					}
					printk("\n");
				}
				zr->frame_num++;
				zr->JPEG_missed = 0;
				zr->JPEG_error = 0;
				zoran_reap_stat_com(zr);
				zoran_feed_stat_com(zr);
				wake_up_interruptible(&zr->jpg_capq);
			} //else {
			//	printk(KERN_ERR "%s: JPEG interrupt while not in motion (de)compress mode!\n", zr->name);
			//}
		}
#endif				/* (IRQ_MASK & ZR36057_ISR_JPEGRepIRQ) */

		if ((astat & zr->card->jpeg_int)	/* DATERR interrupt received                 */
		    || zr->JPEG_missed > 25	/* Too many fields missed without processing */
		    || zr->JPEG_error == 1	/* We are already in error processing        */
		    || ((zr->codec_mode == BUZ_MODE_MOTION_DECOMPRESS)
			&& (zr->frame_num & (zr->JPEG_missed > zr->jpg_settings.field_per_buff)))
		    /* fields missed during decompression */
		    ) {
			error_handler(zr, astat, stat);
		}

		count++;
		if (count > 10) {
			printk(KERN_WARNING "%s: irq loop %d\n", zr->name, count);
			if (count > 20) {
				btand(~ZR36057_ICR_IntPinEn, ZR36057_ICR);
				printk(KERN_ERR "%s: IRQ lockup, cleared int mask\n", zr->name);
				break;
			}
		}
	        zr->last_isr = stat;
	}
	spin_unlock_irqrestore(&zr->spinlock, flags);
}

/* Check a zoran_params struct for correctness, insert default params */

static int
zoran_check_jpg_settings (struct zoran              *zr,
                          struct zoran_jpg_settings *settings)
{
	int err = 0, err0 = 0;

	dprintk(4, KERN_DEBUG "%s: zoran_check_jpg_settings: dec: %d, Hdcm: %d, Vdcm: %d, Tdcm: %d\n",
		      zr->name, settings->decimation, settings->HorDcm, settings->VerDcm, settings->TmpDcm);
	dprintk(4, KERN_DEBUG "%s: zoran_check_jpg_settings: x: %d, y: %d, w: %d, y: %d\n",
		      zr->name, settings->img_x, settings->img_y, settings->img_width, settings->img_height);

	/* Check decimation, set default values for decimation = 1, 2, 4 */
	switch (settings->decimation) {
	case 1:

		settings->HorDcm = 1;
		settings->VerDcm = 1;
		settings->TmpDcm = 1;
		settings->field_per_buff = 2;

		settings->img_x = 0;
		settings->img_y = 0;
		settings->img_width = BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;

	case 2:

		settings->HorDcm = 2;
		settings->VerDcm = 1;
		settings->TmpDcm = 2;
		settings->field_per_buff = 1;

		settings->img_x = (BUZ_MAX_WIDTH==720)?8:0;
		settings->img_y = 0;
		settings->img_width = (BUZ_MAX_WIDTH==720)?704:BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;

	case 4:

		if (zr->card->type == DC10)
		{
			printk(KERN_DEBUG "%s: HDec by 4 is not supported on the DC10\n", zr->name);
			err0++;
			break;
		}

		settings->HorDcm = 4;
		settings->VerDcm = 2;
		settings->TmpDcm = 2;
		settings->field_per_buff = 1;

		settings->img_x = (BUZ_MAX_WIDTH==720)?8:0;
		settings->img_y = 0;
		settings->img_width = (BUZ_MAX_WIDTH==720)?704:BUZ_MAX_WIDTH;
		settings->img_height = BUZ_MAX_HEIGHT / 2;
		break;

	case 0:

		/* We have to check the data the user has set */

		if (settings->HorDcm != 1 && settings->HorDcm != 2 && (zr->card->type == DC10 || settings->HorDcm != 4))
			err0++;
		if (settings->VerDcm != 1 && settings->VerDcm != 2)
			err0++;
		if (settings->TmpDcm != 1 && settings->TmpDcm != 2)
			err0++;
		if (settings->field_per_buff != 1 && settings->field_per_buff != 2)
			err0++;

		if (settings->img_x < 0)
			err0++;
		if (settings->img_y < 0)
			err0++;
		if (settings->img_width < 0)
			err0++;
		if (settings->img_height < 0)
			err0++;
		if (settings->img_x + settings->img_width > BUZ_MAX_WIDTH)
			err0++;
		if (settings->img_y + settings->img_height > BUZ_MAX_HEIGHT / 2)
			err0++;
		if (settings->HorDcm) {
			if (settings->img_width % (16 * settings->HorDcm) != 0)
				err0++;
			if (settings->img_height % (8 * settings->VerDcm) != 0)
				err0++;
		}

		if (err0) {
			printk(KERN_ERR "%s: SET PARAMS: error in params for decimation = 0\n", zr->name);
			err++;
		}
		break;

	default:
		printk(KERN_ERR "%s: SET PARAMS: decimation = %d, must be 0, 1, 2 or 4\n", zr->name, settings->decimation);
		err++;
		break;
	}

	if (settings->jpg_comp.quality > 100)
		settings->jpg_comp.quality = 100;
	if (settings->jpg_comp.quality < 5)
		settings->jpg_comp.quality = 5;

	if (settings->jpg_comp.APPn < 0)
		settings->jpg_comp.APPn = 0;
	if (settings->jpg_comp.APPn > 15)
		settings->jpg_comp.APPn = 15;
	if (settings->jpg_comp.APP_len < 0)
		settings->jpg_comp.APP_len = 0;
	if (settings->jpg_comp.APP_len > 60)
		settings->jpg_comp.APP_len = 60;
	if (settings->jpg_comp.COM_len < 0)
		settings->jpg_comp.COM_len = 0;
	if (settings->jpg_comp.COM_len > 60)
		settings->jpg_comp.COM_len = 60;

	if (err)
		return -EINVAL;

	return 0;

}

static void
zoran_open_init_params (struct zoran *zr)
{
	int i;

	/* User must explicitly set a window */
	memset(&zr->overlay_settings, 0, sizeof(struct zoran_overlay_settings));
        zr->overlay_settings.is_set = 0;
	zr->overlay_mask = NULL;
	zr->overlay_active = ZORAN_FREE;

	zr->v4l_memgrab_active = 0;
	zr->v4l_overlay_active = 0;

	zr->v4l_grab_frame = NO_GRAB_ACTIVE;
	zr->v4l_grab_seq = 0;

	zr->v4l_settings.width = 192;
	zr->v4l_settings.height = 144;
	zr->v4l_settings.format = &zoran_formats[4]; /* YUY2 - YUV-4:2:2 packed */
	zr->v4l_settings.bytesperline = zr->v4l_settings.width * ((zr->v4l_settings.format->depth+7)/8);

	/* DMA ring stuff for V4L */
	zr->v4l_pend_tail = 0;
	zr->v4l_pend_head = 0;
	zr->v4l_buffers.active = ZORAN_FREE;
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		zr->v4l_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
	}
	zr->v4l_buffers.allocated = 0;
	zr->v4l_buffers.secretly_allocated = 0;

	for (i = 0; i < BUZ_MAX_FRAME; i++) {
		zr->jpg_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
	}
	zr->jpg_buffers.active = ZORAN_FREE;
	zr->jpg_buffers.allocated = 0;
	zr->jpg_buffers.secretly_allocated = 0;

	/* Set necessary params and call zoran_check_jpg_settings to set the defaults */
	zr->jpg_settings.decimation = 1;

	zr->jpg_settings.jpg_comp.quality = 50;	/* default compression factor 8 */
	if (zr->card->type != BUZ)
                zr->jpg_settings.odd_even = 1;
        else
                zr->jpg_settings.odd_even = 0;

	zr->jpg_settings.jpg_comp.APPn = 0;
	zr->jpg_settings.jpg_comp.APP_len = 0;	/* No APPn marker */
	memset(zr->jpg_settings.jpg_comp.APP_data, 0, sizeof(zr->jpg_settings.jpg_comp.APP_data));

	zr->jpg_settings.jpg_comp.COM_len = 0;	/* No COM marker */
	memset(zr->jpg_settings.jpg_comp.COM_data, 0, sizeof(zr->jpg_settings.jpg_comp.COM_data));

	zr->jpg_settings.jpg_comp.jpeg_markers = JPEG_MARKER_DHT | JPEG_MARKER_DQT;

	i = zoran_check_jpg_settings(zr, &zr->jpg_settings);
	if (i)
		printk(KERN_ERR "%s: zoran_open_init_params internal error\n", zr->name);

	clear_interrupt_counters(zr);
	zr->testing = 0;
}

static void
zoran_open_init_session (struct file *file)
{
	int i;
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* Per default, map the V4L Buffers */
	fh->map_mode = ZORAN_MAP_MODE_RAW;

	/* take over the card's current settings */
	fh->overlay_settings = zr->overlay_settings;
	fh->overlay_settings.is_set = 0;
	fh->overlay_active = ZORAN_FREE;

	/* v4l settings */
	fh->v4l_settings = zr->v4l_settings;

	/* v4l_buffers */
	memset(&fh->v4l_buffers, 0, sizeof(struct zoran_v4l_struct));
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		fh->v4l_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
		fh->v4l_buffers.buffer[i].bs.frame = i;
	}
	fh->v4l_buffers.allocated = 0;
	fh->v4l_buffers.secretly_allocated = 0;
	fh->v4l_buffers.active = ZORAN_FREE;
	fh->v4l_buffers.buffer_size = v4l_bufsize;
	fh->v4l_buffers.num_buffers = v4l_nbufs;

	/* jpg settings */
	fh->jpg_settings = zr->jpg_settings;

	/* jpg_buffers */
	memset(&fh->jpg_buffers, 0, sizeof(struct zoran_jpg_struct));
	for (i = 0; i < BUZ_MAX_FRAME; i++) {
		fh->jpg_buffers.buffer[i].state = BUZ_STATE_USER;	/* nothing going on */
		fh->jpg_buffers.buffer[i].bs.frame = i;
	}
	fh->jpg_buffers.need_contiguous = zr->jpg_buffers.need_contiguous;
	fh->jpg_buffers.allocated = 0;
	fh->jpg_buffers.secretly_allocated = 0;
	fh->jpg_buffers.active = ZORAN_FREE;
}

static void
zoran_close_end_session (struct file *file)
{
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* overlay */
	if (fh->overlay_active != ZORAN_FREE) {
		fh->overlay_active = zr->overlay_active = ZORAN_FREE;
		zr->v4l_overlay_active = 0;
		if (!zr->v4l_memgrab_active)
			zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;
	}

	/* v4l capture */
	if (fh->v4l_buffers.active != ZORAN_FREE) {
		zr36057_set_memgrab(zr, 0);
		zr->v4l_buffers.allocated = 0;
		zr->v4l_buffers.active = fh->v4l_buffers.active = ZORAN_FREE;
	}

	/* v4l buffers */
	if (fh->v4l_buffers.allocated || fh->v4l_buffers.secretly_allocated) {
		v4l_fbuffer_free(file);
	}

	/* jpg capture */
	if (fh->jpg_buffers.active != ZORAN_FREE) {
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
		zr->jpg_buffers.allocated = 0;
		zr->jpg_buffers.active = fh->jpg_buffers.active = ZORAN_FREE;
	}

	/* jpg buffers */
	if (fh->jpg_buffers.allocated || fh->jpg_buffers.secretly_allocated) {
		jpg_fbuffer_free(file);
	}
}

static void zr36057_restart (struct zoran *zr)
{
        btwrite(0, ZR36057_SPGPPCR);
	mdelay(1);
	btor(ZR36057_SPGPPCR_SoftReset, ZR36057_SPGPPCR);
	mdelay(1);

	/* assert P_Reset */
	btwrite(0, ZR36057_JPC);
	/* set up GPIO direction - all output */
	btwrite(ZR36057_SPGPPCR_SoftReset | 0, ZR36057_SPGPPCR);

	/* set up GPIO pins and guest bus timing */
        btwrite((0x81 << 24) | 0x8888, ZR36057_GPPGCR1);
}

/*
 * initialize video front end
 */
static void zr36057_init_vfe(struct zoran *zr)
{
	u32 reg;
	reg = btread(ZR36057_VFESPFR);
	reg |= ZR36057_VFESPFR_LittleEndian;
	reg &= ~ZR36057_VFESPFR_VCLKPol;
	reg |= ZR36057_VFESPFR_ExtFl;
	reg |= ZR36057_VFESPFR_TopField;
	btwrite(reg, ZR36057_VFESPFR);
	reg = btread(ZR36057_VDCR);
        if (triton) // || zr->revision < 1) // Revision 1 has also Triton support
		reg &= ~ZR36057_VDCR_Triton;
	else
		reg |= ZR36057_VDCR_Triton;
	btwrite(reg, ZR36057_VDCR);
}

static void zoran_set_pci_master(struct zoran *zr, int set_master)
{
        u16  command;
        if (set_master) {
                pci_set_master(zr->pci_dev);
        } else {
		pci_read_config_word(zr->pci_dev, PCI_COMMAND, &command);
		command &= ~PCI_COMMAND_MASTER;
		pci_write_config_word(zr->pci_dev, PCI_COMMAND, command);
        }
}

static void zoran_init_hardware (struct zoran *zr)
{
        int j, zero = 0;
        /* Enable bus-mastering */
	zoran_set_pci_master(zr, 1);

	/* Initialize the board */
	if (zr->card->init) {
		zr->card->init(zr);
	}

	j = zr->card->input[zr->input].muxsel;
        
	decoder_command(zr, 0, NULL);
	decoder_command(zr, DECODER_SET_NORM, &zr->norm);
	decoder_command(zr, DECODER_SET_INPUT, &j);
        
	encoder_command(zr, 0, NULL);
	encoder_command(zr, ENCODER_SET_NORM, &zr->norm);
	encoder_command(zr, ENCODER_SET_INPUT, &zero);

	/* toggle JPEG codec sleep to sync PLL */
	jpeg_codec_sleep(zr, 1);
	jpeg_codec_sleep(zr, 0);

	/* set individual interrupt enables (without GIRQ1)
	   but don't global enable until zoran_open() */

	//btwrite(IRQ_MASK & ~ZR36057_ISR_GIRQ1, ZR36057_ICR);  // SW
	// It looks like using only JPEGRepIRQEn is not always reliable,
	// may be when JPEG codec crashes it won't generate IRQ? So,
/*CP*///	btwrite(IRQ_MASK, ZR36057_ICR);	// Enable Vsync interrupts too. SM    WHY ? LP

	zr36057_init_vfe(zr);

	zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
	
        btwrite(IRQ_MASK, ZR36057_ISR);	// Clears interrupts
}

/*
 *   Open a zoran card. Right now the flags stuff is just playing
 */

static int
zoran_open (struct inode *inode,
            struct file  *file)
{
	unsigned int minor = minor(inode->i_rdev);
	struct zoran *zr = NULL;
	struct zoran_fh *fh;
	int i;

	/* find the device */
	for (i=0;i<zoran_num;i++)
	{
		if (zoran[i].video_dev.minor == minor) {
			zr = &zoran[i];
			break;
		}
	}
	if (!zr)
		return -ENODEV;

	if (zr->user >= 2048)
		return -EBUSY;

	dprintk(1, KERN_INFO "%s: zoran_open (%s - pid=[%d]), users(-)=%d\n",
		zr->name, current->comm, current->pid, zr->user);

	/* now, create the open()-specific file_ops struct */
	fh = kmalloc(sizeof(struct zoran_fh), GFP_KERNEL);
	if (!fh) {
		printk(KERN_ERR "%s: allocation of zoran_fh failed\n", zr->name);
		return -ENOMEM;
	}
	memset(fh, 0, sizeof(struct zoran_fh));
	fh->overlay_mask = kmalloc(((BUZ_MAX_WIDTH + 31) / 32) * BUZ_MAX_HEIGHT * 4, GFP_KERNEL);
	if (!fh->overlay_mask) {
		printk(KERN_ERR "%s: allocation of overlay_mask failed\n", zr->name);
		kfree(fh);
		return -ENOMEM;
	}

	zr->user++;

	/* default setup - TODO: look at flags */
	if (zr->user == 1) {	/* First device open */
		zr36057_restart(zr);
		zoran_open_init_params(zr);
		zoran_init_hardware(zr);

		btor(ZR36057_ICR_IntPinEn, ZR36057_ICR);
	}

	/* set file_ops stuff */
	file->private_data = fh;
	fh->zr = zr;
	zoran_open_init_session(file);

	MOD_INC_USE_COUNT;
	return 0;
}

static int
zoran_close (struct inode *inode,
             struct file  *file)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	dprintk(1, KERN_INFO "%s: zoran_close, %s pid=[%d]\n",
		zr->name, current->comm, current->pid);

	zoran_close_end_session(file);

	if (zr->user == 1) {	/* Last process */
		/* Clean up JPEG process */
		wake_up_interruptible(&zr->jpg_capq);
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
		zr->jpg_buffers.allocated = 0;
		zr->jpg_buffers.active = ZORAN_FREE;

		/* disable interrupts */
		btand(~ZR36057_ICR_IntPinEn, ZR36057_ICR);

		if (debug > 1)
			print_interrupts(zr);

		/* Overlay off */
		zr->v4l_overlay_active = 0;
		zr36057_overlay(zr, 0);
		zr->overlay_mask = NULL;

		/* capture off */
		wake_up_interruptible(&zr->v4l_capq);
		zr36057_set_memgrab(zr, 0);
		zr->v4l_buffers.allocated = 0;
		zr->v4l_buffers.active = ZORAN_FREE;
		zoran_set_pci_master(zr, 0);

		if (!pass_through) {	/* Switch to color bar */
			int zero = 0, two = 2;
		        // set_videobus_enable(zr, 0);
			decoder_command(zr, DECODER_ENABLE_OUTPUT, &zero);
			encoder_command(zr, ENCODER_SET_INPUT, &two);
		        // set_videobus_enable(zr, 1);
		}
	}

	zr->user--;
	file->private_data = NULL;
	kfree(fh->overlay_mask);
	kfree(fh);

	MOD_DEC_USE_COUNT;
	dprintk(2, KERN_INFO "%s: zoran_close done\n", zr->name);

	return 0;
}


static ssize_t
zoran_read (struct file *file,
            char        *data,
            size_t       count,
            loff_t      *ppos)
{
	/* we simply don't support read() (yet)... */

	return -EINVAL;
}

static ssize_t
zoran_write (struct file *file,
             const char  *data,
             size_t       count,
             loff_t      *ppos)
{
	/* ...and the same goes for write() */

	return -EINVAL;
}

static int
setup_fbuffer (struct file               *file,
               void                      *base,
               const struct zoran_format *fmt,
               int                        width,
               int                        height,
               int                        bytesperline)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	/* (Ronald) v4l/v4l2 guidelines */
	if(!capable(CAP_SYS_ADMIN) &&
	   !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* we need a bytesperline value, even if not given */
	if (!bytesperline)
		bytesperline = width * ((fmt->depth+7)&~7) / 8;

	if (zr->overlay_active) {
		/* Has the user gotten crazy ... ? */
		printk(KERN_ERR "%s: cannot change fbuffer when overlay active\n",
			zr->name);
		return -EBUSY;
	}
	if (!(fmt->flags & ZORAN_FORMAT_OVERLAY)) {
		printk(KERN_ERR "%s: no valid overlay format given\n",
			zr->name);
		return -EINVAL;
	}
	if (height <= 0 || width <= 0 || bytesperline <= 0) {
		printk(KERN_ERR "%s: invalid height/width/bpl value (%d|%d|%d)\n",
			zr->name, width, height, bytesperline);
		return -EINVAL;
	}
	if (bytesperline & 3) {
		printk(KERN_ERR "%s: bytesperline (%d) must be 4-byte aligned\n",
			zr->name, bytesperline);
		return -EINVAL;
	}

	zr->buffer.base = (void *) ((unsigned long) base & ~3);
	zr->buffer.height = height;
	zr->buffer.width = width;
	zr->buffer.depth = fmt->depth;
	zr->buffer.bytesperline = bytesperline;

	/* The user should set new window parameters */
	zr->overlay_settings.is_set = 0;

	return 0;
}


static int
setup_window (struct file       *file,
              int                x,
              int                y,
              int                width,
              int                height,
              struct video_clip *clips,
              int                clipcount)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	struct video_clip *vcp = NULL;
	int on, end, res;

	if (!zr->buffer.base) {
		printk(KERN_ERR "%s: VIDIOCSWIN: frame buffer has to be set first\n", zr->name);
		return -EINVAL;
	}

	/*
	 * The video front end needs 4-byte alinged line sizes, we correct that
	 * silently here if necessary
	 */
	if (zr->buffer.depth == 15 || zr->buffer.depth == 16) {
		end = (x + width) & ~1;	/* round down */
		x = (x + 1) & ~1;	/* round up */
		width = end - x;
	}

	if (zr->buffer.depth == 24) {
		end = (x + width) & ~3;	/* round down */
		x = (x + 3) & ~3;	/* round up */
		width = end - x;
	}
#if 0
	// At least xawtv seems to care about the following - just leave it away
	/*
	 * Also corrected silently (as long as window fits at all):
	 * video not fitting the screen
	 */
#if 0
	if (x < 0 || y < 0 || x + width > zr->buffer.width || y + height > zr->buffer.height) {
		printk(KERN_ERR "%s: VIDIOCSWIN: window does not fit frame buffer: %dx%d+%d*%d\n",
			zr->name, width, height, x, y);
		return -EINVAL;
	}
#else
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x + width > zr->buffer.width)
		width = zr->buffer.width - x;
	if (y + height > zr->buffer.height)
		height = zr->buffer.height - y;
#endif
#endif

	if (width > BUZ_MAX_WIDTH)
		width = BUZ_MAX_WIDTH;
	if (height > BUZ_MAX_HEIGHT)
		height = BUZ_MAX_HEIGHT;

	/* Check for vaild parameters */
	if (width < BUZ_MIN_WIDTH || height < BUZ_MIN_HEIGHT ||
	    width > BUZ_MAX_WIDTH || height > BUZ_MAX_HEIGHT) {
		printk(KERN_ERR "%s: VIDIOCSWIN: width = %d or height = %d invalid\n",
			zr->name, width, height);
		return -EINVAL;
	}

	fh->overlay_settings.x         = x;
	fh->overlay_settings.y         = y;
	fh->overlay_settings.width     = width;
	fh->overlay_settings.height    = height;
	fh->overlay_settings.clipcount = clipcount;

	/*
	 * If an overlay is running, we have to switch it off
	 * and switch it on again in order to get the new settings in effect.
	 *
	 * We also want to avoid that the overlay mask is written
	 * when an overlay is running.
	 */

	on = zr->v4l_overlay_active && !zr->v4l_memgrab_active &&
		zr->overlay_active != ZORAN_FREE && fh->overlay_active != ZORAN_FREE;
	if (on)
		zr36057_overlay(zr, 0);

	/*
	 *   Write the overlay mask if clips are wanted.
	 */
	if (clipcount > 0) {
		vcp = vmalloc(sizeof(struct video_clip) * (clipcount + 4));
		if (vcp == NULL) {
			printk(KERN_ERR "%s: zoran_ioctl: Alloc of clip mask failed\n", zr->name);
			return -ENOMEM;
		}
		if (copy_from_user(vcp, clips, sizeof(struct video_clip) * clipcount)) {
			vfree(vcp);
			return -EFAULT;
		}
		write_overlay_mask(file, vcp, clipcount);
		vfree(vcp);
	}

	fh->overlay_settings.is_set = 1;
	if (fh->overlay_active != ZORAN_FREE &&
	    zr->overlay_active != ZORAN_FREE)
		zr->overlay_settings = fh->overlay_settings;

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	res = wait_grab_pending(zr);
	if (res)
		return res;

	return 0;
}

#ifdef HAVE_V4L2
	/* get the status of a buffer in the clients buffer queue */
static int
zoran_v4l2_buffer_status (struct file        *file,
                          struct v4l2_buffer *buf,
                          int                 num)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	buf->flags = V4L2_BUF_FLAG_MAPPED;

	switch (fh->map_mode)
	{
		case ZORAN_MAP_MODE_RAW:

			/* check range */
			if (num < 0 || num >= fh->v4l_buffers.num_buffers ||
			    !fh->v4l_buffers.allocated) {
				printk(KERN_ERR "%s: zoran_v4l2_buffer_status() - wrong number or buffers not allocated\n",
					zr->name);
				return -EINVAL;
			}

			buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf->length = fh->v4l_buffers.buffer_size;

			/* get buffer */
			buf->bytesused = fh->v4l_buffers.buffer[num].bs.length;
			if (fh->v4l_buffers.buffer[num].state == BUZ_STATE_DONE ||
			    fh->v4l_buffers.buffer[num].state == BUZ_STATE_USER) {
				buf->sequence  = fh->v4l_buffers.buffer[num].bs.seq;
				buf->flags |= V4L2_BUF_FLAG_DONE;
				buf->timestamp = fh->v4l_buffers.buffer[num].bs.timestamp;
			} else {
				buf->flags |= V4L2_BUF_FLAG_QUEUED;
			}

			if (fh->v4l_settings.height <= BUZ_MAX_HEIGHT/2)
				buf->field = V4L2_FIELD_TOP;
			else
				buf->field = V4L2_FIELD_INTERLACED;

			break;

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:

			/* check range */
			if (num < 0 || num >= fh->jpg_buffers.num_buffers ||
			    !fh->jpg_buffers.allocated) {
				printk(KERN_ERR "%s: zoran_v4l2_buffer_status() - wrong number or buffers not allocated\n",
					zr->name);
				return -EINVAL;
			}

			buf->type = (fh->map_mode==ZORAN_MAP_MODE_JPG_REC)?V4L2_BUF_TYPE_VIDEO_CAPTURE:V4L2_BUF_TYPE_VIDEO_OUTPUT;
			buf->length = fh->jpg_buffers.buffer_size;

			/* these variables are only written after frame has been captured */
			if (fh->jpg_buffers.buffer[num].state == BUZ_STATE_DONE ||
			    fh->jpg_buffers.buffer[num].state == BUZ_STATE_USER) {
				buf->sequence  = fh->jpg_buffers.buffer[num].bs.seq;
				buf->timestamp = fh->jpg_buffers.buffer[num].bs.timestamp;
				buf->bytesused = fh->jpg_buffers.buffer[num].bs.length;
				buf->flags |= V4L2_BUF_FLAG_DONE;
			} else {
				buf->flags |= V4L2_BUF_FLAG_QUEUED;
			}

			/* which fields are these? */
			if (fh->jpg_settings.TmpDcm != 1)
				buf->field = fh->jpg_settings.odd_even?V4L2_FIELD_TOP:V4L2_FIELD_BOTTOM;
			else
				buf->field = fh->jpg_settings.odd_even?V4L2_FIELD_SEQ_TB:V4L2_FIELD_SEQ_BT;

			break;

		default:

			printk(KERN_ERR "%s: zoran_v4l2_buffer_status() - invalid buffer type|map_mode (%d|%d)\n",
				zr->name, buf->type, fh->map_mode);
			return -EINVAL;
	}

	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = num;
	buf->m.offset = buf->length * num;

	return 0;
}
#endif

static int
zoran_set_norm (struct zoran *zr,
                int           norm) /* VIDEO_MODE_* */
{
	int norm_encoder;
	int on;

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		printk(KERN_WARNING "%s: set_norm() called while in playback/capture mode\n",
			zr->name);
		return -EBUSY;
	}

	if (lock_norm && norm != zr->norm) {
		if (lock_norm > 1) {
			printk(KERN_WARNING "%s: set_norm(): TV standard is locked, can not switch norm\n",
				zr->name);
			return -EPERM;
		} else {
			printk(KERN_WARNING "%s: set_norm(): TV standard is locked, norm was not changed\n",
				zr->name);
			norm = zr->norm;
		}
	}

	if (norm != VIDEO_MODE_AUTO &&
	    (norm < 0 || norm >= zr->card->norms || !zr->card->tvn[norm]))
	{
		printk(KERN_ERR "%s: set_norm(): unsupported norm %d\n",
			zr->name, norm);
		return -EINVAL;
	}

	if (norm == VIDEO_MODE_AUTO) {
		int status;

		decoder_command(zr, DECODER_SET_NORM, &norm);

		/* let changes come into effect */
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(1 * HZ);

		decoder_command(zr, DECODER_GET_STATUS, &status);
		if (!(status & DECODER_STATUS_GOOD)) {
			printk(KERN_ERR "%s: set_norm(): no norm detected\n",
				zr->name);
			/* reset norm */
			decoder_command(zr, DECODER_SET_NORM, &zr->norm);
			return -EIO;
		}

		if (status & DECODER_STATUS_NTSC)
			norm = VIDEO_MODE_NTSC;
		else if (status & DECODER_STATUS_SECAM)
			norm = VIDEO_MODE_SECAM;
		else
			norm = VIDEO_MODE_PAL;
	}
	zr->timing = zr->card->tvn[norm];
	norm_encoder = norm;

	/* We switch overlay off and on since a change in the
	   norm needs different VFE settings */
	on = zr->overlay_active && !zr->v4l_memgrab_active;
	if (on)
		zr36057_overlay(zr, 0);

        // set_videobus_enable(zr, 0);
	decoder_command(zr, DECODER_SET_NORM, &norm);
	encoder_command(zr, ENCODER_SET_NORM, &norm_encoder);
       	// set_videobus_enable(zr, 1);

	if (on)
		zr36057_overlay(zr, 1);

	/* Make sure the changes come into effect */
	zr->norm = norm;

	return 0;
}

static int
zoran_set_input (struct zoran *zr,
                 int           input)
{
	int realinput;

	if (input == zr->input)
		return 0;

	if (zr->v4l_buffers.active != ZORAN_FREE ||
	    zr->jpg_buffers.active != ZORAN_FREE) {
		printk(KERN_WARNING "%s: set_input() called while in playback/capture mode\n",
			zr->name);
		return -EBUSY;
	}

	if (input < 0 || input >= zr->card->inputs) {
		printk(KERN_ERR "%s: ioctl VIDIOC_S_INPUT: unnsupported input %d\n",
			zr->name, input);
		return -EINVAL;
	}

	realinput = zr->card->input[input].muxsel;
	zr->input = input;

	// set_videobus_enable(zr, 0);
	decoder_command(zr, DECODER_SET_INPUT, &realinput);
	// set_videobus_enable(zr, 1);

	return 0;
}

/*
 *   ioctl routine
 */

static int
zoran_do_ioctl (struct inode *inode,
                struct file  *file,
                unsigned int  cmd,
                void         *arg)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;

	switch (cmd) {

	case VIDIOCGCAP:
		{
			struct video_capability *vcap = arg;
			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGCAP\n", zr->name);

			memset(vcap, 0, sizeof(struct video_capability));
			strncpy(vcap->name, zr->name, sizeof(vcap->name));
			vcap->type = ZORAN_VID_TYPE;

			vcap->channels = zr->card->inputs;
			vcap->audios = 0;
			vcap->maxwidth = BUZ_MAX_WIDTH;
			vcap->maxheight = BUZ_MAX_HEIGHT;
			vcap->minwidth = BUZ_MIN_WIDTH;
			vcap->minheight = BUZ_MIN_HEIGHT;

			return 0;
		}
		break;

	case VIDIOCGCHAN:
		{
			struct video_channel *vchan = arg;
			int channel = vchan->channel;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGCHAN for channel %d\n",
				zr->name, vchan->channel);

			memset(vchan, 0, sizeof(struct video_channel));
			if (channel > zr->card->inputs) {
				printk(KERN_ERR "%s: VIDIOCGCHAN on not existing channel %d\n",
					zr->name, channel);
				return -EINVAL;
			}

			strcpy (vchan->name, zr->card->input[channel].name);

			vchan->tuners = 0;
			vchan->flags = 0;
			vchan->type = VIDEO_TYPE_CAMERA;
			vchan->norm = zr->norm;
			vchan->channel = channel;

			return 0;
		}
		break;

		/* RJ: the documentation at http://roadrunner.swansea.linux.org.uk/v4lapi.shtml says:

		 * "The VIDIOCSCHAN ioctl takes an integer argument and switches the capture to this input."
		 *                                 ^^^^^^^
		 * The famos BTTV driver has it implemented with a struct video_channel argument
		 * and we follow it for compatibility reasons
		 *
		 * BTW: this is the only way the user can set the norm!
		 */

	case VIDIOCSCHAN:
		{
			struct video_channel *vchan = arg;
			int res;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCSCHAN: channel=%d, norm=%d\n",
				zr->name, vchan->channel, vchan->norm);

			if ((res = zoran_set_input(zr, vchan->channel)))
				return res;
			if ((res = zoran_set_norm(zr, vchan->norm)))
				return res;

			/* Make sure the changes come into effect */
			return wait_grab_pending(zr);
		}
		break;

	case VIDIOCGPICT:
		{
			struct video_picture *vpict = arg;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGPICT\n", zr->name);

			memset(vpict, 0, sizeof(struct video_picture));
			vpict->hue = zr->hue;
			vpict->brightness = zr->brightness;
			vpict->contrast = zr->contrast;
			vpict->colour = zr->saturation;

			vpict->depth = zr->buffer.depth;
			switch (zr->buffer.depth) {
			case 15:
				vpict->palette = VIDEO_PALETTE_RGB555;
				break;

			case 16:
				vpict->palette = VIDEO_PALETTE_RGB565;
				break;

			case 24:
				vpict->palette = VIDEO_PALETTE_RGB24;
				break;

			case 32:
				vpict->palette = VIDEO_PALETTE_RGB32;
				break;
			}

			return 0;
		}
		break;

	case VIDIOCSPICT:
		{
			struct video_picture *vpict = arg;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCSPICT bri=%d hue=%d col=%d con=%d dep=%d pal=%d\n",
			              zr->name, vpict->brightness, vpict->hue, vpict->colour, vpict->contrast,
			              vpict->depth, vpict->palette);

			decoder_command(zr, DECODER_SET_PICTURE, vpict);

			/* The depth and palette values have no meaning to us,
			   should we return  -EINVAL if they don't fit ? */
			zr->hue = vpict->hue;
			zr->contrast = vpict->contrast;
			zr->saturation = vpict->colour;
			zr->brightness = vpict->brightness;

			return 0;
		}
		break;

#ifdef HAVE_V4L2
	case VIDIOC_OVERLAY:
#endif
	case VIDIOCCAPTURE:
		{
			int *on = arg, res;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCCAPTURE/VIDIOC_PREVIEW: %d\n", zr->name, *on);

			/* If there is nothing to do, return immediatly */
			if ((*on && fh->overlay_active != ZORAN_FREE) ||
			    (!*on && fh->overlay_active == ZORAN_FREE))
				return 0;

			/* check whether we're touching someone else's overlay */
			if (*on && zr->overlay_active != ZORAN_FREE && fh->overlay_active == ZORAN_FREE) {
				printk(KERN_ERR "%s: VIDIOCCAPTURE/VIDIOC_PREVIEW: overlay is already active for another session\n",
					zr->name);
				return -EBUSY;
			}
			if (!*on && zr->overlay_active != ZORAN_FREE && fh->overlay_active == ZORAN_FREE) {
				printk(KERN_ERR "%s: VICIOCCAPTURE/VIDIOC_PREVIEW: you cannot cancel someone else's session\n",
					zr->name);
				return -EPERM;
			}

			if (*on == 0) {
				zr->overlay_active = fh->overlay_active = ZORAN_FREE;
				zr->v4l_overlay_active = 0;
				/* When a grab is running, the video simply won't be switched on any more */
				if (!zr->v4l_memgrab_active)
					zr36057_overlay(zr, 0);
				zr->overlay_mask = NULL;
			} else {
				if (!zr->buffer.base || !fh->overlay_settings.is_set) {
					printk(KERN_ERR "%s: VIDIOCCAPTURE/VIDIOC_PREVIEW: buffer or window not set\n", zr->name);
					return -EINVAL;
				}
				zr->overlay_active = fh->overlay_active = ZORAN_LOCKED;
				zr->v4l_overlay_active = 1;
				zr->overlay_mask = fh->overlay_mask;
				zr->overlay_settings = fh->overlay_settings;
				if (!zr->v4l_memgrab_active)
					zr36057_overlay(zr, 1);
				/* When a grab is running, the video will be switched on when grab is finished */
			}
			/* Make sure the changes come into effect */
			res = wait_grab_pending(zr);
			if (res)
				return res;
			return 0;
		}
		break;

	case VIDIOCGWIN:
		{
			struct video_window *vwin = arg;
			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGWIN\n", zr->name);
			memset(vwin, 0, sizeof(struct video_window));
			vwin->x = fh->overlay_settings.x;
			vwin->y = fh->overlay_settings.y;
			vwin->width = fh->overlay_settings.width;
			vwin->height = fh->overlay_settings.height;
			vwin->clipcount = 0;
			return 0;
		}
		break;

	case VIDIOCSWIN:
		{
			struct video_window *vwin = arg;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCSWIN: x=%d y=%d w=%d h=%d clipcount=%d\n",
				      zr->name, vwin->x, vwin->y, vwin->width, vwin->height, vwin->clipcount);

			return setup_window(file, vwin->x, vwin->y, vwin->width, vwin->height,
				vwin->clips, vwin->clipcount);
		}
		break;

	case VIDIOCGFBUF:
		{
			struct video_buffer *vbuf = arg;
			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGFBUF\n", zr->name);
			*vbuf = zr->buffer;
			return 0;
		}
		break;

	case VIDIOCSFBUF:
		{
			struct video_buffer *vbuf = arg;
			int i;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCSFBUF: base=0x%x w=%d h=%d depth=%d bpl=%d\n",
			              zr->name, (u32) vbuf->base, vbuf->width, vbuf->height,
			              vbuf->depth, vbuf->bytesperline);

			for (i=0;i<ZORAN_FORMATS;i++)
				if (zoran_formats[i].depth == vbuf->depth)
					break;
			if (i == ZORAN_FORMATS) {
				printk(KERN_ERR "%s: ioctl VIDIOCSFBUF: invalid fbuf depth %d\n",
					zr->name, vbuf->depth);
				return -EINVAL;
			}

			return setup_fbuffer(file, vbuf->base, &zoran_formats[i],
					vbuf->width, vbuf->height, vbuf->bytesperline);
		}
		break;

		/* RJ: what is VIDIOCKEY intended to do ??? */

	case VIDIOCKEY:
		{
			/* Will be handled higher up .. */
			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCKEY\n", zr->name);
			return 0;
		}
		break;

	case VIDIOCSYNC:
		{
			int *frame = arg;
			dprintk(3, KERN_DEBUG "%s: ioctl VIDIOCSYNC %d\n", zr->name, *frame);
			return v4l_sync(file, *frame);
		}
		break;

	case VIDIOCMCAPTURE:
		{
			struct video_mmap *vmap = arg;
			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCMCAPTURE frame=%d geom=%dx%d fmt=%d\n",
				      zr->name, vmap->frame, vmap->width, vmap->height, vmap->format);
			return v4l_grab(file, vmap);
		}
		break;

	case VIDIOCGMBUF:
		{
			struct video_mbuf *vmbuf = arg;
			int i, do_alloc = 1;

			if (fh->jpg_buffers.allocated || fh->v4l_buffers.allocated) {
				printk(KERN_ERR "%s: VIDIOCGMBUF: buffers allready allocated\n", zr->name);
				return -EINVAL;
			}

			/* if there are existing buffers and they're the same... */
			if (fh->v4l_buffers.secretly_allocated) {
				/* since we provide our own settings, they're always the same */
				do_alloc = 0;
				fh->v4l_buffers.secretly_allocated = 0;
				fh->v4l_buffers.allocated = 1;
			}

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGMBUF\n", zr->name);

			fh->v4l_buffers.buffer_size = v4l_bufsize;
			vmbuf->size = v4l_nbufs * v4l_bufsize;
			fh->v4l_buffers.num_buffers = vmbuf->frames = v4l_nbufs;
			for (i = 0; i < vmbuf->frames; i++) {
				vmbuf->offsets[i] = i * v4l_bufsize;
			}

			if (do_alloc)
				if (v4l_fbuffer_alloc(file))
					return -ENOMEM;

			/* The next mmap will map the V4L buffers */
			fh->map_mode = ZORAN_MAP_MODE_RAW;

			return 0;
		}
		break;

	case VIDIOCGUNIT:
		{
			struct video_unit *vunit = arg;

			dprintk(2, KERN_DEBUG "%s: ioctl VIDIOCGUNIT\n", zr->name);
			vunit->video = zr->video_dev.minor;
			vunit->vbi = VIDEO_NO_UNIT;
			vunit->radio = VIDEO_NO_UNIT;
			vunit->audio = VIDEO_NO_UNIT;
			vunit->teletext = VIDEO_NO_UNIT;

			return 0;
		}
		break;

		/*
		 * RJ: In principal we could support subcaptures for V4L grabbing.
		 *     Not even the famous BTTV driver has them, however.
		 *     If there should be a strong demand, one could consider
		 *     to implement them.
		 */
	case VIDIOCGCAPTURE:
		{
			printk(KERN_ERR "%s: ioctl VIDIOCGCAPTURE not supported\n", zr->name);
			return -EINVAL;
		}
		break;

	case VIDIOCSCAPTURE:
		{
			printk(KERN_ERR "%s: ioctl VIDIOCSCAPTURE not supported\n", zr->name);
			return -EINVAL;
		}
		break;

	case BUZIOC_G_PARAMS:
		{
			struct zoran_params *bparams = arg;
			dprintk(2, KERN_DEBUG "%s: ioctl BUZIOC_G_PARAMS\n", zr->name);

			memset(bparams, 0, sizeof(struct zoran_params));
			bparams->major_version = MAJOR_VERSION;
			bparams->minor_version = MINOR_VERSION;
			bparams->norm = zr->norm;
			bparams->input = zr->input;

			bparams->decimation = fh->jpg_settings.decimation;
			bparams->HorDcm = fh->jpg_settings.HorDcm;
			bparams->VerDcm = fh->jpg_settings.VerDcm;
			bparams->TmpDcm = fh->jpg_settings.TmpDcm;
			bparams->field_per_buff = fh->jpg_settings.field_per_buff;
			bparams->img_x = fh->jpg_settings.img_x;
			bparams->img_y = fh->jpg_settings.img_y;
			bparams->img_width = fh->jpg_settings.img_width;
			bparams->img_height = fh->jpg_settings.img_height;
			bparams->odd_even = fh->jpg_settings.odd_even;

			bparams->quality = fh->jpg_settings.jpg_comp.quality;
			bparams->APPn = fh->jpg_settings.jpg_comp.APPn;
			bparams->APP_len = fh->jpg_settings.jpg_comp.APP_len;
			memcpy(bparams->APP_data, fh->jpg_settings.jpg_comp.APP_data, sizeof(bparams->APP_data));
			bparams->COM_len = zr->jpg_settings.jpg_comp.COM_len;
			memcpy(bparams->COM_data, fh->jpg_settings.jpg_comp.COM_data, sizeof(bparams->COM_data));
			bparams->jpeg_markers = fh->jpg_settings.jpg_comp.jpeg_markers;

			bparams->VFIFO_FB = 0;

			return 0;
		}
		break;

	case BUZIOC_S_PARAMS:
		{
			struct zoran_params *bparams = arg;
			struct zoran_jpg_settings settings;

			if (zr->codec_mode != BUZ_MODE_IDLE) {
				printk(KERN_ERR "%s: BUZIOC_S_PARAMS called but Buz in capture/playback mode\n", zr->name);
				return -EINVAL;
			}

			dprintk(2, KERN_DEBUG "%s: ioctl BUZIOC_S_PARAMS\n", zr->name);

			settings.decimation = bparams->decimation;
			settings.HorDcm = bparams->HorDcm;
			settings.VerDcm = bparams->VerDcm;
			settings.TmpDcm = bparams->TmpDcm;
			settings.field_per_buff = bparams->field_per_buff;
			settings.img_x = bparams->img_x;
			settings.img_y = bparams->img_y;
			settings.img_width = bparams->img_width;
			settings.img_height = bparams->img_height;
			settings.odd_even = bparams->odd_even;

			settings.jpg_comp.quality = bparams->quality;
			settings.jpg_comp.APPn = bparams->APPn;
			settings.jpg_comp.APP_len = bparams->APP_len;
			memcpy(settings.jpg_comp.APP_data, bparams->APP_data, sizeof(bparams->APP_data));
			settings.jpg_comp.COM_len = bparams->COM_len;
			memcpy(settings.jpg_comp.COM_data, bparams->COM_data, sizeof(bparams->COM_data));
			settings.jpg_comp.jpeg_markers = bparams->jpeg_markers;

			/* Check the params first before overwriting our internal values */
			if (zoran_check_jpg_settings(zr, &settings))
				return -EINVAL;

			fh->jpg_settings = settings;

			return 0;
		}
		break;

	case BUZIOC_REQBUFS:
		{
			struct zoran_requestbuffers *breq = arg;
			int do_alloc = 1;

			if (fh->jpg_buffers.allocated || fh->v4l_buffers.allocated) {
				printk(KERN_ERR "%s: BUZIOC_REQBUFS: buffers allready allocated\n", zr->name);
				return -EBUSY;
			}

			dprintk(2, KERN_DEBUG "%s: ioctl BUZIOC_REQBUFS count = %lu size=%lu\n", zr->name,
				      breq->count, breq->size);

			/* Enforce reasonable lower and upper limits */
			if (breq->count < 4)
				breq->count = 4;	/* Could be choosen smaller */
			if (breq->count > BUZ_MAX_FRAME)
				breq->count = BUZ_MAX_FRAME;
			breq->size = PAGE_ALIGN(breq->size);
			if (breq->size < 8192)
				breq->size = 8192;	/* Arbitrary */
			/* breq->size is limited by 1 page for the stat_com tables to a Maximum of 2 MB */
			if (breq->size > (512 * 1024))
				breq->size = (512 * 1024);	/* 512 K should be enough */
			if (fh->jpg_buffers.need_contiguous && breq->size > MAX_KMALLOC_MEM)
				breq->size = MAX_KMALLOC_MEM;

			if (fh->jpg_buffers.secretly_allocated) {
				if (fh->jpg_buffers.num_buffers == breq->count &&
				    fh->jpg_buffers.buffer_size == breq->size) {
					do_alloc = 0;
					fh->jpg_buffers.secretly_allocated = 0;
					fh->jpg_buffers.allocated = 1;
				}
			}

			fh->jpg_buffers.num_buffers = breq->count;
			fh->jpg_buffers.buffer_size = breq->size;

			if (do_alloc)
				if (jpg_fbuffer_alloc(file))
					return -ENOMEM;

			/* The next mmap will map the MJPEG buffers - could also be *_PLAY, but it doesn't matter here */
			fh->map_mode = ZORAN_MAP_MODE_JPG_REC;

			return 0;
		}
		break;

	case BUZIOC_QBUF_CAPT:
		{
			int *frame = arg;
			dprintk(4, KERN_DEBUG "%s: ioctl BUZIOC_QBUF_CAPT %d\n",
				zr->name, *frame);
			return jpg_qbuf(file, *frame, BUZ_MODE_MOTION_COMPRESS);
		}
		break;

	case BUZIOC_QBUF_PLAY:
		{
			int *frame = arg;
			dprintk(4, KERN_DEBUG "%s: ioctl BUZIOC_QBUF_PLAY %d\n",
				zr->name, *frame);
			return jpg_qbuf(file, *frame, BUZ_MODE_MOTION_DECOMPRESS);
		}
		break;

	case BUZIOC_SYNC:
		{
			struct zoran_sync *bsync = arg;
			dprintk(4, KERN_DEBUG "%s: ioctl BUZIOC_SYNC\n", zr->name);
			return jpg_sync(file, bsync);
		}
		break;

	case BUZIOC_G_STATUS:
		{
			struct zoran_status *bstat = arg;
			int norm, input, status;

			if (zr->codec_mode != BUZ_MODE_IDLE) {
				printk(KERN_ERR "%s: BUZIOC_G_STATUS called but Buz in capture/playback mode\n", zr->name);
				return -EINVAL;
			}

			dprintk(2, KERN_DEBUG "%s: ioctl BUZIOC_G_STATUS\n", zr->name);

			if (bstat->input > zr->card->inputs) {
				printk(KERN_ERR "%s: BUZIOC_G_STATUS on not existing input %d\n",
					zr->name, bstat->input);
				return -EINVAL;
			}
			input = zr->card->input[bstat->input].muxsel;

			/* Set video norm to VIDEO_MODE_AUTO */
			norm = VIDEO_MODE_AUTO;
		        // set_videobus_enable(zr, 0);
			decoder_command(zr, DECODER_SET_INPUT, &input);
			decoder_command(zr, DECODER_SET_NORM, &norm);
		        // set_videobus_enable(zr, 1);

			/* sleep 1 second */
			current->state = TASK_UNINTERRUPTIBLE;
			schedule_timeout(1 * HZ);

			/* Get status of video decoder */

			decoder_command(zr, DECODER_GET_STATUS, &status);
			bstat->signal = (status & DECODER_STATUS_GOOD) ? 1 : 0;

			if (status & DECODER_STATUS_NTSC)
				bstat->norm = VIDEO_MODE_NTSC;
			else if (status & DECODER_STATUS_SECAM)
				bstat->norm = VIDEO_MODE_SECAM;
			else
				bstat->norm = VIDEO_MODE_PAL;

			bstat->color = (status & DECODER_STATUS_COLOR) ? 1 : 0;

			/* restore previous input and norm */
			input = zr->card->input[zr->input].muxsel;
		        // set_videobus_enable(zr, 0);
			decoder_command(zr, DECODER_SET_INPUT, &input);
			decoder_command(zr, DECODER_SET_NORM, &zr->norm);
		        // set_videobus_enable(zr, 1);

			return 0;
		}
		break;

#ifdef HAVE_V4L2

	/* The new video4linux2 capture interface - much nicer than video4linux1, since
	 * it allows for integrating the JPEG capturing calls inside standard v4l2
	 */

	case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *cap = arg;

			dprintk(2, "%s: ioctl VIDIOC_QUERYCAP (v4l2)\n", zr->name);

			memset(cap, 0, sizeof(*cap));
			strncpy(cap->card, zr->name, sizeof(cap->card));
			strncpy(cap->driver, "zoran", sizeof(cap->driver));
			snprintf(cap->bus_info, sizeof(cap->bus_info),
				"PCI:%s", zr->pci_dev->slot_name);
			cap->version = KERNEL_VERSION(MAJOR_VERSION,
							MINOR_VERSION,
							RELEASE_VERSION);
			cap->capabilities = ZORAN_V4L2_VID_FLAGS;

			return 0;
		}
		break;

	case VIDIOC_ENUM_FMT:
		{
			struct v4l2_fmtdesc *fmt = arg;
			int index = fmt->index, num = -1, i, flag = 0, type = fmt->type;
			dprintk(2, "%s: ioctl VIDIOC_ENUM_FMT (v4l2): index=%d\n", zr->name,
				fmt->index);

			switch (fmt->type) {
				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
					flag = ZORAN_FORMAT_CAPTURE;
					break;
				case V4L2_BUF_TYPE_VIDEO_OUTPUT:
					flag = ZORAN_FORMAT_PLAYBACK;
					break;
				case V4L2_BUF_TYPE_VIDEO_OVERLAY:
					flag = ZORAN_FORMAT_OVERLAY;
					break;
				default:
					printk(KERN_ERR "%s: ioctl VIDIOC_ENUM_FMT: unknown type %d\n",
						zr->name, fmt->type);
					return -EINVAL;
			}

			for (i=0;i<ZORAN_FORMATS;i++) {
				if (zoran_formats[i].flags & flag)
					num++;
				if (num == fmt->index)
					break;
			}
			if (fmt->index < 0 /* late, but not too late */ || i == ZORAN_FORMATS)
				return -EINVAL;

			memset(fmt, 0, sizeof(*fmt));
			fmt->index = index;
			fmt->type = type;
			strncpy(fmt->description, zoran_formats[i].name, 31);
			fmt->pixelformat = zoran_formats[i].fourcc;
			if (zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED)
				fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;

			return 0;
		}
		break;

	case VIDIOC_G_FMT:
		{
			struct v4l2_format *fmt = arg;
			int type = fmt->type;

			dprintk(2, "%s: ioctl VIDIOC_G_FMT (v4l2)\n", zr->name);

			memset(fmt, 0, sizeof(*fmt));
			fmt->type = type;

			switch (fmt->type) {
				case V4L2_BUF_TYPE_VIDEO_OVERLAY:

					fmt->fmt.win.w.left = fh->overlay_settings.x;
					fmt->fmt.win.w.top = fh->overlay_settings.y;
					fmt->fmt.win.w.width = fh->overlay_settings.width;
					fmt->fmt.win.w.height = fh->overlay_settings.height;
					if (fh->overlay_settings.width * 2 > BUZ_MAX_HEIGHT)
						fmt->fmt.win.field = V4L2_FIELD_INTERLACED;
					else
						fmt->fmt.win.field = V4L2_FIELD_TOP;

					break;

				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				case V4L2_BUF_TYPE_VIDEO_OUTPUT:

					if ((type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
					     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) ||
					    (type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
					     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)) {
						printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: cannot handle capture/playback at the same time\n",
							zr->name);
						return -EINVAL;
					}

					switch (fh->map_mode) {
						case ZORAN_MAP_MODE_RAW:
							fmt->fmt.pix.width = fh->v4l_settings.width;
								fmt->fmt.pix.height = fh->v4l_settings.height;
								fmt->fmt.pix.sizeimage = fh->v4l_buffers.buffer_size /*fh->v4l_settings.bytesperline * fh->v4l_settings.height*/;
								fmt->fmt.pix.pixelformat = fh->v4l_settings.format->fourcc;
								fmt->fmt.pix.colorspace = fh->v4l_settings.format->colorspace;
								fmt->fmt.pix.bytesperline = 0;
								if (BUZ_MAX_HEIGHT < (fh->v4l_settings.height*2))
									fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
								else
									fmt->fmt.pix.field = V4L2_FIELD_TOP;
							break;
						case ZORAN_MAP_MODE_JPG_REC:
						case ZORAN_MAP_MODE_JPG_PLAY:
							fmt->fmt.pix.width = fh->jpg_settings.img_width / fh->jpg_settings.HorDcm;
							fmt->fmt.pix.height = fh->jpg_settings.img_height / (fh->jpg_settings.VerDcm * fh->jpg_settings.TmpDcm);
							if (!fh->jpg_buffers.allocated && !fh->jpg_buffers.secretly_allocated)
								fh->jpg_buffers.buffer_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);
							fmt->fmt.pix.sizeimage = fh->jpg_buffers.buffer_size;
							fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
							if (fh->jpg_settings.TmpDcm == 1)
								fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_SEQ_BT:V4L2_FIELD_SEQ_BT);
							else
								fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_TOP:V4L2_FIELD_BOTTOM);

							fmt->fmt.pix.bytesperline = 0;
							fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
							break;
					}

					break;

				default:
					printk(KERN_ERR "%s: unsupported type %d\n",
						zr->name, fmt->type);
					return -EINVAL;
			}
			return 0;
		}
		break;

	case VIDIOC_S_FMT:
		{
			struct v4l2_format *fmt = arg;
			int i, res;
			__u32 printformat;

			dprintk(2, "%s: ioctl VIDIOC_S_FMT (v4l2): type=%d, ",
				zr->name, fmt->type);

			switch (fmt->type) {
				case V4L2_BUF_TYPE_VIDEO_OVERLAY:

					dprintk(2, "x=%d, y=%d, w=%d, h=%d, cnt=%d\n",
						fmt->fmt.win.w.left, fmt->fmt.win.w.top,
						fmt->fmt.win.w.width, fmt->fmt.win.w.height,
						fmt->fmt.win.clipcount); 
					return setup_window(file,
							fmt->fmt.win.w.left, fmt->fmt.win.w.top,
							fmt->fmt.win.w.width, fmt->fmt.win.w.height,
							(struct video_clip*)fmt->fmt.win.clips,
							fmt->fmt.win.clipcount);
					break;

				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				case V4L2_BUF_TYPE_VIDEO_OUTPUT:

					printformat = __cpu_to_le32(fmt->fmt.pix.pixelformat);
					dprintk(2, "size=%dx%d, fmt=0x%x (%4.4s)\n",
						fmt->fmt.pix.width, fmt->fmt.pix.height,
						fmt->fmt.pix.pixelformat, (char*)&printformat);

					if (fmt->fmt.pix.bytesperline > 0) {
						printk(KERN_ERR "%s: bpl not supported\n",
							zr->name);
						return -EINVAL;
					}

					/* we can be requested to do JPEG/raw playback/capture */
					if (!(fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
					      (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
					       fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG))) {
						printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: unknown type %d/0x%x(%4.4s) combination\n",
							zr->name, fmt->type, fmt->fmt.pix.pixelformat,
							(char*)&printformat);
						return -EINVAL;
					}

					if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
						struct zoran_jpg_settings settings = fh->jpg_settings;

						if (fh->v4l_buffers.allocated || fh->jpg_buffers.allocated) {
							printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: cannot change capture mode\n", zr->name);
							return -EBUSY;
						}

						/* we actually need to set 'real' parameters now */
						if ((fmt->fmt.pix.height*2) > BUZ_MAX_HEIGHT)
							settings.TmpDcm = 1;
						else
							settings.TmpDcm = 2;
						settings.decimation = 0;
						if (fmt->fmt.pix.height <= fh->jpg_settings.img_height/2)
							settings.VerDcm = 2;
						else
							settings.VerDcm = 1;
						if (fmt->fmt.pix.width <= fh->jpg_settings.img_width/4)
							settings.HorDcm = 4;
						else if (fmt->fmt.pix.width <= fh->jpg_settings.img_width/2)
							settings.HorDcm = 2;
						else
							settings.HorDcm = 1;
						if (settings.TmpDcm == 1)
							settings.field_per_buff = 2;
						else
							settings.field_per_buff = 1;

						/* check */
						if ((res = zoran_check_jpg_settings(zr, &settings)))
							return res;

						/* it's ok, so set them */
						fh->jpg_settings = settings;

						/* tell the user what we actually did */
						fmt->fmt.pix.width = settings.img_width / settings.HorDcm;
						fmt->fmt.pix.height = settings.img_height*2 / (settings.TmpDcm*settings.VerDcm);
						if (settings.TmpDcm == 1)
							fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_SEQ_TB:V4L2_FIELD_SEQ_BT);
						else
							fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_TOP:V4L2_FIELD_BOTTOM);
						fh->jpg_buffers.buffer_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);
						fmt->fmt.pix.sizeimage = fh->jpg_buffers.buffer_size;

						/* we hereby abuse this variable to show that
						 * we're gonna do mjpeg capture */
						fh->map_mode = (fmt->type==V4L2_BUF_TYPE_VIDEO_CAPTURE)?ZORAN_MAP_MODE_JPG_REC:ZORAN_MAP_MODE_JPG_PLAY;
					} else {
						if (fh->jpg_buffers.allocated ||
						    (fh->v4l_buffers.allocated && fh->v4l_buffers.active != ZORAN_FREE)) {
							printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: cannot change capture mode\n", zr->name);
							return -EBUSY;
						}
						for (i=0;i<ZORAN_FORMATS;i++)
							if (fmt->fmt.pix.pixelformat == zoran_formats[i].fourcc)
								break;
						if (i == ZORAN_FORMATS) {
							printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: unknown/unsupported format 0x%x (%4.4s)\n",
								zr->name, fmt->fmt.pix.pixelformat, (char*)&printformat);
							return -EINVAL;
						}
						if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
							fmt->fmt.pix.height = BUZ_MAX_HEIGHT;
						if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
							fmt->fmt.pix.width = BUZ_MAX_WIDTH;

						if ((res = zoran_v4l_set_format(file,
						    fmt->fmt.pix.width, fmt->fmt.pix.height, &zoran_formats[i])))
							return res;

						/* tell the user the results/missing stuff */
						fmt->fmt.pix.sizeimage = fh->v4l_buffers.buffer_size /*zr->gbpl * zr->gheight*/;
						if (BUZ_MAX_HEIGHT < (fh->v4l_settings.height*2))
							fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
						else
							fmt->fmt.pix.field = V4L2_FIELD_TOP;

						fh->map_mode = ZORAN_MAP_MODE_RAW;
					}

					break;

				default:
					dprintk(2, "unsupported\n");
					printk(KERN_ERR "%s: unsupported type %d\n",
						zr->name, fmt->type);
					return -EINVAL;
			}

			return 0;
		}
		break;

	case VIDIOC_G_FBUF:
		{
			int i;
			struct v4l2_framebuffer *fb = arg;

			dprintk(2, "%s: ioctl VIDIOC_G_FBUF (v4l2)\n", zr->name);

			memset(fb ,0, sizeof(*fb));
			fb->base = zr->buffer.base;
			fb->fmt.width = zr->buffer.width;
			fb->fmt.height = zr->buffer.height;
			for (i=0;i<ZORAN_FORMATS;i++) {
				if (zoran_formats[i].depth == zr->buffer.depth &&
				    zoran_formats[i].flags & ZORAN_FORMAT_OVERLAY) {
					fb->fmt.pixelformat = zoran_formats[i].fourcc;
					break;
				}
			}
			fb->fmt.bytesperline = zr->buffer.bytesperline;
			fb->fmt.colorspace = V4L2_COLORSPACE_SRGB;
			fb->fmt.field = V4L2_FIELD_INTERLACED;
			fb->flags = V4L2_FBUF_FLAG_OVERLAY;
			fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

			return 0;
		}
		break;

	case VIDIOC_S_FBUF:
		{
			int i;
			struct v4l2_framebuffer *fb = arg;
			__u32 printformat = __cpu_to_le32(fb->fmt.pixelformat);

			dprintk(2, "%s: ioctl VIDIOC_S_FBUF (v4l2): base=0x%p, "
				"size=%dx%d, bpl=%d, fmt=0x%x (%4.4s)\n",
				zr->name, fb->base, fb->fmt.width,
				fb->fmt.height, fb->fmt.bytesperline, fb->fmt.pixelformat,
				(char*)&printformat);

			for (i=0;i<ZORAN_FORMATS;i++)
				if (zoran_formats[i].fourcc == fb->fmt.pixelformat)
					break;
			if (i == ZORAN_FORMATS) {
				printk(KERN_ERR "%s: ioctl VIDIOC_S_FBUF: format=0x%x (%4.4s) not allowed\n",
					zr->name, fb->fmt.pixelformat, (char*)&printformat);
				return -EINVAL;
			}

			return setup_fbuffer(file, fb->base, &zoran_formats[i],
					fb->fmt.width, fb->fmt.height, fb->fmt.bytesperline);
		}
		break;

	case VIDIOC_REQBUFS:
		{
			struct v4l2_requestbuffers *req = arg;
			int do_alloc = 1, calc_size;

			if (fh->v4l_buffers.allocated || fh->jpg_buffers.allocated) {
				printk(KERN_ERR "%s: VIDIOC_REQBUFS (v4l2): buffers allready allocated\n", zr->name);
				return -EBUSY;
			}
			dprintk(2, "%s: ioctl VIDIOC_REQBUFS (v4l2): type=%d\n",
				zr->name, req->type);

			if ((req->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) ||
			    (req->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)) {
				printk(KERN_ERR "%s: ioctl VIDIOC_S_FMT: cannot handle capture/playback at the same time\n",
					zr->name);
				return -EINVAL;
			}

			switch (fh->map_mode) {
				case ZORAN_MAP_MODE_RAW:
					/* this should probably be changed to be what the user asks,
					 * but I'm lazy for now (in other words: TODO) */
					if (fh->v4l_buffers.secretly_allocated) {
						do_alloc = 0;
						fh->v4l_buffers.secretly_allocated = 0;
						fh->v4l_buffers.allocated = 1;
					}
					req->count = fh->v4l_buffers.num_buffers;
					if (do_alloc)
						if (v4l_fbuffer_alloc(file))
							return -ENOMEM;

					/* The next mmap will map the V4L buffers */
					/*fh->map_mode = ZORAN_MAP_MODE_RAW;*/

					break;

				case ZORAN_MAP_MODE_JPG_REC:
				case ZORAN_MAP_MODE_JPG_PLAY:
					if (req->count < 4)
						req->count = 4;
					else if (req->count > BUZ_MAX_FRAME)
						req->count = BUZ_MAX_FRAME;
					calc_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);

					if (fh->jpg_buffers.secretly_allocated) {
						if (fh->jpg_buffers.num_buffers == req->count &&
						    fh->jpg_buffers.buffer_size == calc_size) {
							do_alloc = 0;
							fh->jpg_buffers.secretly_allocated = 0;
							fh->jpg_buffers.allocated = 1;
						} else /* not doing this means memleaks */
							jpg_fbuffer_free(file);
					}

					/* we need to calculate size ourselves now */
					fh->jpg_buffers.num_buffers = req->count;
					fh->jpg_buffers.buffer_size = calc_size;

					if (do_alloc)
						if (jpg_fbuffer_alloc(file))
							return -ENOMEM;

					/* The next mmap will map the MJPEG buffers */
					/*fh->map_mode = (req->type==V4L2_BUF_TYPE_VIDEO_CAPTURE)?ZORAN_MAP_MODE_JPG_REC:ZORAN_MAP_MODE_JPG_PLAY;*/

					break;
				default:
					printk(KERN_ERR "%s: ioctl VIDIOC_REQBUFS: unknown type %d\n",
						zr->name, req->type);
					return -EINVAL;
			}

			return 0;
		}
		break;

	case VIDIOC_QUERYBUF:
		{
			struct v4l2_buffer *buf = arg;
			__u32 type = buf->type;
			int index = buf->index;

			dprintk(2, "%s: ioctl VIDIOC_QUERYBUF (v4l2): index=%d, type=%d\n",
				zr->name, buf->index, buf->type);

			memset(buf, 0, sizeof(buf));
			buf->type = type;
			buf->index = index;

			return zoran_v4l2_buffer_status(file, buf, buf->index);
		}
		break;

	case VIDIOC_QBUF:
		{
			struct v4l2_buffer *buf = arg;
			int res = 0;

			dprintk(4, "%s: ioctl VIDIOC_QBUF (v4l2): type=%d, index=%d\n",
				zr->name, buf->type, buf->index);

			if ((buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) ||
			    (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)) {
				printk(KERN_ERR "%s: ioctl VIDIOC_QBUF: cannot handle capture/playback at the same time\n",
					zr->name);
				return -EINVAL;
			}

			switch (fh->map_mode) {
				case ZORAN_MAP_MODE_RAW:
					res = zoran_v4l_queue_frame(file, buf->index);
					if (!res && !zr->v4l_memgrab_active &&
					    fh->v4l_buffers.active == ZORAN_LOCKED)
						zr36057_set_memgrab(zr, 1);
					break;

				case ZORAN_MAP_MODE_JPG_REC:
				case ZORAN_MAP_MODE_JPG_PLAY:
					res = zoran_jpg_queue_frame(file, buf->index,
						(fh->map_mode==ZORAN_MAP_MODE_JPG_REC)?
						BUZ_MODE_MOTION_COMPRESS:BUZ_MODE_MOTION_DECOMPRESS);
					if (!res && zr->codec_mode == BUZ_MODE_IDLE &&
					    fh->jpg_buffers.active == ZORAN_LOCKED) {
						zr36057_enable_jpg(zr,
							(fh->map_mode==ZORAN_MAP_MODE_JPG_REC)?
							BUZ_MODE_MOTION_COMPRESS:BUZ_MODE_MOTION_DECOMPRESS);
					}
					break;

				default:
					printk(KERN_ERR "%s: unsupported type %d\n",
						zr->name, buf->type);
					return -EINVAL;
			}

			return res;
		}
		break;

	case VIDIOC_DQBUF:
		{
			struct v4l2_buffer *buf = arg;
			int res = 0, num=-1;/* compiler borks here (?) */

			dprintk(4, "%s: ioctl VIDIOC_DQBUF (v4l2): type=%d\n",
				zr->name, buf->type);

			if ((buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY) ||
			    (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
			     fh->map_mode == ZORAN_MAP_MODE_JPG_PLAY)) {
				printk(KERN_ERR "%s: ioctl VIDIOC_DQBUF: cannot handle capture/playback at the same time\n",
					zr->name);
				return -EINVAL;
			}

			switch (fh->map_mode) {
				case ZORAN_MAP_MODE_RAW:
					num = zr->v4l_pend[zr->v4l_pend_tail & V4L_MASK_FRAME];
					if (file->f_flags & O_NONBLOCK) {
						if (zr->v4l_buffers.buffer[num].state != BUZ_STATE_DONE)
							return -EAGAIN;
					}
					res = v4l_sync(file, num);
					if (!res)
						res = zoran_v4l2_buffer_status(file, buf, num);
					break;

				case ZORAN_MAP_MODE_JPG_REC:
				case ZORAN_MAP_MODE_JPG_PLAY:
				{
					struct zoran_sync bs;
					if (file->f_flags & O_NONBLOCK) {
						num = zr->jpg_pend[zr->jpg_que_tail & BUZ_MASK_FRAME];
						if (zr->jpg_buffers.buffer[num].state != BUZ_STATE_DONE)
							return -EAGAIN;
					}
					res = jpg_sync(file, &bs);
					if (!res)
						res = zoran_v4l2_buffer_status(file, buf, bs.frame);
					break;
				}

				default:
					printk(KERN_ERR "%s: unsupported type %d\n",
						zr->name, buf->type);
					return -EINVAL;
			}

			return res;
		}
		break;

	case VIDIOC_STREAMON:
		{
			dprintk(2, "%s: ioctl VIDIOC_STREAMON (v4l2)\n", zr->name);

			switch (fh->map_mode) {
				case ZORAN_MAP_MODE_RAW: /* raw capture */
					if (zr->v4l_buffers.active != ZORAN_ACTIVE ||
					    fh->v4l_buffers.active != ZORAN_ACTIVE)
						return -EBUSY;

					zr->v4l_buffers.active = fh->v4l_buffers.active = ZORAN_LOCKED;

					if (!zr->v4l_memgrab_active &&
					    zr->v4l_pend_head != zr->v4l_pend_tail) {
						zr36057_set_memgrab(zr, 1);
					}
					break;

				case ZORAN_MAP_MODE_JPG_REC:
				case ZORAN_MAP_MODE_JPG_PLAY:
					/* what is the codec mode right now? */
					if (zr->jpg_buffers.active != ZORAN_ACTIVE ||
					    fh->jpg_buffers.active != ZORAN_ACTIVE)
						return -EBUSY;

					zr->jpg_buffers.active = fh->jpg_buffers.active = ZORAN_LOCKED;

					if (zr->jpg_que_head != zr->jpg_que_tail) {
						/* Start the jpeg codec when the first frame is queued  */
						jpeg_start(zr);
					}

					break;
				default:
					printk(KERN_ERR "%s: ioctl VIDIOC_STREAMON: invalid map mode %d\n",
						zr->name, fh->map_mode);
					return -EINVAL;
			}

			return 0;
		}
		break;

	case VIDIOC_STREAMOFF:
		{
			int i, res=0;

			dprintk(2, "%s: ioctl VIDIOC_STREAMOFF (v4l2)\n", zr->name);

			switch (fh->map_mode) {
				case ZORAN_MAP_MODE_RAW: /* raw capture */
					if (fh->v4l_buffers.active == ZORAN_FREE &&
					    zr->v4l_buffers.active != ZORAN_FREE)
						return -EPERM; /* stay off other's settings! */
					if (zr->v4l_buffers.active == ZORAN_FREE)
						return 0;

					/* unload capture */
					if (zr->v4l_memgrab_active)
						zr36057_set_memgrab(zr, 0);

					for (i=0;i<fh->v4l_buffers.num_buffers;i++)
						zr->v4l_buffers.buffer[i].state = BUZ_STATE_USER;
					fh->v4l_buffers = zr->v4l_buffers;

					zr->v4l_buffers.active = fh->v4l_buffers.active = ZORAN_FREE;

					break;

				case ZORAN_MAP_MODE_JPG_REC:
				case ZORAN_MAP_MODE_JPG_PLAY:
					if (fh->jpg_buffers.active == ZORAN_FREE &&
					    zr->jpg_buffers.active != ZORAN_FREE)
						return -EPERM; /* stay off other's settings! */
					if (zr->jpg_buffers.active == ZORAN_FREE)
						return 0;

					res = jpg_qbuf(file, -1,
					               (fh->map_mode==ZORAN_MAP_MODE_JPG_REC)?
					                BUZ_MODE_MOTION_COMPRESS:BUZ_MODE_MOTION_DECOMPRESS);

					break;
				default:
					printk(KERN_ERR "%s: ioctl VIDIOC_STREAMOFF: invalid map mode %d\n",
						zr->name, fh->map_mode);
					return -EINVAL;
			}

			return res;
		}
		break;

	case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *ctrl = arg;
			dprintk(2, "%s: ioctl VIDIOC_QUERYCTRL (v4l2): id=%d\n", zr->name, ctrl->id);

			/* we only support hue/saturation/contrast/brightness */
			if (ctrl->id < V4L2_CID_BRIGHTNESS ||
			    ctrl->id > V4L2_CID_HUE)
				return -EINVAL;
			else {
				int id = ctrl->id;
				memset(ctrl, 0, sizeof(*ctrl));
				ctrl->id = id;
			}

			switch (ctrl->id) {
				case V4L2_CID_BRIGHTNESS:
					strncpy(ctrl->name, "Brightness", 31);
					break;
				case V4L2_CID_CONTRAST:
					strncpy(ctrl->name, "Contrast", 31);
					break;
				case V4L2_CID_SATURATION:
					strncpy(ctrl->name, "Saturation", 31);
					break;
				case V4L2_CID_HUE:
					strncpy(ctrl->name, "Hue", 31);
					break;
			}

			ctrl->minimum = 0;
			ctrl->maximum = 65535;
			ctrl->step = 1;
			ctrl->default_value = 32768;
			ctrl->type = V4L2_CTRL_TYPE_INTEGER;

			return 0;
		}
		break;

	case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			dprintk(2, "%s: ioctl VIDIOC_G_CTRL (v4l2): id=%d\n", zr->name, ctrl->id);

			/* we only support hue/saturation/contrast/brightness */
			if (ctrl->id < V4L2_CID_BRIGHTNESS ||
			    ctrl->id > V4L2_CID_HUE)
				return -EINVAL;

			switch (ctrl->id) {
				case V4L2_CID_BRIGHTNESS:
					ctrl->value = zr->brightness;
					break;
				case V4L2_CID_CONTRAST:
					ctrl->value = zr->contrast;
					break;
				case V4L2_CID_SATURATION:
					ctrl->value = zr->saturation;
					break;
				case V4L2_CID_HUE:
					ctrl->value = zr->hue;
					break;
			}

			return 0;
		}
		break;

	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			struct video_picture pict;

			dprintk(2, "%s: ioctl VIDIOC_S_CTRL (v4l2): id=%d\n", zr->name, ctrl->id);

			/* we only support hue/saturation/contrast/brightness */
			if (ctrl->id < V4L2_CID_BRIGHTNESS ||
			    ctrl->id > V4L2_CID_HUE)
				return -EINVAL;

			if (ctrl->value < 0 ||
			    ctrl->value > 65535) {
				printk(KERN_ERR "%s: ioctl VIDIOC_S_CTRL: invalid value %d for id=%d\n",
					zr->name, ctrl->value, ctrl->id);
				return -EINVAL;
			}

			pict.brightness = zr->brightness;
			pict.contrast = zr->contrast;
			pict.colour = zr->saturation;
			pict.hue = zr->hue;
			switch (ctrl->id) {
				case V4L2_CID_BRIGHTNESS:
					pict.brightness = zr->brightness = ctrl->value;
					break;
				case V4L2_CID_CONTRAST:
					pict.contrast = zr->contrast = ctrl->value;
					break;
				case V4L2_CID_SATURATION:
					pict.colour = zr->saturation = ctrl->value;
					break;
				case V4L2_CID_HUE:
					pict.hue = zr->hue = ctrl->value;
					break;
			}

			decoder_command(zr, DECODER_SET_PICTURE, &pict);

			return 0;
		}
		break;

	case VIDIOC_ENUMSTD:
		{
			struct v4l2_standard *std = arg;
			dprintk(2, "%s: ioctl VIDIOC_ENUMSTD (v4l2): index=%d\n",
				zr->name, std->index);

			if (std->index < 0 || std->index >= (zr->card->norms+1))
				return -EINVAL;
			else {
				int id = std->index;
				memset(std, 0, sizeof(*std));
				std->index = id;
			}

			if (std->index == zr->card->norms) {
				std->id = V4L2_STD_ALL;
				strncpy(std->name, "Autodetect", 31);
				return 0;
			}
			switch (std->index) {
				case 0:
					std->id = V4L2_STD_PAL;
					strncpy(std->name, "PAL", 31);
					std->frameperiod.numerator = 25;
					std->frameperiod.denominator = 1;
					std->framelines = zr->card->tvn[0]->Ht;
					break;
				case 1:
					std->id = V4L2_STD_NTSC;
					strncpy(std->name, "NTSC", 31);
					std->frameperiod.numerator = 30000;
					std->frameperiod.denominator = 1001;
					std->framelines = zr->card->tvn[1]->Ht;
					break;
				case 2:
					std->id = V4L2_STD_SECAM;
					strncpy(std->name, "SECAM", 31);
					std->frameperiod.numerator = 25;
					std->frameperiod.denominator = 1;
					std->framelines = zr->card->tvn[2]->Ht;
					break;
			}

			return 0;
		}
		break;

	case VIDIOC_G_STD:
		{
			v4l2_std_id *std = arg;
			dprintk(2, "%s: ioctl VIDIOC_G_STD (v4l2)\n", zr->name);

			switch (zr->norm) {
				case VIDEO_MODE_PAL:
					*std = V4L2_STD_PAL;
					break;
				case VIDEO_MODE_NTSC:
					*std = V4L2_STD_NTSC;
					break;
				case VIDEO_MODE_SECAM:
					*std = V4L2_STD_SECAM;
					break;
			}

			return 0;
		}
		break;

	case VIDIOC_S_STD:
		{
			int norm = -1, res;
			v4l2_std_id *std = arg;
			dprintk(2, "%s: ioctl VIDIOC_S_STD (v4l2): norm=0x%llx\n",
				zr->name, *std);

			switch (*std) {
				case V4L2_STD_PAL:
					norm = VIDEO_MODE_PAL;
					break;
				case V4L2_STD_NTSC:
					norm = VIDEO_MODE_NTSC;
					break;
				case V4L2_STD_SECAM:
					norm = VIDEO_MODE_SECAM;
					break;
				case V4L2_STD_ALL:
					norm = VIDEO_MODE_AUTO;
					break;
			}

			if ((res = zoran_set_norm(zr, norm)))
				return res;

			return wait_grab_pending(zr);
		}
		break;

	case VIDIOC_ENUMINPUT:
		{
			struct v4l2_input *inp = arg;
			int status;

			dprintk(2, "%s: ioctl VIDIOC_ENUMINPUT (v4l2): index=%d\n", zr->name, inp->index);

			if (inp->index < 0 || inp->index >= zr->card->inputs)
				return -EINVAL;
			else {
				int id = inp->index;
				memset(inp, 0, sizeof(*inp));
				inp->index = id;
			}

			strncpy(inp->name, zr->card->input[inp->index].name, sizeof(inp->name)-1);
			inp->type = V4L2_INPUT_TYPE_CAMERA;
			inp->std = V4L2_STD_ALL;

			/* Get status of video decoder */
			decoder_command(zr, DECODER_GET_STATUS, &status);

			if (!(status & DECODER_STATUS_GOOD)) {
				inp->status |= V4L2_IN_ST_NO_POWER;
				inp->status |= V4L2_IN_ST_NO_SIGNAL;
			}
			if (!(status & DECODER_STATUS_COLOR))
				inp->status |= V4L2_IN_ST_NO_COLOR;

			return 0;
		}
		break;

	case VIDIOC_G_INPUT:
		{
			int *input = arg;
			dprintk(2, "%s: ioctl VIDIOC_G_INPUT (v4l2)\n", zr->name);

			*input = zr->input;

			return 0;
		}
		break;

	case VIDIOC_S_INPUT:
		{
			int *input = arg, res;
			dprintk(2, "%s: ioctl VIDIOC_S_INPUT (v4l2): input=%d\n", zr->name, *input);

			if ((res = zoran_set_input(zr, *input)))
				return res;

			/* Make sure the changes come into effect */
			return wait_grab_pending(zr);
		}
		break;

	case VIDIOC_ENUMOUTPUT:
		{
			struct v4l2_output *outp = arg;
			dprintk(2, "%s: ioctl VIDIOC_ENUMOUTPUT: index=%d\n", zr->name, outp->index);

			if (outp->index != 0)
				return -EINVAL;

			memset(outp, 0, sizeof(*outp));
			outp->index = 0;
			outp->type = V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY;
			strncpy(outp->name, "Autodetect", 31);

			return 0;
		}
		break;

	case VIDIOC_G_OUTPUT:
		{
			int *output = arg;

			*output = 0;

			return 0;
		}
		break;

	case VIDIOC_S_OUTPUT:
		{
			int *output = arg;

			if (*output != 0)
				return -EINVAL;

			return 0;
		}
		break;

	/* (Ronald) Somebody _PLEASE_ explain me how to use cropping. There's no documentation
	 * whatsoever about what the values in v4l2_cropcap are supposed to mean, I'm especially
	 * uncertain about {min,max}_[xy] */

	case VIDIOC_CROPCAP:
		{
			//struct v4l2_cropcap *ccap = arg;
			printk(KERN_ERR "%s: ioctl VIDIOC_CROPCAP (v4l2) not implemented yet\n", zr->name);
			return -EINVAL;
		}
		break;

	case VIDIOC_G_CROP:
		{
			//struct v4l2_crop *crop = arg;
			printk(KERN_ERR "%s: ioctl VIDIOC_G_CROP (v4l2) not implemented yet\n", zr->name);
			return -EINVAL;
		}
		break;

	case VIDIOC_S_CROP:
		{
			//struct v4l2_crop *crop = arg;
			printk(KERN_ERR "%s: ioctl VIDIOC_S_CROP (v4l2) not implemented yet\n", zr->name);
			return -EINVAL;
		}
		break;

	case VIDIOC_G_JPEGCOMP:
		{
			struct v4l2_jpegcompression *params = arg;
			dprintk(2, "%s: ioctl VIDIOC_G_JPEGCOMP (v4l2)\n", zr->name);

			memset(params, 0, sizeof(*params));
			params->quality = fh->jpg_settings.jpg_comp.quality;
			params->APPn = fh->jpg_settings.jpg_comp.APPn;
			memcpy(params->APP_data, fh->jpg_settings.jpg_comp.APP_data, fh->jpg_settings.jpg_comp.APP_len);
			params->APP_len = fh->jpg_settings.jpg_comp.APP_len;
			memcpy(params->COM_data, fh->jpg_settings.jpg_comp.COM_data, fh->jpg_settings.jpg_comp.COM_len);
			params->COM_len = fh->jpg_settings.jpg_comp.COM_len;
			params->jpeg_markers = fh->jpg_settings.jpg_comp.jpeg_markers;

			return 0;
		}
		break;

	case VIDIOC_S_JPEGCOMP:
		{
			struct v4l2_jpegcompression *params = arg;
			dprintk(2, "%s: ioctl VIDIOC_S_JPEGCOMP (v4l2): quality=%d, APPN=%d, APP_len=%d, COM_len=%d\n",
				zr->name, params->quality, params->APPn, params->APP_len, params->COM_len);

			if (fh->v4l_buffers.active != ZORAN_FREE || fh->jpg_buffers.active != ZORAN_FREE) {
				printk(KERN_WARNING "%s: ioctl VIDIOC_S_JPEGCOMP called while in playback/capture mode\n",
					zr->name);
				return -EBUSY;
			}

			if (params->quality > 100)
				params->quality = 100;
			if (params->quality < 5)
				params->quality = 5;
			fh->jpg_settings.jpg_comp.quality = params->quality;

			if (params->APPn < 0)
				params->APPn = 0;
			if (params->APPn > 15)
				params->APPn = 15;
			fh->jpg_settings.jpg_comp.APPn = params->APPn;

			if (params->APP_len < 0)
				params->APP_len = 0;
			if (params->APP_len > 60)
				params->APP_len = 60;
			fh->jpg_settings.jpg_comp.APP_len = params->APP_len;
			memcpy(fh->jpg_settings.jpg_comp.APP_data, params->APP_data, params->APP_len);

			if (params->COM_len < 0)
				params->COM_len = 0;
			if (params->COM_len > 60)
				params->COM_len = 60;
			fh->jpg_settings.jpg_comp.COM_len = params->COM_len;
			memcpy(fh->jpg_settings.jpg_comp.COM_data, params->COM_data, params->COM_len);

			fh->jpg_settings.jpg_comp.jpeg_markers = params->jpeg_markers;

			if (!fh->jpg_buffers.allocated)
				fh->jpg_buffers.buffer_size = zoran_v4l2_calc_bufsize(&fh->jpg_settings);

			return 0;
		}
		break;

	case VIDIOC_QUERYSTD: /* why is this useful? */
		{
			v4l2_std_id *std = arg;

			dprintk(2, "%s: ioctl VIDIOC_QUERY_STD (v4l2): std=0x%llx\n",
				zr->name, *std);

			switch (*std) {
				case V4L2_STD_ALL:
				case V4L2_STD_NTSC:
				case V4L2_STD_PAL:
					break;
				case V4L2_STD_SECAM:
					if (zr->card->norms == 3)
						break;
					/* else ... fall-through */
				default:
					return -EINVAL;
			}

			return 0;
		}
		break;

	case VIDIOC_TRY_FMT:
		{
			struct v4l2_format *fmt = arg;
			int res;

			dprintk(2, "%s: ioctl VIDIOC_TRY_FMT (v4l2): type=%d\n",
				zr->name, fmt->type);

			switch (fmt->type) {
				case V4L2_BUF_TYPE_VIDEO_OVERLAY:
					if (fmt->fmt.win.w.width > BUZ_MAX_WIDTH)
						fmt->fmt.win.w.width = BUZ_MAX_WIDTH;
					if (fmt->fmt.win.w.width < BUZ_MIN_WIDTH)
						fmt->fmt.win.w.width = BUZ_MIN_WIDTH;
					if (fmt->fmt.win.w.height > BUZ_MAX_HEIGHT)
						fmt->fmt.win.w.height = BUZ_MAX_HEIGHT;
					if (fmt->fmt.win.w.height < BUZ_MIN_HEIGHT)
						fmt->fmt.win.w.height = BUZ_MIN_HEIGHT;
					break;

				case V4L2_BUF_TYPE_VIDEO_CAPTURE:
				case V4L2_BUF_TYPE_VIDEO_OUTPUT:
					if (fmt->fmt.pix.bytesperline > 0)
						return -EINVAL;

					if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
						struct zoran_jpg_settings settings = fh->jpg_settings;

						/* we actually need to set 'real' parameters now */
						if ((fmt->fmt.pix.height*2) > BUZ_MAX_HEIGHT)
							settings.TmpDcm = 1;
						else
							settings.TmpDcm = 2;
						settings.decimation = 0;
						if (fmt->fmt.pix.height <= fh->jpg_settings.img_height/2)
							settings.VerDcm = 2;
						else
							settings.VerDcm = 1;
						if (fmt->fmt.pix.width <= fh->jpg_settings.img_width/4)
							settings.HorDcm = 4;
						else if (fmt->fmt.pix.width <= fh->jpg_settings.img_width/2)
							settings.HorDcm = 2;
						else
							settings.HorDcm = 1;
						if (settings.TmpDcm == 1)
							settings.field_per_buff = 2;
						else
							settings.field_per_buff = 1;

						/* check */
						if ((res = zoran_check_jpg_settings(zr, &settings)))
							return res;

						/* tell the user what we actually did */
						fmt->fmt.pix.width = settings.img_width / settings.HorDcm;
						fmt->fmt.pix.height = settings.img_height*2 / (settings.TmpDcm*settings.VerDcm);
						if (settings.TmpDcm == 1)
							fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_SEQ_TB:V4L2_FIELD_SEQ_BT);
						else
							fmt->fmt.pix.field = (fh->jpg_settings.odd_even?V4L2_FIELD_TOP:V4L2_FIELD_BOTTOM);

						fmt->fmt.pix.sizeimage = zoran_v4l2_calc_bufsize(&fh->jpg_settings);
					} else if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
						int i;
						for (i=0;i<ZORAN_FORMATS;i++)
							if (zoran_formats[i].fourcc == fmt->fmt.pix.pixelformat)
								break;
						if (i == ZORAN_FORMATS)
							return -EINVAL;

						if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
							fmt->fmt.pix.width = BUZ_MAX_WIDTH;
						if (fmt->fmt.pix.width < BUZ_MIN_WIDTH)
							fmt->fmt.pix.width = BUZ_MIN_WIDTH;
						if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
							fmt->fmt.pix.height = BUZ_MAX_HEIGHT;
						if (fmt->fmt.pix.height < BUZ_MIN_HEIGHT)
							fmt->fmt.pix.height = BUZ_MIN_HEIGHT;
					} else
						return -EINVAL;
					break;

				default:
					return -EINVAL;
			}

			return 0;
		}
		break;

	case VIDIOC_G_PARM:
	case VIDIOC_S_PARM:
		printk(KERN_ERR "%s: TODO\n", zr->name);
		return -EINVAL; /* (Ronald) I'm lazy, aren't I? */

#endif

	default:
		dprintk(1, KERN_DEBUG "%s: UNKNOWN ioctl cmd: 0x%x\n", zr->name, cmd);
		return -ENOIOCTLCMD;
		break;

	}
	return 0;
}


static int
zoran_ioctl (struct inode *inode,
             struct file  *file,
             unsigned int  cmd,
             unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, zoran_do_ioctl);
}

static unsigned int
zoran_poll (struct file *file,
            poll_table  *wait)
{
 	//struct zoran_fh *fh = file->private_data;
	//struct zoran *zr = fh->zr;

	/* we should check whether buffers are ready to be synced on (w/o waits - O_NONBLOCK) here
	 * if ready for read (sync), return POLLIN|POLLRDNORM,
	 * if ready for write (sync), return POLLOUT|POLLWRNORM,
	 * if error, return POLLERR,
	 * if no buffers queued or so, return POLLNVAL
	 */

	return POLLERR;
}


/*
 * This maps the buffers to user space.
 *
 * Depending on the state of fh->map_mode
 * the V4L or the MJPEG buffers are mapped
 * per buffer or all together
 *
 * Note that we need to connect to some
 * unmap signal event to unmap the de-allocate
 * the buffer accordingly (zoran_vm_close())
 */

static void
zoran_vm_open (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;
	map->count++;
}

static void
zoran_vm_close (struct vm_area_struct *vma)
{
	struct zoran_mapping *map = vma->vm_private_data;
	struct file *file = map->file;
	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	int i;

	map->count--;
	if (map->count == 0) {
		switch (fh->map_mode) {
			case ZORAN_MAP_MODE_JPG_REC:
			case ZORAN_MAP_MODE_JPG_PLAY:

				dprintk(2, KERN_INFO "%s: munmap(MJPEG)\n",
					zr->name);

				for (i=0;i<fh->jpg_buffers.num_buffers;i++) {
					if (fh->jpg_buffers.buffer[i].map == map) {
						fh->jpg_buffers.buffer[i].map = NULL;
					}
				}
				kfree(map);

				for (i=0;i<fh->jpg_buffers.num_buffers;i++)
					if (fh->jpg_buffers.buffer[i].map)
						break;
				if (i == fh->jpg_buffers.num_buffers) {
					if (fh->jpg_buffers.active != ZORAN_FREE) {
						jpg_qbuf(file, -1, zr->codec_mode);
						zr->jpg_buffers.allocated = 0;
						zr->jpg_buffers.active = fh->jpg_buffers.active = ZORAN_FREE;
					}
					/* see below */
					//jpg_fbuffer_free(file);
					fh->jpg_buffers.allocated = 0;
					fh->jpg_buffers.secretly_allocated = 1;
				}

				break;

			case ZORAN_MAP_MODE_RAW:

				dprintk(2, KERN_INFO "%s: munmap(V4L)\n",
					zr->name);

				for (i=0;i<fh->v4l_buffers.num_buffers;i++) {
					if (fh->v4l_buffers.buffer[i].map == map) {
						/* unqueue/unmap */
						fh->v4l_buffers.buffer[i].map = NULL;
					}
				}
				kfree(map);

				for (i=0;i<fh->v4l_buffers.num_buffers;i++)
					if (fh->v4l_buffers.buffer[i].map)
						break;
				if (i == fh->v4l_buffers.num_buffers) {
					if (fh->v4l_buffers.active != ZORAN_FREE) {
						zr36057_set_memgrab(zr, 0);
						zr->v4l_buffers.allocated = 0;
						zr->v4l_buffers.active = fh->v4l_buffers.active = ZORAN_FREE;
					}
					/* needs to be commented out, though I'd rather
					 * free the buffers here, but that somehow
					 * oopses the kernel (only when done inside
					 * munmap()-handlers). So commented out (Ronald)
					 */
					//v4l_fbuffer_free(file);
					/* this hack is to make sure we can mmap() more than
					 * once per open() without screwing up */
					fh->v4l_buffers.allocated = 0;
					fh->v4l_buffers.secretly_allocated = 1;
				}

				break;

			default:
				printk(KERN_ERR "%s: munmap(): internal error - unknown map mode %d\n",
					zr->name, fh->map_mode);
				break;

		}
	}
}

static struct vm_operations_struct zoran_vm_ops =
{
	open:     zoran_vm_open,
	close:    zoran_vm_close,
};

static int
zoran_mmap (struct file           *file,
            struct vm_area_struct *vma)
{
 	struct zoran_fh *fh = file->private_data;
	struct zoran *zr = fh->zr;
	unsigned long size = (vma->vm_end - vma->vm_start);
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	int i, j;
	unsigned long page, start = vma->vm_start, todo, pos, fraglen;
	int first, last;
	struct zoran_mapping *map;

	dprintk(2, KERN_INFO "%s: mmap(%s) of 0x%08lx-0x%08lx (size=%lu)\n",
		zr->name, fh->map_mode==ZORAN_MAP_MODE_RAW?"V4L":"MJPEG", vma->vm_start, vma->vm_end, size);

	if (!(vma->vm_flags & VM_SHARED) ||
	    !(vma->vm_flags & VM_READ) ||
	    !(vma->vm_flags & VM_WRITE)) {
		printk(KERN_ERR "%s: mmap(): no MAP_SHARED/PROT_{READ,WRITE} given\n", zr->name);
		return -EINVAL;
	}

	switch (fh->map_mode) {

		case ZORAN_MAP_MODE_JPG_REC:
		case ZORAN_MAP_MODE_JPG_PLAY:

			/* Map the MJPEG buffers */
			if (!fh->jpg_buffers.allocated) {
				printk(KERN_ERR "%s: zoran_mmap(MJPEG): buffers not yet allocated\n", zr->name);
				return -ENOMEM;
			}

			first = offset / fh->jpg_buffers.buffer_size;
			last = first - 1 + size / fh->jpg_buffers.buffer_size;
			if (offset % fh->jpg_buffers.buffer_size != 0 ||
			    size % fh->jpg_buffers.buffer_size != 0 ||
			    first < 0 || last < 0 ||
			    first >= fh->jpg_buffers.num_buffers ||
			    last >= fh->jpg_buffers.num_buffers) {
				printk(KERN_ERR "%s: mmap(MJPEG) - offset=%lu or size=%lu invalid for bufsize=%d and numbufs=%d\n",
					zr->name, offset, size,
					fh->jpg_buffers.buffer_size, fh->jpg_buffers.num_buffers);
				return -EINVAL;
			}
			for (i=first;i<=last;i++) {
				if (fh->jpg_buffers.buffer[i].map) {
					printk(KERN_ERR "%s: mmap(MJPEG) - buffer %d already mapped\n",
						zr->name, i);
					return -EBUSY;
				}
			}

			/* map these buffers (v4l_buffers[i]) */
			map = kmalloc(sizeof(struct zoran_mapping), GFP_KERNEL);
			if (!map)
				return -ENOMEM;
			map->file = file;
			map->count = 1;

			vma->vm_ops = &zoran_vm_ops;
			vma->vm_flags |= VM_DONTEXPAND;
			vma->vm_private_data = map;

			for (i=first;i<=last;i++) {
				for (j = 0; j < fh->jpg_buffers.buffer_size / PAGE_SIZE; j++) {
					fraglen = (fh->jpg_buffers.buffer[i].frag_tab[2 * j + 1] & ~1) << 1;
					todo = size;
					if (todo > fraglen)
						todo = fraglen;
					pos = (unsigned long) fh->jpg_buffers.buffer[i].frag_tab[2 * j];
					page = virt_to_phys(bus_to_virt(pos));	/* should just be pos on i386 */
					if (remap_page_range(start, page, todo, PAGE_SHARED)) {
						printk(KERN_ERR "%s: zoran_mmap(V4L): remap_page_range failed\n", zr->name);
						return -EAGAIN;
					}
					size -= todo;
					start += todo;
					if (size == 0)
						break;
					if (fh->jpg_buffers.buffer[i].frag_tab[2 * j + 1] & 1)
						break;	/* was last fragment */
				}
				fh->jpg_buffers.buffer[i].map = map;
				if (size == 0)
					break;

			}

			break;

		case ZORAN_MAP_MODE_RAW:

			/* Map the V4L buffers */
			if (!fh->v4l_buffers.allocated) {
				printk(KERN_ERR "%s: zoran_mmap(V4L): buffers not yet allocated\n", zr->name);
				return -ENOMEM;
			}

			first = offset / fh->v4l_buffers.buffer_size;
			last = first - 1 + size / fh->v4l_buffers.buffer_size;
			if (offset % fh->v4l_buffers.buffer_size != 0 ||
			    size % fh->v4l_buffers.buffer_size != 0 ||
			    first < 0 || last < 0 ||
			    first >= fh->v4l_buffers.num_buffers ||
			    last >= fh->v4l_buffers.buffer_size) {
				printk(KERN_ERR "%s: mmap(V4L) - offset=%lu or size=%lu invalid for bufsize=%d and numbufs=%d\n",
					zr->name, offset, size,
					fh->v4l_buffers.buffer_size,
					fh->v4l_buffers.num_buffers);
				return -EINVAL;
			}
			for (i=first;i<=last;i++) {
				if (fh->v4l_buffers.buffer[i].map) {
					printk(KERN_ERR "%s: mmap(V4L) - buffer %d already mapped\n",
						zr->name, i);
					return -EBUSY;
				}
			}

			/* map these buffers (v4l_buffers[i]) */
			map = kmalloc(sizeof(struct zoran_mapping), GFP_KERNEL);
			if (!map)
				return -ENOMEM;
			map->file = file;
			map->count = 1;

			vma->vm_ops = &zoran_vm_ops;
			vma->vm_flags |= VM_DONTEXPAND;
			vma->vm_private_data = map;

			for (i=first;i<=last;i++) {
				todo = size;
				if (todo > fh->v4l_buffers.buffer_size)
					todo = fh->v4l_buffers.buffer_size;
				page = fh->v4l_buffers.buffer[i].fbuffer_phys;
				if (remap_page_range(start, page, todo, PAGE_SHARED)) {
					printk(KERN_ERR "%s: zoran_mmap(V4L): remap_page_range failed\n", zr->name);
					return -EAGAIN;
				}
				size -= todo;
				start += todo;
				fh->v4l_buffers.buffer[i].map = map;
				if (size == 0)
					break;
			}

			break;

		default:
			printk(KERN_ERR "%s: mmap(): internal error - unknown map mode %d\n",
				zr->name, fh->map_mode);
			break;
	}

	return 0;
}

static struct file_operations zoran_fops =
{
	owner:		THIS_MODULE,
	open:		zoran_open,
	release:	zoran_close,
	ioctl:		zoran_ioctl,
	llseek:		no_llseek,
	read:		zoran_read,
	write:		zoran_write,
	mmap:		zoran_mmap,
	poll:		zoran_poll,
};

static struct video_device zoran_template =
{
	name:		ZORAN_NAME,
	type:		ZORAN_VID_TYPE,
#ifdef HAVE_V4L2
	type2:		ZORAN_V4L2_VID_FLAGS,
#endif
	hardware:	ZORAN_HARDWARE,
	fops:		&zoran_fops,
	minor:		-1
};

static void test_interrupts(struct zoran *zr)
{
	int timeout, icr;

	clear_interrupt_counters(zr);
	zr->testing = 1;
	icr = btread(ZR36057_ICR);
	btwrite(0x78000000 | ZR36057_ICR_IntPinEn, ZR36057_ICR);
	timeout = interruptible_sleep_on_timeout(&zr->test_q, 1 * HZ);
	btwrite(0, ZR36057_ICR);
	btwrite(0x78000000, ZR36057_ISR);
	zr->testing = 0;
	dprintk(0, KERN_INFO "%s: Testing interrupts...\n", zr->name);
	if (timeout) {
		dprintk(0, ": time spent: %d\n", 1 * HZ - timeout);
	}
	print_interrupts(zr);
	btwrite(icr, ZR36057_ICR);
}

static int zr36057_init(int i)
{
	struct zoran *zr = &zoran[i];
	unsigned long mem;
	unsigned mem_needed;
	int j;
	int two = 2;
	int zero = 0;

	dprintk(0, KERN_INFO "%s: Initializing card[%d], zr=%x\n", zr->name, i, (int) zr);

	/* default setup of all parameters which will persist beetween opens */

	zr->user = 0;
        
        init_waitqueue_head(&zr->v4l_capq);
        init_waitqueue_head(&zr->jpg_capq);
        init_waitqueue_head(&zr->test_q);

	init_MUTEX(&zr->res_lock);

 	zr->jpg_buffers.allocated = 0;
	zr->v4l_buffers.allocated = 0;

	zr->buffer.base = (void *) vidmem;
// 	zr->buffer.width = 0;
// 	zr->buffer.height = 0;
// 	zr->buffer.depth = 0;
// 	zr->buffer.bytesperline = 0;

	zr->norm = default_norm = (default_norm < 3 ? default_norm : VIDEO_MODE_PAL);	/* Avoid nonsense settings from user */
	if(!(zr->timing = zr->card->tvn[zr->norm])) {
                dprintk(0, KERN_WARNING "%s: default TV statdard not supported by hardware. PAL will be used.\n", zr->name);
                zr->norm = VIDEO_MODE_PAL;
                zr->timing = zr->card->tvn[zr->norm];
        }
        zr->input = default_input = (default_input ? 1 : 0);	/* Avoid nonsense settings from user */

	/* Should the following be reset at every open ? */
	zr->hue = 32768;
	zr->contrast = 32768;
	zr->saturation = 32768;
	zr->brightness = 32768;

// 	for (j = 0; j < VIDEO_MAX_FRAME; j++) {
// 		zr->v4l_buffers.buffer[i].fbuffer = 0;
// 		zr->v4l_buffers.buffer[i].fbuffer_phys = 0;
// 		zr->v4l_buffers.buffer[i].fbuffer_bus = 0;
// 	}

	/* default setup (will be repeated at every open) */

	zoran_open_init_params(zr);

	/* allocate memory *before* doing anything to the hardware in case allocation fails */
	mem_needed = BUZ_NUM_STAT_COM * 4;
	mem = (unsigned long) kmalloc(mem_needed, GFP_KERNEL);
	if (!mem) {
		dprintk(0, KERN_ERR "%s: zr36057_init: kmalloc (STAT_COM) failed\n", zr->name);
		return -ENOMEM;
	}
	memset((void *) mem, 0, mem_needed);

	zr->stat_com = (u32 *) mem;
	for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
		zr->stat_com[j] = 1;	/* mark as unavailable to zr36057 */
	}

	/* Initialize zr->jpg_buffers.buffer */
	for (j = 0; j < BUZ_MAX_FRAME; j++) {
		zr->jpg_buffers.buffer[j].frag_tab = 0;
		zr->jpg_buffers.buffer[j].frag_tab_bus = 0;
		zr->jpg_buffers.buffer[j].state = BUZ_STATE_USER;
		zr->jpg_buffers.buffer[j].bs.frame = j;
	}

	/*
	 *   Now add the template and register the device unit.
	 */
	memcpy(&zr->video_dev, &zoran_template, sizeof(zoran_template));
	strcpy(zr->video_dev.name, zr->name);
	if (video_register_device(&zr->video_dev, VFL_TYPE_GRABBER, -1) < 0) {
		zoran_unregister_i2c(zr);
		kfree((void *) zr->stat_com);
		return -1;
	}

	zoran_init_hardware(zr);

	if (debug > 2)
		detect_guest_activity(zr);

	test_interrupts(zr);

        if (!pass_through) {
	        // set_videobus_enable(zr, 0);
		decoder_command(zr, DECODER_ENABLE_OUTPUT, &zero);
		encoder_command(zr, ENCODER_SET_INPUT, &two);
	        // set_videobus_enable(zr, 1);
	}
	
        zr->zoran_proc = NULL;
	zr->initialized = 1;

	return 0;
}

#include "zoran_procfs.c"

static void zoran_release(void)
{
	int i;
	struct zoran *zr;

	for (i = 0; i < zoran_num; i++) {
		zr = &zoran[i];

		if (!zr->initialized)
			continue;

		/* unregister videocodec bus */
                if (zr->codec) {
                         struct videocodec_master *master =
                                                  zr->codec->master_data;
                         videocodec_detach(zr->codec);
                         if (master)
				kfree(master);
                }
                if (zr->vfe) {
                         struct videocodec_master *master =
                                                  zr->vfe->master_data;
                         videocodec_detach(zr->vfe);
                         if (master)
				kfree(master);
                }

		/* unregister i2c bus */
                zoran_unregister_i2c(zr);

		/* disable PCI bus-mastering */
                zoran_set_pci_master(zr, 0);

		/* put chip into reset */
		btwrite(0, ZR36057_SPGPPCR);

		free_irq(zr->pci_dev->irq, zr);

		/* unmap and free memory */

		kfree((void *) zr->stat_com);

		zoran_proc_cleanup(i);
		iounmap(zr->zr36057_mem);

		video_unregister_device(&zr->video_dev);
	}
}

static struct videocodec_master master_template_060 = {
	"(unset)",  // name is given later
	0L,  // magic not used
	CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER | CODEC_FLAG_JPEG | CODEC_FLAG_VFE,
	VID_HARDWARE_ZR36067,
	NULL, // date is filled later
	zr36060_read,
	zr36060_write,
};

static struct videocodec_master master_template_050 = {
	"(unset)",  // name is given later
	0L,  // magic not used
	CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER | CODEC_FLAG_JPEG,
	VID_HARDWARE_ZR36067,
	NULL, // date is filled later
	zr36050_read,
	zr36050_write,
};

static struct videocodec_master master_template_016 = {
	"(unset)",  // name is given later
	0L,  // magic not used
	CODEC_FLAG_ENCODER | CODEC_FLAG_DECODER | CODEC_FLAG_VFE,
	VID_HARDWARE_ZR36067,
	NULL, // date is filled later
	zr36016_read,
	zr36016_write,
};

/*
 *   Scan for a Buz card (actually for the PCI contoler ZR36057),
 *   request the irq and map the io memory
 */

static int find_zr36057(void)
{
	unsigned char latency, need_latency;
	struct zoran *zr;
	struct pci_dev *dev = NULL;
	int result;
        struct videocodec_master *master_vfe   = NULL;
        struct videocodec_master *master_codec = NULL;

	zoran_num = 0;

	while (zoran_num < BUZ_MAX && (dev = pci_find_device(PCI_VENDOR_ID_ZORAN, PCI_DEVICE_ID_ZORAN_36057, dev)) != NULL) {
		zr = &zoran[zoran_num];
		memset(zr, 0, sizeof(struct zoran));    // Just in case if previous cycle failed
                zr->pci_dev = dev;
		//zr->zr36057_mem = NULL;
		zr->id = zoran_num;
		sprintf(zr->name, "MJPEG[%u]", zr->id);

		spin_lock_init(&zr->spinlock);

                if (pci_enable_device(dev))
                        continue;

                zr->zr36057_adr = pci_resource_start(zr->pci_dev, 0);

		pci_read_config_byte(zr->pci_dev, PCI_CLASS_REVISION, &zr->revision);
		if (zr->revision < 2) {
			dprintk(0, KERN_INFO "%s: Zoran ZR36057 (rev %d) irq: %d, memory: 0x%08x.\n", zr->name,
			       zr->revision, zr->pci_dev->irq, zr->zr36057_adr);
		} else {
			unsigned short ss_vendor_id, ss_id;

                        ss_vendor_id = zr->pci_dev->subsystem_vendor;
                        ss_id = zr->pci_dev->subsystem_device;

			dprintk(0, KERN_INFO "%s: Zoran ZR36067 (rev %d) irq: %d, memory: 0x%08x\n", zr->name,
			       zr->revision, zr->pci_dev->irq, zr->zr36057_adr);
			dprintk(0, KERN_INFO "%s: subsystem vendor=0x%04x id=0x%04x\n", zr->name, ss_vendor_id, ss_id);
		}

		zr->zr36057_mem = ioremap_nocache(zr->zr36057_adr, 0x1000);
                if (!zr->zr36057_mem) {
                        dprintk(0, KERN_ERR "%s: ioremap failed\n", zr->name);
                        continue;
                }

		result = request_irq(zr->pci_dev->irq, zoran_irq, SA_SHIRQ | SA_INTERRUPT, zr->name, (void *) zr);
		if (result < 0) {
                	if (result == -EINVAL) {
				dprintk(0, KERN_ERR "%s: Bad irq number or handler\n", zr->name);
			} else if (result == -EBUSY) {
				dprintk(0, KERN_ERR "%s: IRQ %d busy, change your PnP config in BIOS\n", zr->name, zr->pci_dev->irq);
			} else {
				dprintk(0, KERN_ERR "%s: Can't assign irq, error code %d\n", zr->name, result);
			}
                        goto zr_unmap;
                }
                
		/* set PCI latency timer */
		pci_read_config_byte(zr->pci_dev, PCI_LATENCY_TIMER, &latency);
		need_latency = zr->revision > 1 ? 32 : 48;
                if (latency != need_latency) {
			dprintk(0, KERN_INFO "%s: Changing PCI latency from %d to %d.\n", zr->name, latency, need_latency);
			pci_write_config_byte(zr->pci_dev, PCI_LATENCY_TIMER, need_latency);
		}

	        zr36057_restart(zr);

	        /* i2c */
                dprintk(0, KERN_INFO "%s: Initializing i2c bus...\n", zr->name);
	        if (zoran_register_i2c(zr) < 0) {
                        dprintk(0, KERN_ERR "%s: Can't initialize i2c bus\n", zr->name);
		        goto zr_free_irq;
	        }

                if (zr->card == NULL) {
                        dprintk(0, KERN_ERR "%s: Card not supported\n", zr->name);
                        goto zr_unreg_i2c;
                }
                dprintk(0, KERN_INFO "%s card detected\n", zr->name);

                dprintk(0, KERN_INFO "%s: Initializing videocodec bus...\n", zr->name);

	        /* reset JPEG codec */
	        jpeg_codec_sleep(zr, 1);
                jpeg_codec_reset(zr);

		/* video bus enabled */

		/* display codec revision */
		switch (zr->card->type) {
		case DC10:
		case DC30plus:
			/* Setup ZR36050 */
			master_codec = kmalloc(sizeof(struct videocodec_master),GFP_KERNEL);
			if (!master_codec) {
				dprintk(0, KERN_ERR "%s: out of memory.\n",zr->name);
				goto zr_unreg_i2c;
			}
			memcpy(master_codec, &master_template_050, sizeof(struct videocodec_master));
			strcpy(master_codec->name,zr->name);
			master_codec->data = zr;
			zr->codec = videocodec_attach(master_codec);

			if (!zr->codec) {
				dprintk(0, KERN_ERR "%s: Zoran ZR36050 not found - defect?\n", zr->name);
				goto zr_free_codec;
			}
			if (zr->codec < 0) {
				dprintk(0, KERN_ERR "%s: Codec found, but not Zoran ZR36050.\n", zr->name);
				goto zr_detach_codec;
			}

			/* Setup ZR36016 */
			master_vfe = kmalloc(sizeof(struct videocodec_master),GFP_KERNEL);
			if (!master_vfe) {
				dprintk(0, KERN_ERR "%s: out of memory.\n",zr->name);
				goto zr_unreg_i2c;
			}
			memcpy(master_vfe, &master_template_016, sizeof(struct videocodec_master));
			strcpy(master_vfe->name,zr->name);
			master_vfe->data = zr;
			zr->vfe = videocodec_attach(master_vfe);

			if (!zr->vfe) {
				dprintk(0, KERN_ERR "%s: Zoran ZR36016 not found - defect?\n", zr->name);
				goto zr_free_vfe;
			}
			if (zr->vfe < 0) {
				dprintk(0, KERN_ERR "%s: VFE found, but not Zoran ZR36016.\n", zr->name);
				goto zr_detach_vfe;
			}
			break;

		case DC10plus:
		case LML33:
		case BUZ:
			/* Setup ZR36060 */
			master_codec = kmalloc(sizeof(struct videocodec_master),GFP_KERNEL);
			if (!master_codec) {
				dprintk(0, KERN_ERR "%s: out of memory.\n",zr->name);
				goto zr_unreg_i2c;
			}
			memcpy(master_codec, &master_template_060, sizeof(struct videocodec_master));
			strcpy(master_codec->name,zr->name);
			master_codec->data = zr;
			zr->codec = videocodec_attach(master_codec);

			if (!zr->codec) {
				dprintk(0, KERN_ERR "%s: Zoran ZR36060 not found - defect?\n", zr->name);
				goto zr_free_codec;
			}
			if (zr->codec < 0) {
				dprintk(0, KERN_ERR "%s: Codec found, but not Zoran ZR36060.\n", zr->name);
				goto zr_detach_codec;
			}
			break;

		default:
			dprintk(0, KERN_ERR "%s: Card not supported (codec).\n", zr->name);
			goto zr_unreg_i2c;
			break;
		}

		zoran_num++;
		continue;

		// Init errors
                zr_free_vfe:
                       kfree(master_vfe);

                zr_detach_vfe:
                       videocodec_detach(zr->vfe);

                zr_free_codec:
                       kfree(master_codec);

                zr_detach_codec:
                       videocodec_detach(zr->codec);

		zr_unreg_i2c:
                        zoran_unregister_i2c(zr);

                zr_free_irq:
                        btwrite(0, ZR36057_SPGPPCR);
		        free_irq(zr->pci_dev->irq, zr);

                zr_unmap:
                        iounmap(zr->zr36057_mem);

                continue;
	}
	if (zoran_num == 0) {
		dprintk(0, KERN_INFO "No known MJPEG cards found.\n");
	}
	return zoran_num;
}

#include "chipsets.h"

static void handle_chipset(void)
{
	int index;
	struct pci_dev *dev = NULL;

	for (index = 0; index < sizeof(black) / sizeof(black[0]); index++) {
		if ((dev = pci_find_device(black[index].vendor, black[index].device, dev)) != NULL) {
			dprintk(0, KERN_INFO "MJPEG: Host bridge: %s, ", black[index].name);
			switch (black[index].action) {

			case TRITON:
				dprintk(0, KERN_INFO "enabling Triton support.\n");
				triton = 1;
				break;

			case NATOMA:
				dprintk(0, KERN_INFO "enabling Natoma workaround.\n");
				natoma = 1;
				break;
			}
		}
	}
}

#ifdef MODULE
int init_module(void)
#else
int init_dc10_cards(struct video_init *unused)
#endif
{
	int i;

        memset(zoran, 0, sizeof(zoran));
	printk(KERN_INFO "Zoran ZR36050/60 + ZR36057/67 MJPEG board driver version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);

	/* Look for cards */

	if (find_zr36057() < 0) {
		return -EIO;
	}
	if (zoran_num == 0)
		return 0; //-ENXIO;

	dprintk(0, KERN_INFO "MJPEG: %d card(s) found\n", zoran_num);

	/* check the parameters we have been given, adjust if necessary */

	if (v4l_nbufs < 0)
		v4l_nbufs = 0;
	if (v4l_nbufs > VIDEO_MAX_FRAME)
		v4l_nbufs = VIDEO_MAX_FRAME;
	/* The user specfies the in KB, we want them in byte (and page aligned) */
	v4l_bufsize = PAGE_ALIGN(v4l_bufsize * 1024);
	if (v4l_bufsize < 32768)
		v4l_bufsize = 32768;
	/* 2 MB is arbitrary but sufficient for the maximum possible images */
	if (v4l_bufsize > 2048 * 1024)
		v4l_bufsize = 2048 * 1024;

	dprintk(1, KERN_INFO "MJPEG: using %d V4L buffers of size %d KB\n", v4l_nbufs, v4l_bufsize >> 10);

	/* Use parameter for vidmem or try to find a video card */

	if (vidmem) {
		dprintk(0, KERN_INFO "MJPEG: Using supplied video memory base address @ 0x%lx\n", vidmem);
	} 
	/* check if we have a Triton or Natome chipset */

	handle_chipset();

	/* take care of Natoma chipset and a revision 1 zr36057 */

	for (i = 0; i < zoran_num; i++) {
		if (natoma && zoran[i].revision <= 1) {
			zoran[i].jpg_buffers.need_contiguous = 1;
			dprintk(0, KERN_INFO "%s: ZR36057/Natoma bug, max. buffer size is 128K\n", zoran[i].name);
		}  else {
			zoran[i].jpg_buffers.need_contiguous = 0; //Cleared by memset in find_zr36057()
		}
	}

	/* initialize the cards */

	/* We have to know which ones must be released if an error occurs */
	//for (i = 0; i < zoran_num; i++)
	//	zoran[i].initialized = 0; Cleared by memset in find_zr36057()

	for (i = 0; i < zoran_num; i++) {
		if (zr36057_init(i) < 0) {
			zoran_release();
			return -EIO;
		}
		zoran_proc_init(i);
	}

	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	zoran_release();
}

#endif
