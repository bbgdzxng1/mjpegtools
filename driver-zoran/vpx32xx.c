/*
    vpx32xx.c  MICRONAS INTERMETALL VPX32XX video pixel decoder driver

    (c) 1999-2001 Peter Schlaf <peter.schlaf@org.chemie.uni-giessen.de>
        project started by Jens Seidel <jens.seidel@hrz.tu-chemnitz.de>
    (c) 2001 Wolfgang Scherr <scherr@net4you.at> - Update for the DC30
                   
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
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/uaccess.h>

MODULE_DESCRIPTION("vpx32xx video pixel decoder driver");
MODULE_AUTHOR("Peter Schlaf");
MODULE_LICENSE("GPL");

#if LINUX_VERSION_CODE < 0x020321
#include <linux/i2c.h>
#else
#include <linux/i2c-old.h>
#endif
#define	WAIT_FOR_ACK	5

#include "linux/videodev.h"
#include "linux/video_decoder.h"

#define DEBUG(x) x	/* for debug write "DEBUG(x) x" */

#include "vpx32xx.h"


/* write one byte to i2c chip register */
static int 
vpx32xx_writeI2C(struct vpx32xx *decoder, unsigned char subaddr, unsigned char data)
{
	int ret = 0;

	LOCK_I2C_BUS(decoder->bus);

	i2c_start(decoder->bus);
	ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK);
	ret |= i2c_sendbyte(decoder->bus, subaddr, WAIT_FOR_ACK) << 1;
	ret |= i2c_sendbyte(decoder->bus, data, WAIT_FOR_ACK) << 2;
	i2c_stop(decoder->bus);

	UNLOCK_I2C_BUS(decoder->bus);

	printk(KERN_INFO "vpx32xx_writeI2C: %02x -> #%02x\n",data, subaddr);
	if (ret) {
		printk(KERN_WARNING "vpx32xx: I/O error, vpx32xx_writeI2C failed! (code:0x%x)\n", ret);
		ret = -EIO;
	}
	return ret;
}

/* read and return one byte from i2c chip register */
static unsigned char 
vpx32xx_readI2C(struct vpx32xx *decoder, unsigned char subaddr)
{
	int ret = 0;
	unsigned char data;
	
	LOCK_I2C_BUS(decoder->bus);

	i2c_start(decoder->bus);
	ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK);
	ret |= i2c_sendbyte(decoder->bus, subaddr, WAIT_FOR_ACK) << 1;
	i2c_start(decoder->bus);
	ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX + 1, WAIT_FOR_ACK) << 2;
	data = i2c_readbyte(decoder->bus, 1);
	i2c_stop(decoder->bus);

	UNLOCK_I2C_BUS(decoder->bus);

	printk(KERN_INFO "vpx32xx_readI2C: #%02x -> %02x\n",subaddr,data);
	if (ret) 
		printk(KERN_WARNING "vpx32xx: I/O error, vpx32xx_readI2C failed! (code:0x%x)\n", ret);

	return data;
}

/*******************************/

/* write to vpx fp-ram register */
static int 
vpx32xx_writeFP(struct vpx32xx *decoder, int subaddr, int data)
{
	int i, ret = 0;
	unsigned char c;

	LOCK_I2C_BUS(decoder->bus);

	i2c_start(decoder->bus);
	ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK);
	ret |= i2c_sendbyte(decoder->bus, decoder->fpwr, WAIT_FOR_ACK) << 1;
	ret |= i2c_sendbyte(decoder->bus, (subaddr >> 8), WAIT_FOR_ACK) << 2;
	ret |= i2c_sendbyte(decoder->bus, (subaddr & 0x00FF), WAIT_FOR_ACK) << 3;
	i2c_stop(decoder->bus);

	printk(KERN_INFO "vpx32xx_writeFP: %04x -> #%02x\n",data, subaddr);
	if (ret) {
		UNLOCK_I2C_BUS(decoder->bus);
		printk(KERN_WARNING "vpx32xx: [1] I/O error, vpx32xx_writeFP failed! (code:0x%x)\n", ret);
		return -EIO;
	}
	
	for (i = 0; i < 100; i++) {		   /* FP busy ? */
		i2c_start(decoder->bus);
		ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK) << 4;
		ret |= i2c_sendbyte(decoder->bus, decoder->fpsta, WAIT_FOR_ACK) << 5;
	
		i2c_start(decoder->bus);
		ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX + 1, WAIT_FOR_ACK) << 6;
		c = (i2c_readbyte(decoder->bus, 1) & 0x04);	/* read status and mask the BUSY-bit */
		i2c_stop(decoder->bus);

		if (ret) {
			UNLOCK_I2C_BUS(decoder->bus);
			printk(KERN_WARNING "vpx32xx: [2] I/O error, vpx32xx_writeFP failed! (code:0x%x)\n", ret);
			return -EIO;
		}
							
		if (c == 0) {
			i2c_start(decoder->bus);
			ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK) << 7;
			ret |= i2c_sendbyte(decoder->bus, decoder->fpdat, WAIT_FOR_ACK) << 8;
			ret |= i2c_sendbyte(decoder->bus, (data >> 8), WAIT_FOR_ACK) << 9;
			ret |= i2c_sendbyte(decoder->bus, (data & 0x00FF), WAIT_FOR_ACK) << 10;
			i2c_stop(decoder->bus);
		
			UNLOCK_I2C_BUS(decoder->bus);

			if (ret) {
				printk(KERN_WARNING "vpx32xx: [3] I/O error, vpx32xx_writeFP failed! (code:0x%x)\n", ret);
				ret = -EIO;
			}
			return ret;
		}
	}

	UNLOCK_I2C_BUS(decoder->bus);

	printk(KERN_INFO "\nvpx32xx: vpx32xx_writeFP: FP processor is busy!\n");

	return -EBUSY;
}

