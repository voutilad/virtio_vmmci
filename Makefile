obj-m += virtio_vmmci.o

.PHONY: insmod rmmod

all:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

insmod:	all
	sudo insmod ./virtio_vmmci.ko

rmmod:
	sudo rmmod virtio_vmmci.ko

