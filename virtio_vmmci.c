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
#include <linux/moduleparam.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/sysctl.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

#include "virtio_vmmci.h"

/* You can either change the global debug level here by changing the
 * initialization value for "debug" or configure it at runtime via
 * the kernel module parameter. See README.md for details.
 */
static int debug = 0;

static int set_debug(const char *val, const struct kernel_param *kp)
{
	int n = 0, rc;
	rc = kstrtoint(val, 10, &n);
	if (rc || n < 0)
		return -EINVAL;

	return param_set_int(val, kp);
}

static int get_debug(char *buffer, const struct kernel_param *kp)
{
	int bytes;
	bytes = snprintf(buffer, 1024, "%d\n", debug);
	return bytes + 1; // account for NULL
}

static const struct kernel_param_ops debug_param_ops = {
	.set	= set_debug,
	.get	= get_debug,
};

module_param_cb(debug, &debug_param_ops, &debug, 0664);

/* Define our sysctl table entries for exposing our current clock
 * drift in seconds and nanoseconds. (Avoid using floating point vals
 * for now.)
 */
int drift_sec = 0;
int drift_nsec = 0;

static struct ctl_table_header *vmmci_table_header;

static struct ctl_table drift_table[] = {
	{
		.procname	= "drift_sec",
		.mode		= 0444,
		.maxlen		= sizeof(int),
		.data		= &drift_sec,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "drift_nsec",
		.mode		= 0444,
		.maxlen		= sizeof(int),
		.data		= &drift_nsec,
		.proc_handler	= &proc_dointvec,
	},
	{ },
};

static struct ctl_table vmmci_table = {
	.procname	= "vmmci",
	.child		= drift_table,
};

/* Define our basic commands and structs for our device including the
 * virtio feature tables.
 */
enum vmmci_cmd {
	VMMCI_NONE = 0,
	VMMCI_SHUTDOWN,
	VMMCI_REBOOT,
	VMMCI_SYNCRTC,
};

struct virtio_vmmci {
	struct virtio_device *vdev;

	/* Used for monitoring clock drift. Needs scheduling. */
	struct workqueue_struct *monitor_wq;
	struct delayed_work monitor_work;

	/* Used for synchronizing clock. Work is put on from
	 * the general purpose queue from the interrupt handler.
	 */
	struct work_struct sync_work;
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_VMMCI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VMMCI_F_TIMESYNC, VMMCI_F_ACK, VMMCI_F_SYNCRTC,
};


/* Synchronizes the system time to the hardware clock (rtc). Uses a process
 * similar to the one performed by the kernel at startup as defined in
 * the Linux kernel source file /drivers/rtc/hctosys.c. Minus the 32-bit
 * and non-amd64 specific stuff.
 */
static int sync_system_time(void)
{
	int rc = -1;
	struct rtc_time hw_tm;
	struct timespec64 time = {
		.tv_nsec = NSEC_PER_SEC >> 1,
	};

	// Try to open the hardware clock...which should be the emulated
	// mc146818 clock device.
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		printk(KERN_ERR "vmmci unable to open rtc device\n");
		rc = -ENODEV;
		goto end;
	}

	// Reading the rtc device should be the same as getting the host
	// time via the vmmci config registers...just without all the
	// nastiness
	rc = rtc_read_time(rtc, &hw_tm);
	if (rc) {
		printk(KERN_ERR "vmmci failed to read the hardware clock\n");
		goto close;
	}
	time.tv_sec = rtc_tm_to_time64(&hw_tm);

	// Setting the system clock using do_settimeofday64 should be safe
	// as it is similar to OpenBSD's tc_setclock that steps the system
	// clock while triggering any alarms/timeouts that should fire
	rc = do_settimeofday64(&time);
	if (rc) {
		printk(KERN_ERR "vmmci failed to set system clock to rtc!\n");
		goto close;
	}
	log("set system clock to %d-%02d-%02d %02d:%02d:%02d UTC\n",
	    hw_tm.tm_year + 1900, hw_tm.tm_mon + 1, hw_tm.tm_mday,
	    hw_tm.tm_hour, hw_tm.tm_min, hw_tm.tm_sec);

close:
	// I assume this cleans up any references, if the kernel tracks them
	rtc_class_close(rtc);

end:
	return rc;
}

static void sync_work_func(struct work_struct *work)
{
	int rc = 0;

	debug("starting clock synchronization...");
	rc = sync_system_time();
	if (rc)
		debug("clock synchronization failed (%d)\n", rc);
	else
		debug("finished clock synchronization!\n");

}