/* read from vpx fp-ram register */
static int 
vpx32xx_readFP(struct vpx32xx *decoder, int subaddr)
{
	int i, ret = 0;
	unsigned char FPdataL, FPdataH, c;

	LOCK_I2C_BUS(decoder->bus);

	i2c_start(decoder->bus);
	ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK);
	ret |= i2c_sendbyte(decoder->bus, decoder->fprd, WAIT_FOR_ACK) << 1;
	ret |= i2c_sendbyte(decoder->bus, (subaddr >> 8), WAIT_FOR_ACK) << 2;
	ret |= i2c_sendbyte(decoder->bus, (subaddr & 0x00FF), WAIT_FOR_ACK) << 3;
	i2c_stop(decoder->bus);

	if (ret) {
		UNLOCK_I2C_BUS(decoder->bus);
		printk(KERN_WARNING "vpx32xx: [1] I/O error, vpx32xx_readFP failed! (code:0x%x)\n", ret);
		return -EIO;
	}
	
	for (i = 0; i < 100; i++) {		   /* FP busy ? */
		i2c_start(decoder->bus);
		ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK) << 4;
		ret |= i2c_sendbyte(decoder->bus, decoder->fpsta, WAIT_FOR_ACK) << 5;
	
		i2c_start(decoder->bus);
		ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX + 1, WAIT_FOR_ACK) << 6;
		c = (i2c_readbyte(decoder->bus, 1) & 0x04);	/* read status and mask the BUSY-bit */
		i2c_stop(decoder->bus);

		if (ret) {
			UNLOCK_I2C_BUS(decoder->bus);
			printk(KERN_WARNING "vpx32xx: [2] I/O error, vpx32xx_readFP failed! (code:0x%x)\n", ret);
			return -EIO;
		}
	
		if (c == 0) {
			i2c_start(decoder->bus);
			ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX, WAIT_FOR_ACK) << 7;
			ret |= i2c_sendbyte(decoder->bus, decoder->fpdat, WAIT_FOR_ACK) << 8;

			i2c_start(decoder->bus);
			ret |= i2c_sendbyte(decoder->bus, I2C_VPX32XX + 1, WAIT_FOR_ACK) << 9;

			FPdataH = i2c_readbyte(decoder->bus, 0); /* 0 = not last byte	  */
			FPdataL = i2c_readbyte(decoder->bus, 1); /* 1 = last byte to read */
			i2c_stop(decoder->bus);

			UNLOCK_I2C_BUS(decoder->bus);

			if (ret) 
				printk(KERN_WARNING "vpx32xx: [3] I/O error, vpx32xx_readFP failed! (code:0x%x)\n", ret);

	                printk(KERN_INFO "vpx32xx_readFP: #%02x -> %04x\n",subaddr,(FPdataH << 8) + FPdataL);
			return ((FPdataH << 8) + FPdataL);
		}
	}

	UNLOCK_I2C_BUS(decoder->bus);

	printk(KERN_INFO "\nvpx32xx: vpx32xx_readFP: FP processor is busy!\n");

	return -EBUSY;
}

/*****************************************************************************/
/*****************************************************************************/

static int 
vpx32xx_set_picture(struct vpx32xx *decoder)	/* set picture for VPX3214, VPX3216 or VPX3220 */
{
	int ret = 0;
	
	/* set brightness */
	ret |= vpx32xx_writeI2C(decoder, 0xE6, ((decoder->win1.brightness - 32768) >> 8) & 0xFF);

	/* set contrast */
	ret |= vpx32xx_writeI2C(decoder, 0xE7, (decoder->win1.contrast >> 10));

	/* set saturation */
	ret |= vpx32xx_writeFP(decoder, 0xA0, (decoder->saturation >> 4));

	/* set hue (for NTSC only) */
	ret |= vpx32xx_writeFP(decoder, 0x1C, ((decoder->hue - 32768) >> 6) & 0xFFF); /* ntsc tint angle: +-512 */
	return ret;
}

static int 
vpx322x_set_picture(struct vpx32xx *decoder)	/* set picture for VPX3224 or VPX3225 */
{
	int ret = 0;
	
	/* set brightness */
	ret |= vpx32xx_writeFP(decoder, 0x127, ((decoder->win1.brightness - 32768) >> 8) & 0xFF);
	ret |= vpx32xx_writeFP(decoder, 0x131, ((decoder->win2.brightness - 32768) >> 8) & 0xFF);

	/* set contrast */
	ret |= vpx32xx_writeFP(decoder, 0x128, ((decoder->win1.contrast >> 10) | 0x0100)); /* set clamping level=16 */
	ret |= vpx32xx_writeFP(decoder, 0x132, ((decoder->win2.contrast >> 10) | 0x0100)); /* set clamping level=16 */

	/* set saturation */
	ret |= vpx32xx_writeFP(decoder, 0x30, (decoder->saturation >> 4));

	/* set hue (for NTSC only) */
	ret |= vpx32xx_writeFP(decoder, 0xDC, ((decoder->hue - 32768) >> 6) & 0xFFF); /* ntsc tint angle: +-512 */

	/* latching */
	ret |= vpx32xx_writeFP(decoder, 0x140, 0x860);	/* set latch Timing mode and WinTable #1 + #2 */
	
	return ret;
}

/*****************************************************************************/

