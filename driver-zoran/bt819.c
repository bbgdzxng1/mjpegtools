#define DEBUGLEVEL 0
/* 
    bt819 - BT819A VideoStream Decoder (Rockwell Part)

    Copyright (C) 1999 Mike Bernson <mike@mlb.org>
    Copyright (C) 1998 Dave Perks <dperks@ibm.net>

    Modifications for LML33/DC10plus unified driver
    Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
    
    Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
       - moved over to linux>=2.4.x i2c protocol (9/9/2002)

    This code was modify/ported from the saa7111 driver written
    by Dave Perks.

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

#include <linux/videodev.h>
#include <linux/version.h>
#include <asm/uaccess.h>

MODULE_DESCRIPTION("Brooktree-819 video decoder driver");
MODULE_AUTHOR("Mike Bernson & Dave Perks");
MODULE_LICENSE("GPL");

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include <linux/video_decoder.h>

#if (DEBUGLEVEL > 0)
#define DEBUG(x)       x /* Debug driver */   
#else
#define DEBUG(x)
#endif

/* ----------------------------------------------------------------------- */

struct bt819
{
   unsigned char   reg[32];

   int      initialized;
   int      norm;
   int      input;
   int      enable;
   int      bright;
   int      contrast;
   int      hue;
   int      sat;
};

struct	timing {
	int	hactive;
	int	hdelay;
	int	vactive;
	int	vdelay;
	int	hscale;
	int	vscale;
};

struct timing timing_data[] = {
	{ 864-24, 2, 623, 1, 0x0504, 0x0000 },
 	{ 858-24, 2, 523, 1, 0x00f8, 0x0000 },
//	{ 858-68, 64, 523, 1, 0x00f8, 0x0000 },
};

#define   I2C_BT819        0x8a

/* ----------------------------------------------------------------------- */

static int
bt819_write(struct i2c_client *client, u8 reg, u8 value)
{
   struct bt819 *decoder = client->data;
   decoder->reg[reg] = value;
   return i2c_smbus_write_byte_data(client, reg, value);
}

static int
bt819_setbit(struct i2c_client *client, u8 reg, u8 bit, u8 value)
{
   struct bt819 *decoder = client->data;
   return bt819_write(client, reg, 
		(decoder->reg[reg] & ~(1 << bit)) | (value ? (1 << bit) : 0));
}

static int
bt819_write_block(struct i2c_client *client, const u8 *data, unsigned int len)
{
   int ret = -1;
   u8 reg;

   /* (Ronald) just as saa7110, this is ugly, and for the same reason,
    * we don't use the chip's internal auto-increment function... */
   while (len > 1) {
      reg = *data++;
      ret = bt819_write(client, reg, *data++);
      len-=2;
      if (ret)
         return ret;
   }
   return ret;
}

static u8
bt819_read(struct i2c_client *client, u8 reg)
{
   return i2c_smbus_read_byte_data(client, reg);
}

