/* 
    vpx3220a, vpx3216b & vpx3214c video decoder driver version 0.0.1

    Copyright (C) 2001 Laurent Pinchart <lpinchart@freegates.be>

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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("vpx3220a/vpx3216b/vpx3214c video encoder driver");
MODULE_AUTHOR("Laurent Pinchart");
MODULE_LICENSE("GPL");

#include <linux/videodev.h>
#include <linux/version.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/i2c-old.h>
#include <linux/videodev.h>
#include <linux/video_decoder.h>

#define DEBUG(x)       x /* Debug driver */   

/* ----------------------------------------------------------------------- */

struct vpx3220
{
	struct i2c_bus	*bus;
	int		addr;
	unsigned char	reg[256];
	unsigned short	fpram[256];

	int		part;
	int		norm;
	int		enable;
	int		bright;
	int		contrast;
	int		hue;
	int		sat;
};

#define I2C_VPX3220        0x86
#define I2C_DELAY          10
#define VPX_TIMEOUT_COUNT  10

/* ----------------------------------------------------------------------- */

static
int vpx3220_write(struct vpx3220 *dev, unsigned char subaddr, unsigned char data)
{
	int ack;

	LOCK_I2C_BUS(dev->bus);

	i2c_start(dev->bus);
	i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	i2c_sendbyte(dev->bus, subaddr, I2C_DELAY);
	ack = i2c_sendbyte(dev->bus, data, I2C_DELAY);
	dev->reg[subaddr] = data;
	i2c_stop(dev->bus);

	UNLOCK_I2C_BUS(dev->bus);

	return ack;
}

static 
unsigned char vpx3220_read(struct vpx3220 *dev, unsigned char subaddr)
{
	unsigned char data;

	LOCK_I2C_BUS(dev->bus);

	i2c_start(dev->bus);
	i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	i2c_sendbyte(dev->bus, subaddr, I2C_DELAY);
	i2c_start(dev->bus);
	i2c_sendbyte(dev->bus, dev->addr+1, I2C_DELAY);
	data = i2c_readbyte(dev->bus, 1);
	dev->reg[subaddr] = data;
	i2c_stop(dev->bus);

	UNLOCK_I2C_BUS(dev->bus);

	return data;
}

static
int vpx3220_fp_status(struct vpx3220 *dev)
{
	unsigned char status;
	unsigned int  i;

	for (i=0; i<VPX_TIMEOUT_COUNT; i++) {
		status = vpx3220_read (dev, 0x29);

		if (!(status & 4))
			return 0;

		udelay(10);

		if (current->need_resched)
			schedule();
	}

	return -EBUSY;
}

static
int vpx3220_fp_write(struct vpx3220 *dev, unsigned char fpaddr, unsigned short data)
{
	int ack;

	if (vpx3220_fp_status (dev) < 0)
		return -EBUSY;

	LOCK_I2C_BUS(dev->bus);

	i2c_start(dev->bus);
	i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	i2c_sendbyte(dev->bus, 0x27,      I2C_DELAY);
	i2c_sendbyte(dev->bus, 0x00,      I2C_DELAY);
	i2c_sendbyte(dev->bus, fpaddr,    I2C_DELAY);
	i2c_stop(dev->bus);

	i2c_start(dev->bus);
	i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	i2c_sendbyte(dev->bus, 0x28,      I2C_DELAY);
	i2c_sendbyte(dev->bus, (data >> 8) & 0x0F, I2C_DELAY);
	ack = i2c_sendbyte(dev->bus, data & 0xFF, I2C_DELAY);
	dev->reg[fpaddr] = data;
	i2c_stop(dev->bus);

	UNLOCK_I2C_BUS(dev->bus);

	return ack;
}

