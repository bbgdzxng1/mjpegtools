/*
    vpx32xx.h	MICRONAS INTERMETALL VPX32XX video decoder driver

    (c) 1999-2001 Peter Schlaf <peter.schlaf@org.chemie.uni-giessen.de>	
        projekt started by Jens Seidel <jens.seidel@hrz.tu-chemnitz.de>
                
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

//#define	I2C_VPX32XX		0x86	/* 0x86 or 0x8E for VPX32XX */
#define	I2C_VPX32XX		0x8E	/* 0x86 or 0x8E for VPX32XX */
#define VPX32XX_MAX_INPUT	8	/* 3 CVBS + 5 SVHS modes are possible */
#define VPX32XX_MAX_OUTPUT	0	/* these chips can only decode */


struct vpx32xx_win {
	int	vbeg;
	int	vlinei;
	int	tdec;
	int	vlineo;
	int	hbeg;
	int	hlen;
	int	npix;
	int	peaking;
	int	brightness;
	int	contrast;
};

struct vpx32xx {
	struct	i2c_bus	*bus;
	int	part_number;
#define VPX3220A	0x4680  /* 16-bit part numbers */
#define VPX3216B	0x4260
#define VPX3214C	0x4280
#define VPX3225D	0x7230
#define VPX3224D	0x7231

	unsigned char	fprd;
	unsigned char	fpwr;
	unsigned char	fpdat;
	unsigned char	fpsta;

	struct vpx32xx_win win1;
	struct vpx32xx_win win2;

	int	norm;
#define VPX_PALBGHI	0x00
#define VPX_PALM	0x04
#define VPX_PALN	0x05
#define VPX_PAL60	0x06
#define VPX_NTSCM	0x01
#define VPX_NTSC44	0x03
#define VPX_NTSCCOMB	0x07
#define VPX_SECAM	0x02
	int	input;
	int	hue;
	int	saturation;
};
