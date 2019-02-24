obj-m += virtio_vmmci.o virtio_obsd_pci.o
virtio_obsd_pci-y := virtio_pci_common.o virtio_pci_obsd.o

.PHONY: insmod rmmod

all:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

insmod:	all
	sudo insmod ./virtio_vmmci.ko

rmmod:
	sudo rmmod virtio_vmmci.ko