static 
unsigned short vpx3220_fp_read(struct vpx3220 *dev, unsigned char fpaddr)
{
	int ret = 0;
	unsigned char data, status;

	LOCK_I2C_BUS(dev->bus);

	i2c_start(dev->bus);
	ret |= i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	ret |= i2c_sendbyte(dev->bus, 0x27, I2C_DELAY);
	ret |= i2c_sendbyte(dev->bus, 0x00, I2C_DELAY);
	ret |= i2c_sendbyte(dev->bus, fpaddr, I2C_DELAY);
	i2c_stop(dev->bus);

	if (ret) {
		DEBUG(printk(KERN_DEBUG "vpx3220: vpx3220_fp_read failed\n"));
		return -EIO;
	}

	/* Read FP status */

	i2c_start(dev->bus);
	ret |= i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	ret |= i2c_sendbyte(dev->bus, 0x29, I2C_DELAY);

	i2c_start(dev->bus);
	ret |= i2c_sendbyte(dev->bus, dev->addr+1, I2C_DELAY);

	status = i2c_readbyte(dev->bus, 1) << 8;
	i2c_stop(dev->bus);

	if (status & 0x04) {
		DEBUG(printk(KERN_DEBUG "vpx3220: vpx3220_fp_read busy\n"));
		return -EIO;
	}

	/* Read FP register */

	i2c_start(dev->bus);
	ret |= i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
	ret |= i2c_sendbyte(dev->bus, 0x28, I2C_DELAY);

	i2c_start(dev->bus);
	ret |= i2c_sendbyte(dev->bus, dev->addr+1, I2C_DELAY);

	data  = i2c_readbyte(dev->bus, 0) << 8;
	data += i2c_readbyte(dev->bus, 1);
	dev->reg[fpaddr] = data;
	i2c_stop(dev->bus);

	UNLOCK_I2C_BUS(dev->bus);

	if (ret) {
		DEBUG(printk(KERN_DEBUG "vpx3220: vpx3220_fp_read failed\n"));
		return -EIO;
	}

	return data;
}

static
int vpx3220_write_block(struct vpx3220 *dev, unsigned const char *data, unsigned int len)
{
	int ack = 0;
	unsigned subaddr;

	LOCK_I2C_BUS(dev->bus);

	while (len > 1) {
		i2c_start(dev->bus);
		i2c_sendbyte(dev->bus, dev->addr, I2C_DELAY);
		ack |= i2c_sendbyte(dev->bus, (subaddr = *data++), I2C_DELAY);
		ack |= i2c_sendbyte(dev->bus, (dev->reg[subaddr] = *data++), I2C_DELAY);
		len -= 2;
		i2c_stop(dev->bus);
	}

	UNLOCK_I2C_BUS(dev->bus);

	return ack;
}

static
int vpx3220_write_fp_block(struct vpx3220 *dev, unsigned const short *data, unsigned int len)
{
	int ack = 0;
	unsigned char subaddr;

	while (len > 1) {
		subaddr = *data++;
		ack = vpx3220_fp_write (dev, subaddr, (dev->fpram[subaddr] = *data++));

		if (ack) {
			DEBUG(printk(KERN_INFO "vpx3220_write_fp_block failed at len=%d\n", len));
			break;
		}

		len -= 2;
	}

	return len;
}

/* ---------------------------------------------------------------------- */

static const unsigned short init_ntsc[] = {
	0x1c, 0x00,  /* NTSC tint angle                                         */  
	0x88, 17,    /* Window 1 vertical begin                                 */
	0x89, 240,   /* Vertical lines in                                       */
	0x8a, 240,   /* Vertical lines out                                      */
	0x8b, 000,   /* Horizontal begin                                        */
	0x8c, 640,   /* Horizontal length                                       */
	0x8d, 640,   /* Number of pixels                                        */
	0x8f, 000,   /* Disable window 2                                        */
	0xf0, 0x173, /* 13.5 MHz transport, Forced mode, latch windows          */
	0xf2, 0x13,  /* NTSC M, composite input                                 */
	0xe7, 0x20a, /* Enable vertical standard locking @ 240 lines            */
};

static const unsigned short init_pal[] = {
	0x88, 5,     /* Window 1 vertical begin                                 */
	0x89, 288+16,/* Vertical lines in (16 lines skipped by the VFE)         */
	0x8a, 288+16,/* Vertical lines out (16 lines skipped by the VFE)        */
	0x8b, 10,    /* Horizontal begin                                        */
	0x8c, 768,   /* Horizontal length                                       */
	0x8d, 768,   /* Number of pixels                                        */
	0x8e, 318,   /* Window 2 vertical begin                                 */
	0x89, 288+16,/* Vertical lines in (16 lines skipped by the VFE)         */
	0x8a, 288+16,/* Vertical lines out (16 lines skipped by the VFE)        */
	0x91, 10,    /* Horizontal begin                                        */
	0x92, 768,   /* Horizontal length                                       */
	0x93, 768,   /* Number of pixels                                        */
	0xf0, 0x173, /* 13.5 MHz transport, Forced mode, latch windows          */
	0xf2, 0x3d1, /* PAL B,G,H,I, composite input                            */
	0xe7, 0x26e, /* PAL                                                     */
};

