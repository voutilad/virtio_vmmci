# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2020 Dave Voutila <dave@sisu.io>. All rights reserved.

ccflags-y := -O3 -Wall
ccflags-y += -DCONFIG_HZ=$(CONFIG_HZ)
ccflags-$(CONFIG_VMMCI_DEBUG) += -DDEBUG -g

obj-m += virtio_vmmci.o virtio_pci_obsd.o
virtio_pci_obsd-y := virtio_pci_openbsd.o virtio_pci_common.o
