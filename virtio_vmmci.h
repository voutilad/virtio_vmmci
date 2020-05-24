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
#include <linux/version.h>
#ifndef _VIRTIO_VMMCI_H
#define _VIRTIO_VMMCI_H

const char *QNAME_MONITOR = "vmmci-monitor";
// const char *QNAME_SYNC = "vmmci-sync";

/* This should be picked up from the kernel config */
#ifdef CONFIG_HZ
#define HZ CONFIG_HZ
#else
/* fallback to 100, the default in alpine-virt */
#define HZ 100
#endif

/* 1 jiffy = 1 HZ...but that assumes our clock doesn't have issues :-( */
#define DELAY_1s HZ
#define DELAY_20s 20 * HZ

#define VIRTIO_ID_VMMCI			0xffff	/* matches OpenBSD's private id */

#define PCI_VENDOR_ID_OPENBSD		0x0b5d
#define PCI_DEVICE_ID_OPENBSD_VMMCI	0x0777

/* Configuration registers */
#define VMMCI_CONFIG_COMMAND		0
#define VMMCI_CONFIG_TIME_SEC		4
#define VMMCI_CONFIG_TIME_USEC		12

/* Features...these get bit-shifted in the Linux virtio code */
#define VMMCI_F_TIMESYNC		0
#define VMMCI_F_ACK			1
#define VMMCI_F_SYNCRTC			2

/*
 * Linux is in a 32/64 bit transition phases where v4.17 and below
 * seem to define timespec64 as just timespec...ugh. Also, this is
 * probably a really bad idea to handle it this way?
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,18,0)
#define TIME_FMT "%ld.%09ld"
#else
#define TIME_FMT "%lld.%09ld"
#endif

#define debug(fmt, ...) \
	do { if (debug) pr_info("vmmci: [%s] " fmt, __func__, ##__VA_ARGS__); \
	} while (0)
#define log(fmt, ...) pr_info("vmmci: " fmt, ##__VA_ARGS__)

#endif // _VIRTIO_VMMCI_H