static int 
vpx32xx_set_video_norm(struct vpx32xx *decoder, int video_norm)	/* set video_norm for VPX3214, VPX3216 or VPX3220 */
{
	int i, choice, ret = 0;

	const int init0[][2] = {	/*	I2C init 	*/
	/* addr		  value						*/
	{ 0x00F0	, 0x008A },	/* FIFO Control			*/
	{ 0x0033	, 0x0001 },	/* input select	(TV)		*/

	{ 0x0020	, 0x0003 },	/* Chroma processing		*/ 

	{ 0x00E0	, 0x00FF },	/* alpha keyer ymax		*/
	{ 0x00E1	, 0x0000 },	/* alpha keyer ymin		*/
	{ 0x00E2	, 0x007F },	/* alpha keyer umax		*/
	{ 0x00E3	, 0x0080 },	/* alpha keyer umin		*/
	{ 0x00E4	, 0x007F },	/* alpha keyer vmax		*/
	{ 0x00E5	, 0x0080 },	/* alpha keyer vmin		*/

	{ 0x00E8	, 0x00F8 },	/* color format			*/
	{ 0x00EA	, 0x0018 },	/* misc settings		*/
	{ 0x00D8	, 0x0080 },	/* vref/href control            */
	{ 0x00F8	, 0x0012 },	/* output port A		*/
	{ 0x00F9	, 0x0012 }	/* output port B		*/
	}; 

	const int init1[][2] = {	/*	FP init 	*/
	/* addr	  value							*/
	{ 0x001C	, 0x0000 },	/* neutral tint			*/
	{ 0x004B	, 0x0298 },	/* PLL 				*/
	{ 0x0058	, 0x0000 },	/* oscillator freq. adjust	*/

	/*  WinTable #1 */
        /* PAL windowing within ##DATA## area:

           Blank       Blank
           |Burst      |
           || ##DATA## |
           vv vvvvvvvv v
                 _ -
            #-_-= - ~_-
           _#          _
           ____64us____     => pixels/line: 864 (13.5MHz) 944 (14.75MHz)  |
              _52.15u_      => pixels/line: 720 (13.5MHz) 768 (14.75MHz)  | begin is always 0 */
	{ 0x0088	,      7 },	/* vertical begin		*/
	{ 0x0089	,    288 }, 	/* vertical lines in		*/
	{ 0x008A	,    288 },	/* vertical lines out		*/
	{ 0x008B	,     16 }, 	/* horizontal begin		*/   
	{ 0x008C	,    720 }, 	/* horizontal lines in		*/  
	{ 0x008D	,    720 },	/* number of pixels		*/ 

	/*  WinTable #2 */ 	
	{ 0x008E	,    320 },  	/* vertical begin          	*/
	{ 0x008F	,    288 },  	/* vertical lines in          	*/
	{ 0x0090	,    288 },  	/* vertical lines out          	*/
	{ 0x0091	,     16 },  	/* horizontal begin          	*/
	{ 0x0092	,    720 },  	/* horizontal lines in         	*/
	{ 0x0093	,    720 },  	/* number of pixels       	*/

	{ 0x00A0	, 0x0816 },	/* ACC				*/
	{ 0x00A8	, 0x001E },	/* color killer			*/
	{ 0x00B2	, 0x0300 },	/* gain control			*/ 
	{ 0x00BE	, 0x001B },	/* gain control			*/
	{ 0x00E7	, 0x020B },	/* vertical standard		*/

	{ 0x00F0	, 0x0173 },	/* 13.5  MHz, Open		*/
	{ 0x00F2	, 0x03D1 }	/* PAL BGHI, composite		*/ 
	};

	const int norms[3][9] = {
	/*                ver.b.  ver.li. ver.lo. hor.b.  hor.l.  nr.px.  latch   tvnorm ver.b.2 */
	/* PAL   */	{      7,    288,    288,     16,    720,    720, 0x0173, 0x03D1, 320 },
	/* NTSC  */	{      7,    240,    240,     16,    720,    720, 0x0173, 0x03D1, 270 },
	/* SECAM */	{      7,    288,    288,     16,    720,    720, 0x0173, 0x03D1, 320 }
			};

	switch (video_norm) {
		case VIDEO_MODE_PAL: 	choice = 0; break;	/* PAL B,G,H,I 625/50	*/
		case VIDEO_MODE_NTSC: 	choice = 1; break;	/* NTSC-M 525/60 	*/
		case VIDEO_MODE_SECAM: 	choice = 2; break;	/* SECAM 625/50 	*/
		default:
			return -EPERM;
	}

	/*	start of vpx-initialisation	*/

	if (decoder->part_number == VPX3214C)
		ret |= vpx32xx_writeI2C(decoder, 0xAA, 0x00);	/* normal power mode */

	ret |= vpx32xx_writeI2C(decoder, 0xF1, 0x98);		/* output multiplexer */
	ret |= vpx32xx_writeI2C(decoder, 0xF2, 0x5B);		/* output enable (with pixclk sync.) */

	/*	I2C init first 	*/
	for (i = 0; i < (sizeof(init0) / (2 * sizeof(int))); i++)
		ret |= vpx32xx_writeI2C(decoder, (unsigned char)(init0[i][0]), (unsigned char)(init0[i][1]));

	/*	FP is next 	*/
	for (i = 0; i < (sizeof(init1) / (2 * sizeof(int))); i++)
		ret |= vpx32xx_writeFP(decoder, init1[i][0], init1[i][1]);

	ret |= vpx32xx_writeFP(decoder, 0x0088, norms[choice][0]);	/* Vertical Begin	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x0089, norms[choice][1]);	/* Vertical Lines In	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x008A, norms[choice][2]);	/* Vertical Lines Out	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x008B, norms[choice][3]);	/* Horizontal Begin	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x008C, norms[choice][4]);	/* Horizontal Length	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x008D, norms[choice][5]);	/* Number of Pixels	#1	*/
	ret |= vpx32xx_writeFP(decoder, 0x008E, norms[choice][8]);	/* Vertical Begin	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x008F, norms[choice][1]);	/* Vertical Lines In	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x0090, norms[choice][2]);	/* Vertical Lines Out	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x0091, norms[choice][3]);	/* Horizontal Begin	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x0092, norms[choice][4]);	/* Horizontal Length	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x0093, norms[choice][5]);	/* Number of Pixels	#2	*/
	ret |= vpx32xx_writeFP(decoder, 0x00F0, norms[choice][6]);	/* Latch for Window #1 + #2	*/
	ret |= vpx32xx_writeFP(decoder, 0x00F2, norms[choice][7]);	/* TV Standard 			*/

	if (0==ret) decoder->norm = video_norm;

	return ret;
}

