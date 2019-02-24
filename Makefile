obj-m += virtio_vmmci.o virtio_pci_obsd.o
virtio_pci_obsd-y := virtio_pci_openbsd.o virtio_pci_common.o

.PHONY: insmod rmmod

all:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	 make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

insmod:	all
	sudo insmod ./virtio_pci_obsd.ko
	sudo insmod ./virtio_vmmci.ko

rmmod:
	sudo rmmod virtio_vmmci.ko
	sudo rmmod virtio_pci_obsd.ko
