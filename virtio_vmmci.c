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
#include <linux/virtio.h>
#include "virtio_vmmci.h"

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_VMMCI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {

};

static int vmmci_validate(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_validate\n");
	return 0;
}

static int vmmci_probe(struct virtio_device *vdev)
{
	printk(KERN_INFO "vmmci_probe\n");
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

static struct virtio_driver virtio_vmmci = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = 	KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = 	id_table,
	.validate = 	vmmci_validate,
	.probe = 	vmmci_probe,
	.remove = 	vmmci_remove,
	.config_changed = vmmci_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze = 	vmmci_freeze,
	.restore = 	vmmci_restore,
#endif
};

static int __init init(void)
{
	int error = register_virtio_driver(&virtio_vmmci);
	if (error)
		printk(KERN_ERR "failed to init vmmci: %d\n", error);

	printk(KERN_INFO "vmmci init success!\n");
	return 0;
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_vmmci);
	printk(KERN_INFO "vmmci exited!\n");
}

module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dave Voutila");
MODULE_DESCRIPTION("OpenBSD VMM Control Interface");
MODULE_VERSION("6.4");
