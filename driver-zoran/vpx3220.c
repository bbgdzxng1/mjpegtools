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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <linux/byteorder/swab.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/videodev.h>
#include <linux/video_decoder.h>

#define I2C_VPX3220        0x86
#define VPX3220_DEBUG	KERN_DEBUG "vpx3220: "
#define DEBUG(x)	x

#define VPX_TIMEOUT_COUNT  10

/* ----------------------------------------------------------------------- */

static int vpx3220_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static u8 vpx3220_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int vpx3220_fp_status(struct i2c_client *client)
{
	unsigned char status;
	unsigned int  i;

	for (i = 0; i < VPX_TIMEOUT_COUNT; i++) {
		status = vpx3220_read(client, 0x29);

		if (!(status & 4))
			return 0;

		udelay(10);

		if (current->need_resched)
			schedule();
	}

	return -1;
}

static int vpx3220_fp_write(struct i2c_client *client, u8 fpaddr, u16 data)
{
	/* Write the 16-bit address to the FPWR register */
	if (i2c_smbus_write_word_data(client, 0x27, swab16(fpaddr)) == -1) {
		DEBUG(printk(VPX3220_DEBUG __func__": failed\n"));
		return -1;
	}

	if (vpx3220_fp_status(client) < 0)
		return -1;

	/* Write the 16-bit data to the FPDAT register */
	if (i2c_smbus_write_word_data(client, 0x28, swab16(data)) == -1) {
		DEBUG(printk(VPX3220_DEBUG __func__": failed\n"));
		return -1;
	}

	return 0;
}

static u16 vpx3220_fp_read(struct i2c_client *client, u16 fpaddr)
{
	s16 data;

	/* Write the 16-bit address to the FPRD register */
	if (i2c_smbus_write_word_data(client, 0x26, swab16(fpaddr)) == -1) {
		DEBUG(printk(VPX3220_DEBUG __func__": failed\n"));
		return -1;
	}

	if (vpx3220_fp_status(client) < 0)
		return -1;

	/* Read the 16-bit data from the FPDAT register */
	data = i2c_smbus_read_word_data(client, 0x28);
	if (data == -1) {
		DEBUG(printk(VPX3220_DEBUG __func__": failed\n"));
		return -1;
	}

	return swab16(data);
}

static int vpx3220_write_block(struct i2c_client *client, const u8 *data, unsigned int len)
{
	u8 reg;
	int ret = 0;

	while (len > 1) {
		reg = *data++;
		ret |= i2c_smbus_write_byte_data(client, reg, *data++);
		len -= 2;
	}

	return ret;
}