static const unsigned char init_common[] = {
	0xf2, 0x00,  /* Disable all outputs                                     */
	0x33, 0x0d,  /* Luma : VIN2, Chroma : CIN (clamp off)                   */
	0xd8, 0x80,  /* HREF/VREF active high, VREF pulse = 2, Odd/Even flag    */
	0x20, 0x03,  /* IF compensation 0dB/oct                                 */
	0xe0, 0xff,  /* Open up all comparators                                 */
	0xe1, 0x00,
	0xe2, 0x7f,
	0xe3, 0x80,
	0xe4, 0x7f,
	0xe5, 0x80,
	0xe6, 0x00,  /* Brightness set to 0                                     */
	0xe7, 0x20,  /* Contrast to 1.0, noise shaping 9 to 8 1-bit rounding    */
	0xe8, 0xf8,  /* YUV422, CbCr binary offset, ... (p.32)                  */
	0xea, 0x18,  /* LLC2 connected, output FIFO reset with VACTintern       */
	0xf0, 0x4a,  /* Half full level to 10, bus shuffler [7:0, 23:16, 15:8]  */
	0xf1, 0x18,  /* Single clock, sync mode, no FE delay, no HLEN counter   */
	0xf8, 0x12,  /* Port A, PIXCLK, HF# & FE# strength to 2                 */
	0xf9, 0x24,  /* Port B, HREF, VREF, PREF & ALPHA strength to 4          */
};

static const unsigned short init_fp[] = {
	0xa0, 2070,  /* ACC reference                                           */
	0xa3, 0,     /*                                                         */
	0xa4, 0,     /*                                                         */
	0xa8, 30,    /*                                                         */
	0xb2, 768,   /*                                                         */
	0xbe, 27,    /*                                                         */
	0x58, 0,     /*                                                         */
	0x26, 0,     /*                                                         */
	0x4b, 0x29c, /* PLL gain                                                */
};

static int vpx3220_attach(struct i2c_device *device)
{
	int i;
	struct vpx3220 *decoder;

	device->data = decoder = kmalloc(sizeof(struct vpx3220), GFP_KERNEL);
	if (decoder == NULL) {
		return -ENOMEM;
	}

	memset(decoder, 0, sizeof(struct vpx3220));
	decoder->bus    = device->bus;
	decoder->addr   = device->addr;
	decoder->norm   = VIDEO_MODE_PAL;
	decoder->enable = 0;

	/* Check for manufacture ID and part number */
	i = vpx3220_read(decoder, 0x00);
	if (i != 0xec) {
		printk(KERN_INFO "vpx3220_attach: Wrong manufacture ID (0x%02x)\n", i);
		kfree (decoder);
		return -EINVAL;
	}

	i = decoder->part = (vpx3220_read(decoder, 0x02) << 8) + vpx3220_read(decoder, 0x01);
	switch (i) {
	case 0x4680:
		strcpy(device->name, "vpx3220a");
		break;
	case 0x4260:
		strcpy(device->name, "vpx32xx");
		break;
	case 0x4280:
		strcpy(device->name, "vpx3214c");
		break;
	default:
		printk(KERN_INFO "vpx3220_attach: Wrong part number (0x%04x)\n", i);
		kfree (decoder);
		return -EINVAL;
	}

	printk (KERN_INFO "vpx3220_attach: %s found at 0x%02x\n", device->name, device->addr);

	MOD_INC_USE_COUNT;

	vpx3220_write_block (decoder, init_common, sizeof(init_common));
	vpx3220_write_fp_block (decoder, init_fp, sizeof(init_fp)>>1);
	/* Default to PAL */
	vpx3220_write_fp_block (decoder, init_pal, sizeof(init_pal)>>1);

	return 0;
}

#ifdef DEBUG
static void vpx3220_dump_i2c (struct vpx3220 *decoder)
{
	int len = sizeof (init_common);
	const unsigned char *data = init_common;
	while (len > 1)
	{
		printk (KERN_DEBUG "vpx3216b i2c reg 0x%02x data 0x%02x\n", *data, vpx3220_read (decoder, *data));
		data += 2;
		len -= 2;
	}
}
#endif