static int bt819_init(struct i2c_client *client)
{
   struct bt819 *decoder = client->data;

   static unsigned char init[] = {
	//0x1f, 0x00,     /* Reset */
        0x01, 0x59,	/* 0x01 input format */
	0x02, 0x00,	/* 0x02 temporal decimation */
	0x03, 0x12,	/* 0x03 Cropping msb */
	0x04, 0x16,	/* 0x04 Vertical Delay, lsb */
	0x05, 0xe0,	/* 0x05 Vertical Active lsb */
	0x06, 0x80,	/* 0x06 Horizontal Delay lsb */ 
	0x07, 0xd0,	/* 0x07 Horizontal Active lsb */
 	0x08, 0x00,	/* 0x08 Horizontal Scaling msb */
 	0x09, 0xf8,	/* 0x09 Horizontal Scaling lsb */
	0x0a, 0x00,	/* 0x0a Brightness control */
	0x0b, 0x30,	/* 0x0b Miscellaneous control */
	0x0c, 0xd8,	/* 0x0c Luma Gain lsb */
	0x0d, 0xfe,	/* 0x0d Chroma Gain (U) lsb */
	0x0e, 0xb4,	/* 0x0e Chroma Gain (V) msb */
	0x0f, 0x00,	/* 0x0f Hue control */
 	0x12, 0x04,	/* 0x12 Output Format */
	0x13, 0x20,	/* 0x13 Vertial Scaling msb 0x60, */
	0x14, 0x00,	/* 0x14 Vertial Scaling lsb */
 	0x16, 0x04,	/* 0x16 Video Timing Polarity 0x02, */
	0x18, 0x68,	/* 0x18 AGC Delay */
	0x19, 0x5d,	/* 0x19 Burst Gate Delay */
	0x1a, 0x80,	/* 0x1a ADC Interface */
   };

   struct timing *timing = &timing_data[decoder->norm];

   init[3*2-1] = (((timing->vdelay >> 8) & 0x03) << 6) |
	     (((timing->vactive >> 8) & 0x03) << 4) |
	     (((timing->hdelay >> 8) & 0x03) << 2) |
	     ((timing->hactive >> 8) & 0x03);
   init[4*2-1] = timing->vdelay & 0xff;
   init[5*2-1] = timing->vactive & 0xff;
   init[6*2-1] = timing->hdelay & 0xff;
   init[7*2-1] = timing->hactive & 0xff;
   init[8*2-1] = timing->hscale >> 8;
   init[9*2-1] = timing->hscale & 0xff;

   bt819_write(client, 0x1f, 0x00);
   mdelay(1);
   return bt819_write_block(client, init, sizeof(init));

}

/* ----------------------------------------------------------------------- */

