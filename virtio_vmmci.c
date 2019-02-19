/*
 *  Implementation of an OpenBSD VMM control interface for Linux guests
 *  running under an OpenBSD host.
 *
 *  Copyright 2019 Dave Voutila
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include "virtio_vmmci.h"

// ??? Does this need to be like the OpenBSD vmmci_softc struct?
struct virtio_vmmci_device {
	struct virtio_device vdev;
	struct pci_dev *pci_dev;
};

static struct pci_device_id vmmci_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_OPENBSD_VMM, PCI_DEVICE_ID_OPENBSD_VMMCI) },
	{ 0 },
};

static struct virtio_device_id vmmci_virtio_id_table[] = {
	{ VIRTIO_ID_VMMCI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VMMCI_F_TIMESYNC, VMMCI_F_ACK, VMMCI_F_SYNCRTC,
};

static int vmmci_validate(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_validate\n");
	return 0;
}

static int vmmci_probe(struct virtio_device *vdev)
{
	/*
	struct virtio_vmmci *vmmci;
	int err;
	*/
	printk(KERN_INFO "vmmci_probe\n");
/*
	vdev->priv = vmmci = kmalloc(sizeof(*vmmci), GFP_KERNEL);

	if (!vmmci) {
		err = -ENOMEM;
		printk(KERN_ERR "kmalloc error in vmmci_probe\n");
		return err;
	}
*/
	return 0;
}

static void vmmci_remove(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_remove\n");
}

static void vmmci_changed(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_change\n");
}

#ifdef CONFIG_PM_SLEEP
static int vmmci_freeze(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_freeze\n");
	return 0;
}

static int vmmci_restore(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_restore\n");
	return 0;
}
#endif

static int vmmci_pci_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	struct virtio_vmmci_device *vmm_dev = NULL;
	int rc;

	printk(KERN_INFO "vmmci_pci_probe...\n");
	printk(KERN_INFO "\tvendor: %d, device: %d\n", pci_dev->vendor, pci_dev->device);
	printk(KERN_INFO "\tsubvendor: %d, subdevice: %d\n", pci_dev->subsystem_vendor, pci_dev->subsystem_device);

	vmm_dev = kzalloc(sizeof(struct virtio_vmmci_device), GFP_KERNEL);
	if (!vmm_dev) {
		printk(KERN_ERR "could not allocate virtio_vmmci_device!\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pci_dev, vmm_dev);
	vmm_dev->vdev.dev.parent = &pci_dev->dev;
	vmm_dev->pci_dev = pci_dev;

	rc = pci_enable_device(pci_dev);
	if (rc) {
		printk(KERN_ERR "could not enable device\n");
		pci_disable_device(pci_dev);
	}

	printk(KERN_INFO "vmmci_pci_probe finished\n");
	return rc;
}

static void vmmci_pci_remove(struct pci_dev *pci_dev)
{
	printk(KERN_INFO "vmmci_pci_remove!\n");
}

static int vmmci_pci_sriov_configure(struct pci_dev *pci_dev, int num_vfs)
{
	printk(KERN_INFO "vmmci_pci_sriov_configure\n");
	return 0;
}

static struct pci_driver vmmci_pci_driver = {
	.name		= "virtio-vmmci",
	.id_table	= vmmci_pci_id_table,
	.probe		= vmmci_pci_probe,
	.remove		= vmmci_pci_remove,
	.sriov_configure = vmmci_pci_sriov_configure,
};


static struct virtio_driver vmmci_virtio_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = 	KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = 	vmmci_virtio_id_table,
	.validate = 	vmmci_validate,
	.probe = 	vmmci_probe,
	.remove = 	vmmci_remove,
	.config_changed = vmmci_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze = 	vmmci_freeze,
	.restore = 	vmmci_restore,
#endif
};

/*
static int __init init(void)
{
	int error = register_virtio_driver(&vmmci_virtio_driver);
	if (error)
		printk(KERN_ERR "failed to init vmmci: %d\n", error);

	printk(KERN_INFO "vmmci init success!\n");
	return 0;
}

static void __exit fini(void)
{
	unregister_virtio_driver(&vmmci_virtio_driver);
	printk(KERN_INFO "vmmci exited!\n");
}
*/

module_pci_driver(vmmci_pci_driver);
MODULE_DEVICE_TABLE(pci, vmmci_pci_id_table);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dave Voutila");
MODULE_DESCRIPTION("OpenBSD VMM Control Interface");
MODULE_VERSION("6.4");