static int 
vpx322x_set_video_norm(struct vpx32xx *decoder, int video_norm)	/* set video_norm for VPX3224 and VPX3225 */
{
	int ret = 0;
	const int norms[3][8] = {
	/*                ver.b.  ver.li. ver.lo. hor.b.  hor.l.  nr.px.  latch   tvnorm */
	/* PAL   */	{ 0x0018, 0x0130, 0x0130, 0x0000, 0x0300, 0x0300, 0x0173, 0x03D1 },
	/* NTSC  */	{ 0x001B, 0x00F0, 0x00F0, 0x002A, 0x0290, 0x02B0, 0x0061, 0x03D3 },
	/* SECAM */	{ 0x0018, 0x0130, 0x0130, 0x0000, 0x0300, 0x0300, 0x0061, 0x03D5 }
			};

	//const int norms[8][11] = {
	// /*                ver.b.  ver.li. ver.lo. hor.b.  hor.l.  nr.px.  sel.p/c st.v.a. e.v.a.  col.pr. */
	// /* PAL  */	{ 0x0007, 0x0130, 0x0130, 0x0000, 0x0330, 0x0330, 0x0000, 0x0008, 0x0330, 0x07FF, 0x0000 }, 
	// /* NTSC  */	{ 0x000A, 0x00F0, 0x00F0, 0x0000, 0x0290, 0x02B0, 0x0000, 0x0030, 0x02A8, 0x07FF, 0x0001 },
	// /* SECAM */	{ 0x0007, 0x0130, 0x0130, 0x0000, 0x0330, 0x0330, 0x0000, 0x0008, 0x0330, 0x017F, 0x0002 },
// 
	// /* PAL 60 */	{ 0x000A, 0x00F0, 0x00F0, 0x0000, 0x0290, 0x02B0, 0x0000, 0x0030, 0x02A8, 0x07FF, 0x0006 },
	// /* PAL M  */	{ 0x000A, 0x00F0, 0x00F0, 0x0000, 0x0290, 0x02B0, 0x0000, 0x0030, 0x02A8, 0x07FF, 0x0004 },
// 
	// /* PAL N  */	{ 0x0007, 0x0130, 0x0130, 0x0000, 0x0330, 0x0330, 0x0000, 0x0008, 0x0330, 0x07FF, 0x0005 }, 
	// /* NTSC44  */	{ 0x000A, 0x00F0, 0x00F0, 0x0000, 0x0290, 0x02B0, 0x0000, 0x0030, 0x02A8, 0x07FF, 0x0003 },
	// /* NTSC COMP */	{ 0x000A, 0x00F0, 0x00F0, 0x0000, 0x0290, 0x02B0, 0x0000, 0x0030, 0x02A8, 0x07FF, 0x0007 }
			// };

	if (video_norm < 0 || video_norm > 7){
		printk(KERN_WARNING "vpx32xx: vpx322x_set_video_norm error, norm invalid! (code:0x%x) Set PAL instead\n", video_norm);
		video_norm = 0;
	}
	
	/* Output */
	ret |= vpx32xx_writeI2C(decoder, 0xAA, 0x00);		/* Low Power Mode, bit 4 on TDO-pin and 0 */
	//ret |= vpx32xx_writeI2C(decoder, 0xF2, 0x0F);		/* Output Enable */
	ret |= vpx32xx_writeI2C(decoder, 0xF2, 0x5B);		/* output enable */

	/* Formatter */
	ret |= vpx32xx_writeFP(decoder, 0x0150, 0x0010);	/* Format Selection */

	/* Output Multiplexer */
	ret |= vpx32xx_writeFP(decoder, 0x0154, 0x0000);	/* Multiplexer/Multipurpose output */ 

//	/* Load Table for Window #1 */
	ret |= vpx32xx_writeFP(decoder, 0x0120, norms[video_norm][0]);	/* Vertical Begin		*/
	ret |= vpx32xx_writeFP(decoder, 0x0121, norms[video_norm][1]);	/* Vertical Lines In		*/
	ret |= vpx32xx_writeFP(decoder, 0x0122, norms[video_norm][2]);	/* Vertical Lines Out		*/
	ret |= vpx32xx_writeFP(decoder, 0x0123, norms[video_norm][3]);	/* Horizontal Begin	 	*/
	ret |= vpx32xx_writeFP(decoder, 0x0124, norms[video_norm][4]);	/* Horizontal Length		*/
	ret |= vpx32xx_writeFP(decoder, 0x0125, norms[video_norm][5]);	/* Number of Pixels		*/
	ret |= vpx32xx_writeFP(decoder, 0x0126, norms[video_norm][6]);	/* Selection for peaking/coring	*/
//
//	/* Load Table for Window #2 */
//	ret |= vpx32xx_writeFP(decoder, 0x012B, 0x0C00);		/* Vertical Lines In (window #2 disabled) */
//
	/* HVREF */
	ret |= vpx32xx_writeFP(decoder, 0x0151, norms[video_norm][7]);	/* Startpos. of programmable video aktive */
	ret |= vpx32xx_writeFP(decoder, 0x0152, norms[video_norm][8]);	/* Endpos. of programmable video aktive */
	ret |= vpx32xx_writeFP(decoder, 0x0153, 0x0027);		/* Length/Polarity of HREF, VREF, FIELD	*/

	/* Standard Selection */	
	ret |= vpx32xx_writeFP(decoder, 0x0020, norms[video_norm][10]);	/* tv norm setting */

	/* Load Table for VBI-Window */
	ret |= vpx32xx_writeFP(decoder, 0x0138, 0x0800);		/* Control VBI-Window (vbi window disabled) */ 

	/* Control Word */
	ret |= vpx32xx_writeFP(decoder, 0x0140, 0x0864);		/* Latch for Window #1 + #2 */ 

	/* Color Processing */	
	ret |= vpx32xx_writeFP(decoder, 0x0030, norms[video_norm][9]);	/* acc reference setting */

	if (0==ret) decoder->norm = video_norm;

	return ret;
}