/* Runs our guest/host clock drift measurements and logs them to the syslog */
static void monitor_work_func(struct work_struct *work)
{
	struct virtio_vmmci *vmmci;
	struct timespec64 host, guest, diff;
	s64 sec, usec; // should these be signed or unsigned?

	debug("measuring clock drift...\n");

	// My god this container_of stuff seems...messy? Oh, Linux...
	vmmci = container_of((struct delayed_work *) work, struct virtio_vmmci, monitor_work);

	vmmci->vdev->config->get(vmmci->vdev, VMMCI_CONFIG_TIME_SEC, &sec, sizeof(sec));
	vmmci->vdev->config->get(vmmci->vdev, VMMCI_CONFIG_TIME_USEC, &usec, sizeof(usec));

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
	getnstimeofday64(&guest);
#else
	ktime_get_real_ts64(&guest);
#endif
	debug("host clock: %lld.%lld, guest clock: " TIME_FMT,
	    sec, usec * NSEC_PER_USEC, guest.tv_sec, guest.tv_nsec);

	host.tv_sec = sec;
	host.tv_nsec = (long) usec * NSEC_PER_USEC;

	diff = timespec64_sub(host, guest);

	// XXX: our globals for tracking drift...since we're not SMP enabled let's
	// ignore locking/unlocking for now...also yes, we're blindly going from a
	// s64 to an int here.
	drift_sec = diff.tv_sec;
	drift_nsec = diff.tv_nsec;

	debug("current clock drift: " TIME_FMT " seconds\n", diff.tv_sec, diff.tv_nsec);

	queue_delayed_work(vmmci->monitor_wq, &vmmci->monitor_work, DELAY_20s);
	debug("drift measurement routine finished\n");
}

static int vmmci_probe(struct virtio_device *vdev)
{
	struct virtio_vmmci *vmmci;

	debug("initializing vmmci device\n");

	vdev->priv = vmmci = kzalloc(sizeof(*vmmci), GFP_KERNEL);
	if (!vmmci) {
		printk(KERN_ERR "vmmci_probe: failed to alloc vmmci struct\n");
		return -ENOMEM;
	}
	vmmci->vdev = vdev;

	if (virtio_has_feature(vdev, VMMCI_F_TIMESYNC))
		debug("...found feature TIMESYNC\n");
	if (virtio_has_feature(vdev, VMMCI_F_ACK))
		debug("...found feature ACK\n");
	if (virtio_has_feature(vdev, VMMCI_F_SYNCRTC))
		debug("...found feature SYNCRTC\n");

	// wire up routine clock drift monitoring
	vmmci->monitor_wq = create_singlethread_workqueue(QNAME_MONITOR);
	if (vmmci->monitor_wq == NULL) {
		printk(KERN_ERR "vmmci_probe: failed to alloc monitoring workqueue\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&vmmci->monitor_work, monitor_work_func);
	queue_delayed_work(vmmci->monitor_wq, &vmmci->monitor_work, DELAY_1s);

	INIT_WORK(&vmmci->sync_work, sync_work_func);

	vmmci_table_header = register_sysctl_table(&vmmci_table);

	log("started VMM Control Interface driver\n");
	return 0;
}

static void vmmci_remove(struct virtio_device *vdev)
{
	struct virtio_vmmci *vmmci = vdev->priv;
	debug("removing device\n");

	cancel_delayed_work(&vmmci->monitor_work);
	flush_workqueue(vmmci->monitor_wq);
	destroy_workqueue(vmmci->monitor_wq);
	cancel_work_sync(&vmmci->sync_work);
	debug("cancelled, flushed, and destroyed work queues\n");

	vdev->config->reset(vdev);
        debug("reset device\n");

	kfree(vmmci);

	unregister_sysctl_table(vmmci_table_header);

	log("removed device\n");
}

static void vmmci_changed(struct virtio_device *vdev)
{
	struct virtio_vmmci *vmmci = vdev->priv;
	s32 cmd = 0;
	debug("reading command register...\n");

	vdev->config->get(vdev, VMMCI_CONFIG_COMMAND, &cmd, sizeof(cmd));

	switch (cmd) {
	case VMMCI_NONE:
		debug("VMMCI_NONE received\n");
		break;

	case VMMCI_SHUTDOWN:
		log("shutdown requested by host!\n");
		orderly_poweroff(false);
		break;

	case VMMCI_REBOOT:
		log("reboot requested by host!\n");
		orderly_reboot();
		break;

	case VMMCI_SYNCRTC:
		log("clock sync requested by host\n");
		schedule_work(&vmmci->sync_work);
		break;

	default:
		printk(KERN_ERR "invalid command received: 0x%04x\n", cmd);
		break;
	}

	if (cmd != VMMCI_NONE
	    && (vdev->features & VMMCI_F_ACK)) {
		vdev->config->set(vdev, VMMCI_CONFIG_COMMAND, &cmd, sizeof(cmd));
		debug("...acknowledged command %d\n", cmd);
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
static int vmmci_validate(struct virtio_device *vdev)
{
	debug("not implemented");
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int vmmci_freeze(struct virtio_device *vdev)
{
	debug("not implemented\n");
	return 0;
}

static int vmmci_restore(struct virtio_device *vdev)
{
	debug("not implemented\n");
	return 0;
}
#endif

static struct virtio_driver virtio_vmmci_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = 	KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = 	id_table,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	.validate = 	vmmci_validate,
#endif
	.probe = 	vmmci_probe,
	.remove = 	vmmci_remove,
	.config_changed = vmmci_changed,
#ifdef CONFIG_PM_SLEEP
	.freeze = 	vmmci_freeze,
	.restore = 	vmmci_restore,
#endif
};

module_virtio_driver(virtio_vmmci_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OpenBSD VMM Control Interface");
MODULE_AUTHOR("Dave Voutila <voutilad@gmail.com>");