static int bt819_command(struct i2c_client *client, unsigned int cmd, void * arg)
{
   int temp;

   struct bt819 *decoder = client->data;
   
   if(!decoder->initialized) {      // First call to bt819_init could be
        bt819_init(client);         // without #FRST = 0
        decoder->initialized = 1;
   }
   
   switch (cmd) {

   case 0:  // This is just for testing!!!
        bt819_init(client);
        break;
        
   case DECODER_GET_CAPABILITIES:
      {
         struct video_decoder_capability *cap = arg;

         cap->flags
            = VIDEO_DECODER_PAL
            | VIDEO_DECODER_NTSC
            | VIDEO_DECODER_CCIR;
         cap->inputs = 8;
         cap->outputs = 1;
      }
      break;

   case DECODER_GET_STATUS:
      {
         int * iarg = arg;
         int status;
         int res;

         status = bt819_read(client, 0x00);
         res = 0;
         if ((status & 0x80)) {
            res |= DECODER_STATUS_GOOD;
         }
         switch (decoder->norm) {
         case VIDEO_MODE_NTSC:
            res |= DECODER_STATUS_NTSC;
            break;
         case VIDEO_MODE_PAL:
            res |= DECODER_STATUS_PAL;
            break;
         default:
         case VIDEO_MODE_AUTO:
            if ((status & 0x10)) {
               res |= DECODER_STATUS_PAL;
            } else {
               res |= DECODER_STATUS_NTSC;
            }
            break;
         }
         res |= DECODER_STATUS_COLOR;
         *iarg = res;

	 DEBUG(printk(KERN_INFO "%s: get status %x\n", 
		client->name, *iarg));
      }
      break;

   case DECODER_SET_NORM:
      {
         int * iarg = arg;
	 struct timing *timing;

	 DEBUG(printk(KERN_INFO "%s: set norm %x\n", 
		client->name, *iarg));

         if (*iarg == VIDEO_MODE_NTSC) {
            bt819_setbit(client, 0x01,  0, 1);
            bt819_setbit(client, 0x01,  1, 0);
            bt819_write(client, 0x18, 0x68);
            bt819_write(client, 0x19, 0x5d);
            //bt819_setbit(client, 0x1a,  5, 1);
 	    timing = &timing_data[VIDEO_MODE_NTSC];
         } else {
            bt819_setbit(client, 0x01,  0, 1);
            bt819_setbit(client, 0x01,  1, 1);
            bt819_write(client, 0x18, 0x7f);
            bt819_write(client, 0x19, 0x72);
            //bt819_setbit(client, 0x1a,  5, 0);
	    timing = &timing_data[VIDEO_MODE_PAL];
	 }

	 bt819_write(client, 0x03, 
		     (((timing->vdelay >> 8) & 0x03) << 6) |
		     (((timing->vactive >> 8) & 0x03) << 4) |
		     (((timing->hdelay >> 8) & 0x03) << 2) |
		     ((timing->hactive >> 8) & 0x03));
  	 bt819_write(client, 0x04, timing->vdelay & 0xff);
  	 bt819_write(client, 0x05, timing->vactive & 0xff);
	 bt819_write(client, 0x06, timing->hdelay & 0xff);
	 bt819_write(client, 0x07, timing->hactive & 0xff);
	 bt819_write(client, 0x08, timing->hscale >> 8 );
	 bt819_write(client, 0x09, timing->hscale & 0xff);
         decoder->norm = *iarg;
      }
      break;

   case DECODER_SET_INPUT:
      {
         int * iarg = arg;

	 DEBUG(printk(KERN_INFO "%s: set input %x\n", 
		client->name, *iarg));

         if (*iarg < 0 || *iarg > 7) {
            return -EINVAL;
         }

         if (decoder->input != *iarg) {
            decoder->input = *iarg;
            /* select mode */
	    if (decoder->input == 0) {
		bt819_setbit(client, 0x0b, 6, 0);
		bt819_setbit(client, 0x1a, 1, 1);
	    } else {
		bt819_setbit(client, 0x0b, 6, 1);
		bt819_setbit(client, 0x1a, 1, 0);
	    }
         }
      }
      break;

   case DECODER_SET_OUTPUT:
      {
         int * iarg = arg;

	 DEBUG(printk(KERN_INFO "%s: set output %x\n", 
		client->name, *iarg));

         /* not much choice of outputs */
         if (*iarg != 0) {
            return -EINVAL;
         }
      }
      break;

   case DECODER_ENABLE_OUTPUT:
      {
         int * iarg = arg;
         int enable = (*iarg != 0);

	 DEBUG(printk(KERN_INFO "%s: enable output %x\n", 
		client->name, *iarg));

         if (decoder->enable != enable) {
            decoder->enable = enable;

            if(decoder->enable)
            {
	       bt819_setbit(client, 0x16, 7, 0);
            }
            else
            {
	       bt819_setbit(client, 0x16, 7, 1);
            }
         }
      }
      break;

   case DECODER_SET_PICTURE:
      {
         struct video_picture *pic = arg;

	 DEBUG(printk(KERN_INFO "%s: set picture brightness %d contrast %d colour %d\n", 
		client->name, pic->brightness, pic->contrast,
		pic->colour));


         if (decoder->bright != pic->brightness) {
            /* We want -128 to 127 we get 0-65535 */
            decoder->bright = pic->brightness;
            bt819_write(client, 0x0a, (decoder->bright >> 8) - 128);
         }

         if (decoder->contrast != pic->contrast) {
            /* We want 0 to 511 we get 0-65535 */
            decoder->contrast = pic->contrast;
            bt819_write(client, 0x0c, (decoder->contrast >> 7) & 0xff);
	    bt819_setbit(client, 0x0b, 2, ((decoder->contrast >> 15) & 0x01));
         }

         if (decoder->sat != pic->colour) {
            /* We want 0 to 511 we get 0-65535 */
            decoder->sat = pic->colour;
            bt819_write(client, 0x0d, (decoder->sat >> 7) & 0xff);
	    bt819_setbit(client, 0x0b, 1, ((decoder->sat >> 15) & 0x01));

	    temp = (decoder->sat * 201) / 237;
            bt819_write(client, 0x0e, (temp >> 7) & 0xff);
	    bt819_setbit(client, 0x0b, 0, (temp >> 15) &0x01);
         }

         if (decoder->hue != pic->hue) {
            /* We want -128 to 127 we get 0-65535 */
            decoder->hue = pic->hue;
            bt819_write(client, 0x0f, 128 - (decoder->hue >> 8));
         }
      }
      break;

   default:
      return -EINVAL;
   }

   return 0;
}