static int 
vpx32xx_selmux(struct vpx32xx *decoder, int video_input)	/* input select for VPX3214, VPX3216, VPX3218, VPX3224 and VPX3225 */
{
	int data, ret = 0;
	const int input_vpx322x[VPX32XX_MAX_INPUT][2] = {
	/*  0x21 , s-vhs					*/
	{  0x0224,   0 },	/* CVBS	(VIN3)			*/
	{  0x0225,   0 },	/* CVBS	(VIN2)			*/
	{  0x0226,   0 },	/* CVBS	(VIN1)			*/

	{  0x0224,   1 },	/* S-VHS (Y-VIN3, C-CIN) 	*/
	{  0x0225,   1 },	/* S-VHS (Y-VIN2, C-CIN)	*/
	{  0x0226,   1 },	/* S-VHS (Y-VIN1, C-CIN) 	*/
	{  0x0220,   1 },	/* S-VHS (Y-VIN3, C-VIN1)	*/
	{  0x0221,   1 }};	/* S-VHS (Y-VIN2, C-VIN1)	*/

	const int input_vpx32xx[VPX32XX_MAX_INPUT][2] = {
	/* 0x33	 , s-vhs					*/
	{  0x0000,   0 },	/* CVBS	(VIN3)			*/
	{  0x0001,   0 },	/* CVBS	(VIN2)			*/
	{  0x0002,   0 },	/* CVBS (VIN1)			*/

	{  0x0004,   1 },	/* S-VHS (Y-VIN3, C-CIN)	*/
	{  0x0005,   1 },	/* S-VHS (Y-VIN2, C-CIN)	*/
	{  0x0006,   1 },	/* S-VHS (Y-VIN1, C-CIN)	*/
	{  0x0000,   1 },	/* S-VHS (Y-VIN3, C-VIN1)	*/
	{  0x0001,   1 }};	/* S-VHS (Y-VIN2, C-VIN1)	*/

	switch (decoder->part_number) {
		case VPX3224D: case VPX3225D:
			data = vpx32xx_readFP(decoder, 0x20) & ~(0x0040);	/* read word and erase bit 6 */
			if (0 > data) return data; 
			ret |= vpx32xx_writeFP(decoder, 0x20, (data | (input_vpx322x[video_input][1] << 6)));
			ret |= vpx32xx_writeFP(decoder, 0x21, input_vpx322x[video_input][0]);
			break;
		case VPX3220A: case VPX3216B: case VPX3214C:
			ret |= vpx32xx_writeI2C(decoder, 0x33, (unsigned char)(input_vpx32xx[video_input][0]));
			data = vpx32xx_readFP(decoder, 0xF2) & ~(0x0020);	/* read word and erase bit 5 */ 
			if (0 > data) return data;
			ret |= vpx32xx_writeFP(decoder, 0xF2, (data | (input_vpx32xx[video_input][1] << 5)));
			break;
		default:
			return -EINVAL;
	}
	if (0==ret) decoder->input = video_input;
	return ret;
}

static int 
vpx322x_dump(struct vpx32xx *decoder)	/* dump VPX3224 or VPX3225 registers */
{
	int i, data_0, data_1, ret = 0;
	 
	printk(KERN_INFO "vpx32xx: dump i2c registers:\n");
	for (i = 0; i < 0x100; i = i + 2) {
		ret |= data_0 = vpx32xx_readI2C(decoder, i);
		ret |= data_1 = vpx32xx_readI2C(decoder, i + 1);
		printk(KERN_INFO "vpx32xx: i2c_adr: 0x%02x = 0x%02x,  0x%02x = 0x%02x\n", i, data_0, i + 1, data_1);
	}

	printk(KERN_INFO "vpx32xx: dump fp-ram registers:\n");
	for (i = 0; i < 0x180; i = i + 2) {
		ret |= data_0 = vpx32xx_readFP(decoder, i);
		ret |= data_1 = vpx32xx_readFP(decoder, i + 1);
		printk(KERN_INFO "vpx32xx: fp_adr: 0x%03x = 0x%03x,  0x%03x = 0x%03x\n", i, data_0, i + 1, data_1);
	}
	printk(KERN_INFO "vpx32xx: end of vpx-register dump\n");
	 
	return ret;
}

/*****************************************************************************/