static int vpx3220_detach(struct i2c_device *device)
{
	kfree(device->data);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int vpx3220_command(struct i2c_device *device, unsigned int cmd, void * arg)
{
	struct vpx3220 * decoder = device->data;

	switch (cmd) {

	case 0:
		{
			vpx3220_write_block (decoder, init_common, sizeof(init_common));
			vpx3220_write_fp_block (decoder, init_fp, sizeof(init_fp)>>1);
			/* Default to PAL */
			vpx3220_write_fp_block (decoder, init_pal, sizeof(init_pal)>>1);
		}
		break;

	case DECODER_GET_CAPABILITIES:
		{
			struct video_decoder_capability *cap = arg;

			DEBUG(printk (KERN_DEBUG "%s: DECODER_GET_CAPABILITIES\n", device->name));

			cap->flags = VIDEO_DECODER_PAL
				| VIDEO_DECODER_NTSC
				| VIDEO_DECODER_SECAM
				| VIDEO_DECODER_AUTO
				| VIDEO_DECODER_CCIR;
			cap->inputs = 3;
			cap->outputs = 1;
		}
		break;

	case DECODER_GET_STATUS:
		{
			int res = 0, status;

			DEBUG(printk(KERN_INFO "%s: DECODER_GET_STATUS\n", device->name));

			status = vpx3220_fp_read(decoder, 0x0f3);

			DEBUG(printk(KERN_INFO "%s: status: 0x%04x\n", device->name, status));

			if (status < 0)
				return status;

			if ((status & 0x20) == 0) {
				res |= DECODER_STATUS_GOOD | DECODER_STATUS_COLOR;

				switch (status & 0x18) {

				case 0x00:
				case 0x10:
				case 0x14:
				case 0x18:
					res |= DECODER_STATUS_PAL;
					break;

				case 0x08:
					res |= DECODER_STATUS_SECAM;
					break;

				case 0x04:
				case 0x0c:
				case 0x1c:
					res |= DECODER_STATUS_NTSC;
					break;
				}
			}

			*(int*)arg = res;
		}
		break;

	case DECODER_SET_NORM:
		{
			int * iarg = arg, data;

			DEBUG(printk (KERN_DEBUG "%s: DECODER_SET_NORM %d\n", device->name, *iarg));
			switch (*iarg) {

			case VIDEO_MODE_NTSC:
				vpx3220_write_fp_block(decoder, init_ntsc, sizeof(init_ntsc)>1);
				break;

			case VIDEO_MODE_PAL:
				vpx3220_write_fp_block(decoder, init_pal, sizeof(init_pal)>1);
				break;

			case VIDEO_MODE_SECAM:
				return -EINVAL;

			case VIDEO_MODE_AUTO:
				/* FIXME This is only preliminary support */
				data = vpx3220_fp_read(decoder, 0xf2) & 0x20;
				vpx3220_fp_write(decoder, 0xf2, 0x00c0 | data);
				break;

			default:
				return -EINVAL;

			}
			decoder->norm = *iarg;
		}
		break;

	case DECODER_SET_INPUT:
		{
			int * iarg = arg, data;

			/* RJ:	*iarg = 0: ST8 (PCTV) input
				*iarg = 1: COMPOSITE  input
				*iarg = 2: SVHS       input  */

			const int input[3][2] = {
				{ 0x0c, 0 },
				{ 0x0d, 0 },
				{ 0x0e, 1 }
			};

			DEBUG(printk (KERN_DEBUG "%s: DECODER_SET_INPUT %d\n", device->name, *iarg));

			if (*iarg < 0 || *iarg > 2)
				return -EINVAL;

			vpx3220_write(decoder, 0x33, input[*iarg][0]);

			data = vpx3220_fp_read(decoder, 0xf2) & ~(0x0020); 
			if (data < 0)
				return data;
			vpx3220_fp_write(decoder, 0xf2, data | (input[*iarg][1] << 5));

			udelay(10);
		}
		break;

	case DECODER_SET_OUTPUT:
		{
			int * iarg = arg;

			/* not much choice of outputs */
			if (*iarg != 0) {
				return -EINVAL;
			}
		}
		break;

	case DECODER_ENABLE_OUTPUT:
		{
			int * iarg = arg;

			DEBUG(printk (KERN_DEBUG "%s: DECODER_ENABLE_OUTPUT %d\n", device->name, *iarg));

			decoder->enable = !!*iarg;
			vpx3220_write(decoder, 0xf2, (decoder->enable? 0x1b: 0x00));
		}
		break;

#ifdef DEBUG
	case DECODER_DUMP:
		{
			vpx3220_dump_i2c(decoder);
		}
		break;
#endif

	default:
		return -EINVAL;
	}

	return 0;
}

/* ----------------------------------------------------------------------- */

struct i2c_driver i2c_driver_vpx3220 = {
	"vpx3220",                    /* name              */
	I2C_DRIVERID_VIDEODECODER,    /* ID                */
	I2C_VPX3220, I2C_VPX3220+9,   /* I2C address range */
	vpx3220_attach,
	vpx3220_detach,
	vpx3220_command
};

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
int init_vpx3220(void)
#endif
{
	int res = 0;

	res = i2c_register_driver(&i2c_driver_vpx3220);
	printk (KERN_INFO "i2c_register_driver returned %d\n", res);

	return res;
}

#ifdef MODULE

void cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_vpx3220);
}

#endif
