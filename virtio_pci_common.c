/*
 * Virtio PCI driver - common functionality for all device versions
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
 * Virtio PCI OpenBSD driver - a hacked version of the standard Virtio PCI
 * driver to handle current OpenBSD quirks.
 *
 * Authors:
 *  Dave Voutila <voutilad@gmail.com>
 */

#include "virtio_pci_common.h"
#include "virtio_vmmci.h"

/* Handle a configuration change: Tell driver if it wants to know. */
static irqreturn_t vp_config_changed(int irq, void *opaque)
{
	struct virtio_pci_device *vp_dev = opaque;

	// printk(KERN_INFO "vp_config_changed: %02x\n", irq);

	virtio_config_changed(&vp_dev->vdev);
	return IRQ_HANDLED;
}

/* A small wrapper to also acknowledge the interrupt when it's handled.
 * I really need an EIO hook for the vring so I can ack the interrupt once we
 * know that we'll be handling the IRQ but before we invoke the callback since
 * the callback may notify the host which results in the host attempting to
 * raise an interrupt that we would then mask once we acknowledged the
 * interrupt. */
static irqreturn_t vp_interrupt(int irq, void *opaque)
{
	// printk(KERN_INFO "virtio_pci_common: vp_interrupt (%d)\n", irq);
	vp_config_changed(irq, opaque);

	return IRQ_HANDLED;
}

/* the config->del_vqs() implementation */
void vp_del_vqs(struct virtio_device *vdev)
{
	// XXX: we don't use queues!
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
    struct virtqueue *vqs[], vq_callback_t *callbacks[],
    const char * const names[])
{
	return 0;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0) && LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
int vp_find_vqs(struct virtio_device *, unsigned nvqs,
    struct virtqueue *vqs[], vq_callback_t *callbacks[],
    const char * const names[], struct irq_affinity *desc)
{
	return 0;
}
#else
/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		struct virtqueue *vqs[], vq_callback_t *callbacks[],
		const char * const names[], const bool *ctx,
		struct irq_affinity *desc)
{
	// XXX: we don't use queues!
	return 0;
}
#endif
const char *vp_bus_name(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	return pci_name(vp_dev->pci_dev);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,18,20)
/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, int cpu)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = vp_dev->vqs[vq->index];
	struct cpumask *mask;
	unsigned int irq;

	if (!vq->callback)
		return -EINVAL;

	if (vp_dev->msix_enabled) {
		mask = vp_dev->msix_affinity_masks[info->msix_vector];
		irq = pci_irq_vector(vp_dev->pci_dev, info->msix_vector);
		if (cpu == -1)
			irq_set_affinity_hint(irq, NULL);
		else {
			cpumask_clear(mask);
			cpumask_set_cpu(cpu, mask);
			irq_set_affinity_hint(irq, mask);
		}
	}
	return 0;
}
#else
/* Setup the affinity for a virtqueue:
 * - force the affinity for per vq vector
 * - OR over all affinities for shared MSI
 * - ignore the affinity request if we're using INTX
 */
int vp_set_vq_affinity(struct virtqueue *vq, const struct cpumask *cpu_mask)
{
	struct virtio_device *vdev = vq->vdev;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_vq_info *info = vp_dev->vqs[vq->index];
	struct cpumask *mask;
	unsigned int irq;

	if (!vq->callback)
		return -EINVAL;

	if (vp_dev->msix_enabled) {
		mask = vp_dev->msix_affinity_masks[info->msix_vector];
		irq = pci_irq_vector(vp_dev->pci_dev, info->msix_vector);
		if (!cpu_mask)
			irq_set_affinity_hint(irq, NULL);
		else {
			cpumask_copy(mask, cpu_mask);
			irq_set_affinity_hint(irq, mask);
		}
	}
	return 0;
}
#endif

const struct cpumask *vp_get_vq_affinity(struct virtio_device *vdev, int index)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	if (!vp_dev->per_vq_vectors ||
	    vp_dev->vqs[index]->msix_vector == VIRTIO_MSI_NO_VECTOR)
		return NULL;

	return pci_irq_get_affinity(vp_dev->pci_dev,
				    vp_dev->vqs[index]->msix_vector);
}