#if 0
static int 
vpx3225_init_vbi_sliced(struct vpx32xx *decoder)	/* vbi sliced data only possible on VPX3225 */
{
	int ret = 0;

	/*	start vbi-initialisation	*/
	/* 				 addr  value								*/
	ret |= vpx32xx_writeFP(decoder, 0x0121, 0x0C00);	/* Vertical Lines (window #1 disable) 		*/

	ret |= vpx32xx_writeI2C(decoder, 0xC7, 0x01);	/* accumulator mode: reset DC, AC, and FLT accu		*/
	ret |= vpx32xx_writeI2C(decoder, 0xC7, 0x30); 	/* accumulator mode: dc and ac adaptation enable	*/
	ret |= vpx32xx_writeI2C(decoder, 0xCE, 0x15); 	/* bit error tolerance					*/
	ret |= vpx32xx_writeI2C(decoder, 0xB8, 0x00); 	/* clock run-in and framing code don´t care mask high	*/
	ret |= vpx32xx_writeI2C(decoder, 0xB9, 0x00); 	/* clock run-in and framing code don´t care mask mid	*/
	ret |= vpx32xx_writeI2C(decoder, 0xBA, 0x03); 	/* clock run-in and framing code don´t care mask low	*/ 
	ret |= vpx32xx_writeI2C(decoder, 0xC8, 0x40); 	/* sync slicer						*/
	ret |= vpx32xx_writeI2C(decoder, 0xC0, 0x10); 	/* soft slicer						*/ 
	ret |= vpx32xx_writeI2C(decoder, 0xC6, 0x40); 	/* data slicer						*/ 
	ret |= vpx32xx_writeI2C(decoder, 0xC5, 0x07); 	/* filter coefficient					*/
	ret |= vpx32xx_writeI2C(decoder, 0xC9, 0x01); 	/* standard ... pal, ttx				*/
	ret |= vpx32xx_writeI2C(decoder, 0xBB, 0x27); 	/* clock run-in and framing code reference high		*/
	ret |= vpx32xx_writeI2C(decoder, 0xBC, 0x55); 	/* clock run-in and framing code reference mid		*/
	ret |= vpx32xx_writeI2C(decoder, 0xBD, 0x55); 	/* clock run-in and framing code reference low		*/
	ret |= vpx32xx_writeI2C(decoder, 0xC1, 0xBE); 	/* ttx bitslicer frequency LSB	... wst pal		*/
	ret |= vpx32xx_writeI2C(decoder, 0xC2, 0x0A); 	/* ttx bitslicer frequency MSB				*/
	ret |= vpx32xx_writeI2C(decoder, 0xCF, 0xAB); 	/* output mode	vid.lin data output, 64-byte mode disable */

	ret |= vpx32xx_writeFP(decoder, 0x0134,	0x013F); /* Start line even field	319		*/
	ret |= vpx32xx_writeFP(decoder, 0x0135,	0x014F); /* End line even field		335		*/
	ret |= vpx32xx_writeFP(decoder, 0x0136,	0x0006); /* Start line odd field	6		*/
	ret |= vpx32xx_writeFP(decoder, 0x0137,	0x0016); /* End line odd field		22		*/

	ret |= vpx32xx_writeFP(decoder, 0x0139,	0x0000); /* Slicer data size 0 (corresp. to 64)		*/
	ret |= vpx32xx_writeFP(decoder, 0x0140,	0x0C60); /* Register for Control and Latching 		*/ 
	ret |= vpx32xx_writeFP(decoder, 0x0150,	0x0140); /* Format selection 	disable splitting	*/
	ret |= vpx32xx_writeFP(decoder, 0x0154,	0x0000); /* Multiplexer/Multipurpose output		*/ 

	ret |= vpx32xx_writeI2C(decoder, 0xAA, 0x10);	 /* Low power mode, bit 4 connected to TDO-pin  */
	
	ret |= vpx32xx_writeFP(decoder, 0x0138, 0x0803); /* Control VBI-Window, sliced data, vbi enable	*/ 
	ret |= vpx32xx_writeFP(decoder, 0x0030, 0x0666); /* Color Processing	*/

	return ret; 
}

static int 
vpx3225_init_vbi_raw(struct vpx32xx *decoder)	/* vbi raw data on VPX3225 */
{
	int ret = 0;

	/* vbi-raw initialisation */

	/* Load Table for Window #1 and #2 */
	ret |= vpx32xx_writeFP(decoder, 0x0121, 0x0C00);	/* Vertical Lines In (window #1 disabled) */
	ret |= vpx32xx_writeFP(decoder, 0x012B, 0x0C00);	/* Vertical Lines In (window #2 disabled) */

	/* Load Table for VBI-Window */
	ret |= vpx32xx_writeFP(decoder, 0x0134, 0x013F);	/* Start line even field	319	*/
	ret |= vpx32xx_writeFP(decoder, 0x0135, 0x014F);	/* End line even field		335	*/
	ret |= vpx32xx_writeFP(decoder, 0x0136, 0x0006);	/* Start line odd field		6	*/
	ret |= vpx32xx_writeFP(decoder, 0x0137, 0x0016);	/* End line odd field		22	*/
	ret |= vpx32xx_writeFP(decoder, 0x0139, 0x0000);	/* Slicer data size 0 (corresp. to 64)	*/

	/* Control Word */
	ret |= vpx32xx_writeFP(decoder, 0x0140, 0x0C60);	/* Register for Control and Latching 	*/ 

	/* Formatter */
	ret |= vpx32xx_writeFP(decoder, 0x0150, 0x0010);	/* Format selection			*/

	/* Output Multiplexer */
	ret |= vpx32xx_writeFP(decoder, 0x0154, 0x0000);	/* Multiplexer/Multipurpose output	*/ 

	/* Output */
	ret |= vpx32xx_writeI2C(decoder, 0xAA, 0x10);		/* Low power mode, bit 4 connected to TDO-pin */
	//ret |= vpx32xx_writeI2C(decoder, 0xF2, 0x0F);		/* Output enable (A,B,pixclk,href,vref,field,vact,llc,llc2 */
	ret |= vpx32xx_writeI2C(decoder, 0xF2, 0x5B);		/* output enable */

	ret |= vpx32xx_writeFP(decoder, 0x0138, 0x0801);	/* Control VBI-Window, raw data, vbi enable */ 

	/* Color Processing */	
	ret |= vpx32xx_writeFP(decoder, 0x0030, 0x0666);	/* disable acc */

	return ret; 
}
#endif