/* ----------------------------------------------------------------------- */

static void
bt819_inc_use (struct i2c_client *client)
{
#ifdef MODULE
   MOD_INC_USE_COUNT;
#endif
}

static void
bt819_dec_use(struct i2c_client *client)
{
#ifdef MODULE
   MOD_DEC_USE_COUNT;
#endif
}

/*
 * Generic i2c probe
 * concerning the addresses: i2c wants 7 bit (without the r/w bit), so '>>1'
 */
static unsigned short normal_i2c[] = { I2C_BT819>>1, I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

static int bt819_i2c_id = 0;
struct i2c_driver i2c_driver_bt819;

static int
bt819_detect_client (struct i2c_adapter *adapter,
                     int                 address,
                     unsigned short      flags,
                     int                 kind)
{
   int i;
   struct bt819 *decoder;
   struct i2c_client *client;

   DEBUG(printk(KERN_INFO "saa7111.c: detecting bt819 client on address 0x%x\n", address<<1));

   /* Check if the adapter supports the needed features */
   if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
      return 0;

   client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
   if (client == 0)
      return -ENOMEM;
   memset(client, 0, sizeof(struct i2c_client));
   client->addr = address;
   client->adapter = adapter;
   client->driver = &i2c_driver_bt819;
   client->flags = 0;
   client->id = bt819_i2c_id++;
   snprintf(client->name, sizeof(client->name)-1, "bt819[%d]", client->id);

   client->data = decoder = kmalloc(sizeof(struct bt819), GFP_KERNEL);
   if (decoder == NULL) {
      return -ENOMEM;
   }

   MOD_INC_USE_COUNT;

   memset(decoder, 0, sizeof(struct bt819));
   decoder->norm = VIDEO_MODE_NTSC;
   decoder->input = 0;
   decoder->enable = 1;
   decoder->bright = 32768;
   decoder->contrast = 32768;
   decoder->hue = 32768;
   decoder->sat = 32768;
   decoder->initialized = 0;

   i = i2c_attach_client(client);
   if (i) {
      kfree(client);
      kfree(decoder);
      return i;
   }

   i = bt819_init(client);
   if (i < 0) {
      printk(KERN_ERR "%s_attach: init status %d\n", client->name, i);
   } else {
      printk(KERN_INFO "%s_attach: chip version %x at address 0x%x\n", 
		client->name, bt819_read(client, 0x17) & 0x0f, client->addr<<1);
   }

   bt819_inc_use(client);

   return 0;
}

static int
bt819_attach_adapter(struct i2c_adapter *adapter)
{
   DEBUG(printk(KERN_INFO "bt819.c: starting probe for adapter %s (0x%x)\n",
		adapter->name, adapter->id));
   return i2c_probe(adapter, &addr_data, &bt819_detect_client);
}

static int
bt819_detach_client(struct i2c_client *client)
{
   struct bt819* decoder = client->data;
   int err;

   err = i2c_detach_client(client);
   if (err) {
      return err;
   }

   bt819_dec_use(client);
   kfree(decoder);
   kfree(client);
   return 0;
}

/* ----------------------------------------------------------------------- */

struct i2c_driver i2c_driver_bt819 = {
   name:		"bt819",

   id:			I2C_DRIVERID_BT819,
   flags:		I2C_DF_NOTIFY,

   attach_adapter:	bt819_attach_adapter,
   detach_client:	bt819_detach_client,
   command:		bt819_command,
};

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
int bt819_init(void)
#endif
{
   DEBUG(printk("inserting bt819\n"));
   return i2c_add_driver(&i2c_driver_bt819);
}



#ifdef MODULE

void cleanup_module(void)
{
   i2c_del_driver(&i2c_driver_bt819);
}

#endif