/* Qumranet donated their vendor ID for devices 0x1000 thru 0x10FF. */
static const struct pci_device_id virtio_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_OPENBSD, PCI_DEVICE_ID_OPENBSD_VMMCI) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, virtio_pci_id_table);

static void virtio_pci_release_dev(struct device *_d)
{
	struct virtio_device *vdev = dev_to_virtio(_d);
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* As struct device is a kobject, it's not safe to
	 * free the memory (including the reference counter itself)
	 * until it's release callback. */
	kfree(vp_dev);
}

static int virtio_pci_probe(struct pci_dev *pci_dev,
			    const struct pci_device_id *id)
{
	struct virtio_pci_device *vp_dev, *reg_dev = NULL;
	int rc;

	/* allocate our structure and fill it out */
	vp_dev = kzalloc(sizeof(struct virtio_pci_device), GFP_KERNEL);
	if (!vp_dev)
		return -ENOMEM;

	pci_set_drvdata(pci_dev, vp_dev);
	vp_dev->vdev.dev.parent = &pci_dev->dev;
	vp_dev->vdev.dev.release = virtio_pci_release_dev;
	vp_dev->pci_dev = pci_dev;
	INIT_LIST_HEAD(&vp_dev->virtqueues);
	spin_lock_init(&vp_dev->lock);

	/* enable the device */
	rc = pci_enable_device(pci_dev);
	if (rc)
		goto err_enable_device;

	rc = virtio_pci_obsd_probe(vp_dev);
	if (rc)
		goto err_probe;


	pci_set_master(pci_dev);


	rc = request_irq(pci_dev->irq, vp_interrupt, IRQF_SHARED,
	    dev_name(&vp_dev->vdev.dev), vp_dev);
	if (rc)
		goto err_probe;

	rc = register_virtio_device(&vp_dev->vdev);
	reg_dev = vp_dev;
	if (rc)
		goto err_register;

	return 0;

err_register:
	virtio_pci_obsd_remove(vp_dev);
err_probe:
	pci_disable_device(pci_dev);
err_enable_device:
	if (reg_dev)
		put_device(&vp_dev->vdev.dev);
	else
		kfree(vp_dev);
	return rc;
}

static void virtio_pci_remove(struct pci_dev *pci_dev)
{
	struct virtio_pci_device *vp_dev = pci_get_drvdata(pci_dev);
	struct device *dev = get_device(&vp_dev->vdev.dev);

	pci_disable_sriov(pci_dev);

	unregister_virtio_device(&vp_dev->vdev);

	virtio_pci_obsd_remove(vp_dev);

	// XXX: where should this go?
	free_irq(pci_dev->irq, vp_dev);

	pci_disable_device(pci_dev);
	put_device(dev);
}

static int virtio_pci_sriov_configure(struct pci_dev *pci_dev, int num_vfs)
{
	// XXX: not needed?
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int virtio_pci_freeze(struct device *dev)
{
	// XXX: vmm(4) does not support power management
	return 0;
}

static int virtio_pci_restore(struct device *dev)
{
	// XXX: vmm(4) does not support power management
	return 0;
}

static const struct dev_pm_ops virtio_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(virtio_pci_freeze, virtio_pci_restore)
};
#endif


static struct pci_driver virtio_pci_driver = {
	.name		= "virtio-pci-obsd",
	.id_table	= virtio_pci_id_table,
	.probe		= virtio_pci_probe,
	.remove		= virtio_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm	= &virtio_pci_pm_ops,
#endif
	.sriov_configure = virtio_pci_sriov_configure,
};

module_pci_driver(virtio_pci_driver);

MODULE_AUTHOR("Dave Voutila <voutilad@gmail.com>");
MODULE_DESCRIPTION("virtio-pci-obsd");
MODULE_LICENSE("GPL");
MODULE_VERSION("1");