/*****************************************************************************/
/*****************************************************************************/

static int 
vpx32xx_attach(struct i2c_device *device)
{
	struct	vpx32xx *decoder;
	int ret = 0;

	device->data = decoder = kmalloc(sizeof(struct vpx32xx), GFP_KERNEL);
	if (decoder == 0) return -ENOMEM;
	
	memset(decoder, 0, sizeof(struct vpx32xx));
	strcpy(device->name, "vpx32xx");
	decoder->bus = device->bus;

	/* chip detection */
	printk(KERN_INFO "%s: ", device->name);

	if (0xEC == vpx32xx_readI2C(decoder, 0x00)) {
		printk("MICRONAS INTERMETALL ");

		decoder->part_number = (vpx32xx_readI2C(decoder, 0x02) << 8) + vpx32xx_readI2C(decoder, 0x01);

		switch (decoder->part_number) {
			case VPX3225D: printk("VPX3225D "); break;
			case VPX3224D: printk("VPX3224D "); break;
			case VPX3220A: printk("VPX3220A "); break;
			case VPX3216B: printk("VPX3216B "); break;
			case VPX3214C: printk("VPX3214C "); break;
			default:
				printk("chip detected but unsupported by this driver\n");
				return -1;
		}
		printk("chip detected and attached ");
	} else {
		printk("no MICRONAS INTERMETALL chip found!\n");
		return -1;
	}

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif

	/* load most common values into the parameter-list */
	decoder->norm = VIDEO_MODE_PAL;
	decoder->win1.brightness = decoder->win2.brightness = 32768;
	decoder->win1.contrast = decoder->win2.contrast = 32768;
	decoder->saturation = 32768;
	decoder->hue = 32768;
	decoder->input = 2;

	switch (decoder->part_number) {	/* set fp-ram register addresses */
		case VPX3225D: case VPX3224D: 
			decoder->fpsta = 0x35; decoder->fprd  = 0x36;
			decoder->fpwr  = 0x37; decoder->fpdat = 0x38;
			ret |= vpx322x_set_video_norm(decoder, decoder->norm);
			ret |= vpx32xx_selmux(decoder, decoder->input);
			ret |= vpx322x_set_picture(decoder);

			break;
		case VPX3220A: case VPX3216B: case VPX3214C:
			decoder->fprd  = 0x26; decoder->fpwr  = 0x27;
			decoder->fpdat = 0x28; decoder->fpsta = 0x29;
			ret |= vpx32xx_set_video_norm(decoder, decoder->norm);
			ret |= vpx32xx_selmux(decoder, decoder->input);
			ret |= vpx32xx_set_picture(decoder);
			break;
		default:
			ret = -EINVAL;
	}

	printk("(status: 0x%x)\n", ret);

//	vpx32xx_writeFP(decoder, 0x00F2, 0x03C1);	/* TV Standard	*/
//	        vpx32xx_writeI2C(decoder, 0x0033, 1);	/* IN 3 */
//                mdelay(500);
//        if (1) {
//		int status = vpx32xx_readFP(decoder, 0x0F3);
//                printk("VIN3: (status-word = 0x%03x)\n", status);
//        }
//	        vpx32xx_writeI2C(decoder, 0x0033, 2);	/* IN 2 */
//                mdelay(500);
//        if (1) {
//		int status = vpx32xx_readFP(decoder, 0x0F3);
//                printk("VIN2: (status-word = 0x%03x)\n", status);
//        }
//	        vpx32xx_writeI2C(decoder, 0x0033, 3);	/* IN 1 */
//                mdelay(500);
//        if (1) {
//		int status = vpx32xx_readFP(decoder, 0x0F3);
//                printk("VIN1: (status-word = 0x%03x)\n", status);
//        }
//	vpx32xx_writeFP(decoder, 0x00F2, 0x03C0);	/* TV Standard	*/
//	//printk(KERN_INFO "\nvpx32xx: vpx32xx_writeFP: FP processor is busy!\n");

	return ret;
}

static int 
vpx32xx_detach(struct i2c_device *device)
{
	struct vpx32xx *decoder = device->data;
	int ret = 0;
	
	/* stop further output */
	ret = vpx32xx_writeI2C(decoder, 0xF2, 0x00);

	printk(KERN_INFO "vpx32xx: %s detached\n", device->name);

	kfree(device->data);

#ifdef MODULE
        MOD_DEC_USE_COUNT;
#endif

	return ret;
}

