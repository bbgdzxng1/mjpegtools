#################################################
# config

# where the kernel sources are located
KERNEL_LOCATION=/usr/src/linux
TOPDIR=/usr/src/linux
#################################################
# some magic for using linux kernel settings
# when compiling module(s)

EXTRA_CFLAGS = -DEXPORT_SYMTAB # -save-temps
CURRENT = $(shell uname -r)

here:
	@echo "Making Unified Zoran (zr360x7) driver for $(CURRENT) kernel..."
	DIR=`pwd`; (cd $(KERNEL_LOCATION); make SUBDIRS=$$DIR modules)

obj-m      := zoran.o \
		saa7111.o saa7110.o saa7185.o saa7114.o \
		adv7175.o adv7170.o \
		bt819.o bt856.o \
		vpx3220.o \
		zr36060.o zr36050.o zr36016.o \
		videocodec.o
zoran-objs := zoran_procfs.o zoran_device.o zoran_driver.o zoran_card.o

zoran.o: $(zoran-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(zoran-objs)

install:
	su -c "cp -v $(obj-m) /lib/modules/$(CURRENT)/kernel/drivers/media/video/;/sbin/depmod -a"

clean:
	-rm -f $(obj-m) $(zoran-objs)

include $(KERNEL_LOCATION)/Rules.make
