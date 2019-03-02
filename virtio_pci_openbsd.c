/*
 * Virtio PCI driver - legacy device support
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
/*
 * Virtio PCI driver - OpenBSD device support
 *
 * This module handles the quirks related to OpenBSD virtio control devices
 * like virtio_vmmci.
 *
 * Authors:
 *  Dave Voutila <voutilad@gmail.com>
 */
#include "virtio_pci_common.h"

/* virtio config->get_features() implementation */
static u64 vp_get_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* When someone needs more than 32 feature bits, we'll need to
	 * steal a bit to indicate that the rest are somewhere else. */
	return ioread32(vp_dev->ioaddr + VIRTIO_PCI_HOST_FEATURES);
}

/* virtio config->finalize_features() implementation */
static int vp_finalize_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* Give virtio_ring a chance to accept features. */
	// vring_transport_features(vdev);

	/* Make sure we don't have any features > 32 bits! */
	BUG_ON((u32)vdev->features != vdev->features);

	/* We only support 32 feature bits. */
	iowrite32(vdev->features, vp_dev->ioaddr + VIRTIO_PCI_GUEST_FEATURES);

	return 0;
}

/* OpenBSD's vmmci does some funky stuff when reading registers, so the normal
   Linux legacy  vp_get won't work since it reads a byte at a time iterating
   over the registers.
 */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	void __iomem *config_addr = vp_dev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(vp_dev->msix_enabled);
	u8 b;
	__le16 w;
	__le32 l;

	BUG_ON(NULL == config_addr);
	switch (len) {
	case 1:
		b = ioread8(config_addr + offset);
		memcpy(buf, &b, sizeof b);
		break;
	case 2:
		w = cpu_to_le16(ioread16(config_addr + offset));
		memcpy(buf, &w, sizeof w);
		break;
	case 4:
		l = cpu_to_le32(ioread32(config_addr + offset));
		memcpy(buf, &l, sizeof l);
		break;
	case 8:
		l = cpu_to_le32(ioread32(config_addr + offset));
		memcpy(buf, &l, sizeof l);
		l = cpu_to_le32(ioread32(config_addr + offset + sizeof l));
		memcpy(buf + sizeof l, &l, sizeof l);
		break;
	default:
		BUG();
	}
}

/* Similar to vp_get, we need ot use the logic from Linux's virtio_pci_modern
   to make sure we write to the device properly
 */
static void vp_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	void __iomem *config_addr = vp_dev->ioaddr +
		VIRTIO_PCI_CONFIG_OFF(vp_dev->msix_enabled);
	u8 b;
	__le16 w;
	__le32 l;

	// BUG_ON(offset + len > vp_dev->device_len);

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof b);
		iowrite8(b, config_addr + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof w);
		iowrite16(le16_to_cpu(w), config_addr + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), config_addr + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), config_addr + offset);
		memcpy(&l, buf + sizeof l, sizeof l);
		iowrite32(le32_to_cpu(l), config_addr + offset + sizeof l);
		break;
	default:
		BUG();
	}
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	return ioread8(vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	iowrite8(status, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	/* 0 status means a reset. */
	iowrite8(0, vp_dev->ioaddr + VIRTIO_PCI_STATUS);
	/* Flush out the status write, and flush in device writes,
	 * including MSi-X interrupts, if any. */
	ioread8(vp_dev->ioaddr + VIRTIO_PCI_STATUS);
}

static u16 vp_config_vector(struct virtio_pci_device *vp_dev, u16 vector)
{
	/* Setup the vector used for configuration events */
	iowrite16(vector, vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
	/* Verify we had enough resources to assign the vector */
	/* Will also flush the write out to device */
	return ioread16(vp_dev->ioaddr + VIRTIO_MSI_CONFIG_VECTOR);
}

static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
				  struct virtio_pci_vq_info *info,
				  unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name,
				  bool ctx,
				  u16 msix_vec)
{
	// XXX: not needed
	return ERR_PTR(-ENOENT);
}

static void del_vq(struct virtio_pci_vq_info *info)
{
	// XXX: we don't use virt queues
}

static const struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
	.get_vq_affinity = vp_get_vq_affinity,
#endif
};

static int virtio_pci_obsd_match(struct pci_dev *pci_dev)
{
	printk(KERN_INFO "virtio_pci_obsd_match: matching 0x%04x\n", pci_dev->device);
	/* Typically we'd only own devices >= 0x1000 and <= 0x103f... */
	if (pci_dev->device < 0x1000 || pci_dev->device > 0x103f) {
		/* but we make an exception for OpenBSD */
		if (pci_dev->device == 0x0777) {
			printk(KERN_INFO "virtio_pci_obsd_match: found OpenBSD device\n");
			return 0;
		}
		printk(KERN_ERR "virtio_pci_obsd_match: unkown device\n");
		return -ENODEV;
	}

	printk(KERN_INFO "virtio_pci_obsd_match: regular virtio match\n");
	return 0;
}

/* the PCI probing function */
int virtio_pci_obsd_probe(struct virtio_pci_device *vp_dev)
{
	struct pci_dev *pci_dev = vp_dev->pci_dev;
	int rc;

	rc = virtio_pci_obsd_match(pci_dev);
	if (rc)
		return rc;

	if (pci_dev->revision != VIRTIO_PCI_ABI_VERSION) {
		printk(KERN_ERR "virtio_pci_obsd: expected ABI version %d, got %d\n",
		    VIRTIO_PCI_ABI_VERSION, pci_dev->revision);
		return -ENODEV;
	}

	rc = dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(64));

	// XXX: I think we can cut this stuff out since vmd(8) only
	// 	supports 64-bit guests
	if (rc) {
		rc = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(32));
	} else {
		/*
		 * The virtio ring base address is expressed as a 32-bit PFN,
		 * with a page size of 1 << VIRTIO_PCI_QUEUE_ADDR_SHIFT.
		 */
		dma_set_coherent_mask(&pci_dev->dev,
				DMA_BIT_MASK(32 + VIRTIO_PCI_QUEUE_ADDR_SHIFT));
	}

	if (rc)
		dev_warn(&pci_dev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	rc = pci_request_region(pci_dev, 0, "virtio-pci-obsd");
	if (rc)
		return rc;

	rc = -ENOMEM;
	vp_dev->ioaddr = pci_iomap(pci_dev, 0, 0);
	if (!vp_dev->ioaddr)
		goto err_iomap;

	vp_dev->isr = vp_dev->ioaddr + VIRTIO_PCI_ISR;

	/* we use the subsystem vendor/device id as the virtio vendor/device
	 * id.  this allows us to use the same PCI vendor/device id for all
	 * virtio devices and to identify the particular virtio driver by
	 * the subsystem ids */
	vp_dev->vdev.id.vendor = pci_dev->subsystem_vendor;
	vp_dev->vdev.id.device = pci_dev->subsystem_device;

	vp_dev->vdev.config = &virtio_pci_config_ops;

	vp_dev->config_vector = vp_config_vector;
	vp_dev->setup_vq = setup_vq;
	vp_dev->del_vq = del_vq;

	return 0;

err_iomap:
	pci_release_region(pci_dev, 0);
	return rc;
}

void virtio_pci_obsd_remove(struct virtio_pci_device *vp_dev)
{
	struct pci_dev *pci_dev = vp_dev->pci_dev;

	pci_iounmap(pci_dev, vp_dev->ioaddr);
	pci_release_region(pci_dev, 0);
}