static int 
vpx32xx_command(struct i2c_device *device, unsigned int cmd, void *arg)
{
	struct vpx32xx *decoder = device->data;
	int ret = 0;

	switch (cmd) {
		case DECODER_GET_CAPABILITIES: {	/* get decoder capabilites */
			struct video_decoder_capability *dc = arg;
DEBUG(printk(KERN_INFO "%s_command: DECODER_GET_CAPABILITIES\n", device->name));
			dc->flags = VIDEO_DECODER_PAL
				  | VIDEO_DECODER_NTSC
				  | VIDEO_DECODER_SECAM;
				  //| VIDEO_DECODER_VBI;
			dc->inputs = VPX32XX_MAX_INPUT;
			dc->outputs = VPX32XX_MAX_OUTPUT;
			return ret;
		}
				
		case DECODER_GET_STATUS: {	/* get decoder status */
			int res = 0, status = 0;
DEBUG(printk(KERN_INFO "%s_command: DECODER_GET_STATUS ", device->name));
			if ((VPX3225D == decoder->part_number) || (VPX3224D == decoder->part_number)) {
				status = vpx32xx_readFP(decoder, 0x13);
			} else {
				status = vpx32xx_readFP(decoder, 0x0F3);
                        }
//DEBUG(printk("(status-word = 0x%03x)\n", status));

			if (0 > status) return status; 
			if ((status & 4) == 0) res |= DECODER_STATUS_GOOD;
			res |= DECODER_STATUS_COLOR;

			switch (decoder->norm) {
				case VIDEO_MODE_PAL:
					res |= DECODER_STATUS_PAL;
					break;
				case VIDEO_MODE_NTSC:
					res |= DECODER_STATUS_NTSC;
					break;
				case VIDEO_MODE_SECAM:
					res |= DECODER_STATUS_SECAM;
					break;
				default:
					break;
			}
			*(int*)arg = res;
			return ret;
		}
	
		case DECODER_SET_NORM: {	/* set video norm */
			int video_norm = *(int*)arg;
DEBUG(printk(KERN_INFO "%s_command: DECODER_SET_NORM (norm=%d)\n", device->name, video_norm));
			switch (decoder->part_number) {
				case VPX3225D: case VPX3224D:
					return vpx322x_set_video_norm(decoder, video_norm);
				case VPX3220A: case VPX3216B: case VPX3214C:
					return vpx32xx_set_video_norm(decoder, video_norm);
				default:
					return -EINVAL;
			}
		}
		
		case DECODER_SET_INPUT: {	/* set video channel input */
			int video_input = *(int*)arg;
DEBUG(printk(KERN_INFO "%s_command: DECODER_SET_INPUT (video channel input=%d)\n", device->name, video_input));
			if ((VPX32XX_MAX_INPUT <= video_input) || (video_input < 0)) /* very important test !! */
				return -EINVAL;
                        //return vpx32xx_selmux(decoder, 2);
			return vpx32xx_selmux(decoder, video_input);
		}

		case DECODER_SET_OUTPUT: {	/* set decoder output - not supported here */
DEBUG(printk(KERN_INFO "%s_command: DECODER_SET_OUTPUT (%d) not supported\n", device->name,*(int *)arg));
			return -EOPNOTSUPP;
		}
		
		case DECODER_ENABLE_OUTPUT: {	/* decoder enable output - not supported here */
DEBUG(printk(KERN_INFO "%s_command: DECODER_ENABLE_OUTPUT (%d) not supported\n", device->name,*(int *)arg));
			return -EOPNOTSUPP;
		}
		
		case DECODER_SET_PICTURE: {	/* set brightness, contrast, saturation, and hue = 0, ..., 65535 */
			struct video_picture *pic = arg;
DEBUG(printk(KERN_INFO "%s_command: DECODER_SET_PICTURE (brightness=%d, hue=%d, color=%d, contrast=%d)\n", device->name, pic->brightness, pic->hue, pic->colour, pic->contrast));
			decoder->win1.brightness = pic->brightness;
			decoder->win2.brightness = pic->brightness;
			decoder->win1.contrast = pic->contrast;
			decoder->win2.contrast = pic->contrast;
			decoder->saturation = pic->colour;
			decoder->hue = pic->hue;

			switch (decoder->part_number) {
				case VPX3225D: case VPX3224D:
					return vpx322x_set_picture(decoder);
				case VPX3220A: case VPX3216B: case VPX3214C:
					return vpx32xx_set_picture(decoder);
				default:
					return -EINVAL;
			}
		}
#if 0
		case DECODER_SET_VBI: {		/* set parameters for vbi mode */
DEBUG(printk(KERN_INFO "%s_command: DECODER_SET_VBI\n", device->name));
			if (VPX3225D == decoder->part_number)	/* vbi_sliced only for VPX3225 */
				return vpx3225_init_vbi_sliced(decoder);
//				return vpx3225_init_vbi_raw(decoder);
			return -EINVAL;
		}
#endif
		case DECODER_DUMP: {
DEBUG(printk(KERN_INFO "%s_command: DECODER_DUMP\n", device->name));
			if ((VPX3225D == decoder->part_number) || (VPX3224D == decoder->part_number))
				return vpx322x_dump(decoder);
			return -EINVAL;
		}

		default:
DEBUG(printk(KERN_INFO "%s_command: unknown ioctl cmd (0x%x)\n", device->name, cmd));
			return -ENOIOCTLCMD;
	}
}

struct i2c_driver i2c_driver_vpx32xx = {
	"vpx32xx",			/* name */
	I2C_DRIVERID_VIDEODECODER,	/* def. in i2c.h */
	I2C_VPX32XX, I2C_VPX32XX + 1,	/* address range */
	vpx32xx_attach,
	vpx32xx_detach,
	vpx32xx_command
};

/*****************************************************************************/

EXPORT_NO_SYMBOLS;

#ifdef MODULE
void 
cleanup_module(void)
{
	i2c_unregister_driver(&i2c_driver_vpx32xx);
}

int 
init_module(void)
#else
int 
vpx32xx_init(void)
#endif
{
	return i2c_register_driver(&i2c_driver_vpx32xx);
}