static int vpx3220_write_fp_block(struct i2c_client *client, const u16 *data, unsigned int len)
{
	u8 reg;
	int ret = 0;

	while (len > 1) {
		reg = *data++;
		ret |= vpx3220_fp_write (client, reg, *data++);
		len -= 2;
	}

	return ret;
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

#ifdef DEBUG
static void vpx3220_dump_i2c (struct i2c_client *client)
{
	int len = sizeof (init_common);
	const unsigned char *data = init_common;
	while (len > 1)
	{
		printk (KERN_DEBUG "vpx3216b i2c reg 0x%02x data 0x%02x\n", *data, vpx3220_read (client, *data));
		data += 2;
		len -= 2;
	}
}
#endif

static int vpx3220_command(struct i2c_client *client, unsigned int cmd, void * arg)
{
	switch (cmd) {
	case 0:
		{
			vpx3220_write_block(client, init_common, sizeof(init_common));
			vpx3220_write_fp_block(client, init_fp, sizeof(init_fp)>>1);
			/* Default to PAL */
			vpx3220_write_fp_block(client, init_pal, sizeof(init_pal)>>1);
		}
		break;

	case DECODER_GET_CAPABILITIES:
		{
			struct video_decoder_capability *cap = arg;

			DEBUG(printk (KERN_DEBUG "%s: DECODER_GET_CAPABILITIES\n", client->name));

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

			DEBUG(printk(KERN_INFO "%s: DECODER_GET_STATUS\n", client->name));

			status = vpx3220_fp_read(client, 0x0f3);

			DEBUG(printk(KERN_INFO "%s: status: 0x%04x\n", client->name, status));

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

			DEBUG(printk (KERN_DEBUG "%s: DECODER_SET_NORM %d\n", client->name, *iarg));
			switch (*iarg) {

			case VIDEO_MODE_NTSC:
				vpx3220_write_fp_block(client, init_ntsc, sizeof(init_ntsc)>1);
				break;

			case VIDEO_MODE_PAL:
				vpx3220_write_fp_block(client, init_pal, sizeof(init_pal)>1);
				break;

			case VIDEO_MODE_SECAM:
				return -EINVAL;

			case VIDEO_MODE_AUTO:
				/* FIXME This is only preliminary support */
				data = vpx3220_fp_read(client, 0xf2) & 0x20;
				vpx3220_fp_write(client, 0xf2, 0x00c0 | data);
				break;

			default:
				return -EINVAL;

			}
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

			DEBUG(printk (KERN_DEBUG "%s: DECODER_SET_INPUT %d\n", client->name, *iarg));

			if (*iarg < 0 || *iarg > 2)
				return -EINVAL;

			vpx3220_write(client, 0x33, input[*iarg][0]);

			data = vpx3220_fp_read(client, 0xf2) & ~(0x0020); 
			if (data < 0)
				return data;
			vpx3220_fp_write(client, 0xf2, data | (input[*iarg][1] << 5));

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

			DEBUG(printk (KERN_DEBUG "%s: DECODER_ENABLE_OUTPUT %d\n", client->name, *iarg));

			vpx3220_write(client, 0xf2, (*iarg ? 0x1b : 0x00));
		}
		break;

#ifdef DEBUG
	case DECODER_DUMP:
		{
			vpx3220_dump_i2c(client);
		}
		break;
#endif

	default:
		return -EINVAL;
	}

	return 0;
}

static int vpx3220_init_client (struct i2c_client *client)
{
	vpx3220_write_block(client, init_common, sizeof(init_common));
	vpx3220_write_fp_block(client, init_fp, sizeof(init_fp)>>1);
	/* Default to PAL */
	vpx3220_write_fp_block(client, init_pal, sizeof(init_pal)>>1);

	return 0;
}

/* -----------------------------------------------------------------------
 * Client managment code
 */

static void vpx3220_inc_use (struct i2c_client *client)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

static void vpx3220_dec_use (struct i2c_client *client)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */
static unsigned short normal_i2c[] = { I2C_VPX3220>>1, (I2C_VPX3220>>1)+4,
					I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int vpx3220_i2c_id = 0;
static struct i2c_driver vpx3220_i2c_driver;

static int vpx3220_detach_client (struct i2c_client *client)
{
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(client);
	return 0;
}

static int vpx3220_detect_client (struct i2c_adapter *adapter, int address,
				  unsigned short flags, int kind)
{
	int err;
	struct i2c_client *client;

	DEBUG(printk(VPX3220_DEBUG __func__"\n"));

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
					      I2C_FUNC_SMBUS_WORD_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == NULL) {
		return -ENOMEM;
	}

	client->addr = address;
	client->data = NULL;
	client->adapter = adapter;
	client->driver = &vpx3220_i2c_driver;
	client->flags = 0;

	/* Check for manufacture ID and part number */
	if ( kind < 0 ) {
		u8 id;
		u16 pn;

		id = vpx3220_read(client, 0x00);
		if (id != 0xec) {
			printk(KERN_INFO "vpx3220_attach: Wrong manufacturer ID (0x%02x)\n", id);
			kfree(client);
			return 0;
		}

		pn = (vpx3220_read(client, 0x02) << 8) + vpx3220_read(client, 0x01);
		switch (pn) {
		case 0x4680:
			strcpy(client->name, "vpx3220a");
			break;
		case 0x4260:
			strcpy(client->name, "vpx32xx");
			break;
		case 0x4280:
			strcpy(client->name, "vpx3214c");
			break;
		default:
			printk(KERN_INFO __func__ ": Wrong part number (0x%04x)\n", pn);
			kfree(client);
			return 0;
		}
	}
	else {
		strcpy(client->name, "forced vpx32xx");
	}

	client->id = vpx3220_i2c_id++;

	err = i2c_attach_client(client);
	if (err) {
		kfree(client);
		return err;
	}

	printk (KERN_INFO __func__ ": %s found at 0x%02x\n", client->name, client->addr<<1);

	vpx3220_init_client(client);

	return 0;
}

static int vpx3220_attach_adapter (struct i2c_adapter *adapter)
{
	int ret;

	ret = i2c_probe(adapter, &addr_data, &vpx3220_detect_client);
	DEBUG(printk(VPX3220_DEBUG __func__ ": i2c_probe returned %d\n", ret));
	return ret;
}

/* -----------------------------------------------------------------------
 * Driver initialization and cleanup code
 */

static struct i2c_driver vpx3220_i2c_driver = {
	name:		"vpx3220",
	id:		I2C_DRIVERID_VPX32XX,
	flags:		I2C_DF_NOTIFY,
	attach_adapter:	&vpx3220_attach_adapter,
	detach_client:	&vpx3220_detach_client,
	command:	&vpx3220_command,
	inc_use:	vpx3220_inc_use,
	dec_use:	vpx3220_dec_use,
};

EXPORT_NO_SYMBOLS;

int __init vpx3220_init (void)
{
	int res = 0;

	res = i2c_add_driver(&vpx3220_i2c_driver);
	printk (KERN_INFO "i2c_register_driver returned %d\n", res);

	return res;
}

void __exit vpx3220_cleanup (void)
{
	i2c_del_driver(&vpx3220_i2c_driver);
}

#ifdef MODULE

int init_module(void)
{
	return vpx3220_init();
}

void cleanup_module(void)
{
	vpx3220_cleanup();
}

MODULE_DESCRIPTION("vpx3220a/vpx3216b/vpx3214c video encoder driver");
MODULE_AUTHOR("Laurent Pinchart");
MODULE_LICENSE("GPL");

#endif
