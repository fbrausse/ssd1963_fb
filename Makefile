ifneq ($(KERNELVERSION),)
	ccflags-y += -Wall
	obj-m := ssd1963_fb_drv.o
	ssd1963_fb_drv-objs := ssd1963_fb.o ssd1963.o
else
#	KERNELDIR ?= $(wildcard /home/kane/bin/linux-3.0)
#	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
#	KERNELDIR ?= $(if $(wildcard /home/kane/bin/linux-3.0),/home/kane/bin/linux-3.0,/lib/modules/$(shell uname -r)/build)
	KERNELDIR ?= ../linux-rpi-3.2.27
	PWD := $(shell pwd)

3.0:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) $(shell cat ../flags) modules

clean:
	$(RM) *.o *.ko *.mod.c Module.symvers .ssd1963* modules.order
	$(RM) -r .tmp_versions
endif
