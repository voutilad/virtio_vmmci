# A VMM Control Interface (vmmci) for Linux
_...or "How I learned to shut my x270 laptop and not worry about my VMs."_

This is a hacky implementation of [vmmci(4)](https://man.openbsd.org/vmmci) for
Linux using a customized version of the `virtio_pci` driver from the
mainline kernel. It comes in two parts:

1. `virtio_pci_obsd.ko` -- handles the quirks of OpenBSD's PCI
   implementation of `vmmci(4)`
2. `virtio_vmmci.ko` -- virtio device driver that does the clock
   syncing similar to `vmmci(4)`

You need both modules!

## Installation & Usage
This version is a bit easier as it doesn't require my custom kernel
(still available at https://github.com/voutilad/linux) and should work
with a recent (4.x) kernel shipped by your distro.

### 1. Prerequisites
Install the tools required to build kernel modules using your package
manager or whatever you normally use to install stuff. For
Ubuntu/Debian-like systems, you can try the following:

```sh
$ sudo apt install build-essential linux-headers-$(uname -r)
```

### 2. Compiling
This should be easy:

```sh
$ make
```

If that worked and you see _no warnings_, you should have both a
`virtio_pci_obsd.ko` file and a `virtio_vmmci.ko` file.

### 3. Loading
You can either use `make insmod` or manually load the modules:

```sh
$ sudo insmod virtio_pci_obsd.ko
$ sudo insmod virtio_vmmci.ko
```

## Verifying things are Working

After you load `virtio_pci_obsd.ko` you should see your system match
and enable the vmmci PCI device. Check `dmesg(1)` and you should see
something like:

```
[  825.819945] virtio_pci_obsd: loading out-of-tree module taints kernel.
[  825.819945] virtio_pci_obsd: module verification failed: signature and/or required key missing - tainting kernel
[  825.819945] virtio-pci-obsd 0000:00:05.0: runtime IRQ mapping not provided by arch
[  825.819945] virtio_pci_obsd_match: matching 0x0777
[  825.819945] virtio_pci_obsd_match: found OpenBSD device
[  825.819945] virtio-pci-obsd 0000:00:05.0: enabling bus mastering
```

If you check with `lspci(8)` in verbose mode (`lspci -v`) you should
see the device and the fact it's using our `virti_pci_obsd` driver:

```
00:05.0 Communication controller: Device 0b5d:0777
        Subsystem: Device 0b5d:ffff
        Flags: bus master, fast devsel, latency 0, IRQ 9
        I/O ports at 5000 [size=4K]
        Kernel driver in use: virtio-pci-obsd
```

When you load `virtio_vmmci.ko`, you should see a confirmation the
module is loaded and, if setting `debug = 1` in `virtio_vmmci.c`
you'll get some other messages as well in the `dmesg(1)` output:


```
[17769.012388] virtio_vmmci: [vmmci_validate] not implemented
[17769.012388] virtio_vmmci: [vmmci_probe] initializing vmmci device
[17769.012388] virtio_vmmci: [vmmci_probe] ...found feature TIMESYNC
[17769.012388] virtio_vmmci: [vmmci_probe] ...found feature ACK
[17769.012388] virtio_vmmci: [vmmci_probe] ...found feature SYNCRTC
[17769.012388] virtio_vmmci: started VMM Control Interface driver
[17769.034540] virtio_vmmci: [clock_work_func] starting clock synchronization
[17769.034864] virtio_vmmci: [clock_work_func] guest clock: 1550959642.629898000, host clock: 1550959642.638556712
[17769.034867] virtio_vmmci: [clock_work_func] current time delta: -1.991341288
[17769.034870] virtio_vmmci: [clock_work_func] clock synchronization routine finished
```

> Too noisey? You can turn down the log level by setting `debug = 0` in the
> driver code and recompiling.

If you want to test the driver, suspend your OpenBSD host by
triggering `zzz` manually or by something like closing your laptop
lid. Wait at least 10 seconds or so and resume your OpenBSD system. In
the Linux guest, your `dmesg(1)` output will tell you (in less than 30
seconds) that it's detected a clock drift and it's sync'ing the clock:

```
[15670.027879] virtio_vmmci: [clock_work_func] current time delta: 91.482370612
[15670.027879] virtio_vmmci: detected drift greater than 5 seconds, synchronizing clock
[15670.027879] virtio_vmmci: [clock_work_func] clock synchronization routine finished
```

If you check `date` or `timedatectl` on the Linux guest you should see
the system time is very close to our host time.

## But...why? Why did you do this?
I <3 OpenBSD and occasionally have to run GNU/Linux for _$work_ reasons. While
it generally just works (with some caveats around clocksource and usually a
kernel version <= 4.15), what doesn't work yet is being able to suspend my
laptop and have the Linux guests' clocks resync after an extended period of
time.

## ...Huh? What?
Linux guests that run under [vmm(4)](http://man.openbsd.org/vmm) can't properly
update their clocks when [vmd(8)](http://man.openbsd.org/vmd) tells them to like
OpenBSD guests.

From what I can tell, this is because currently the RTC is a partial
implementation of [mc146818][1] and also relies on a custom virtio device called
[vmmci(4)](http://man.openbsd.org/vmmci) that runs on OpenBSD guests and listens
for events from the host that tells it to sync it's clock (when the host detects
clock drift due to things like a host suspend/hibernation).

The goal is to implement a Linux kernel module that can register a virtio device
that will listen for the clock sync events from the OpenBSD host. This should
allow OpenBSD users that suspend/hibernate to have Linux guests that don't
suffer from clocks getting out of sync if they don't halt the guests before
suspending the host.


# Current State & Known Caveats
As of _24 Feb 2019_, `virtio_vmmci` will:

- register as a virtio device when loading the module (`virtio_vmmci.ko`)
- regularly read the OpenBSD host clock via the virtio config registers...
  a) compare the guest clock looking for a drift of `5 seconds` or more
  b) if too much drift, set the guest's system clock via `do_settimeofday64`[2]

Currently, it doesn't _yet_:

- listen for rtc sync control messages from the OpenBSD host
- listen for ANY control messages from the OpenBSD host :-) (still figuring
  out how to do this)


# Learnings from VirtIO hacking...
Here are some notes of things I've learned along the way. Maybe someone will
find this interesting?

## Making Linux assign VMMCI to the virtio-pci driver
OpenBSD "hides" some devices from non-OpenBSD guests by using non-standard PCI
vendor, device, and subsystem identifiers that fall outside established virtio
ranges that Linux explicitly uses to sanity check before "attaching" to a
device.

For instance, on Ubuntu 18.04, `lspci -v` shows (abbreviated):

```
...
00:04.0 SCSI storage controller: Red Hat, Inc. Virtio SCSI
        Subsystem: Device 0b5d:0008
        Flags: bus master, fast devsel, latency 0, IRQ 7
        I/O ports at 4000 [size=4K]
        Kernel driver in use: virtio-pci

00:05.0 Communication controller: Device 0b5d:0777
        Subsystem: Device 0b5d:ffff
        Flags: bus master, fast devsel, latency 0, IRQ 9
        I/O ports at 5000 [size=4K]
```
Where the SCSI controller looks like a Red Hat device because the OpenBSD host
uses Red Hat's "donated" (as they say) identifiers. However, the `vmmci(4)`
device (the `00:05.0` one) uses `0b5d:0777` (`b5d == 'bsd'`...ha).

We can either re-implement all the `virtio_pci` code in our vmmci driver, or do
what I did in the interim and hack the kernel's `virtio_pci` driver.

## Reading the Host clock
OpenBSD gets a little cheeky and uses virtio configuration registers as a way
to transfer the host clock details to the guest. Since the config space is
mapped via the virtio pci driver, the vmmci virtio driver just needs to read
the right config registers to get the host to return the clock.

__IF IT WERE SO EASY!__

Apparently there are differing versions of virtio, and the OpenBSD devices show
up as "legacy" devices. I honestly don't know the differences yet, but the main
difference here is how Linux will try to read the config registers.

OpenBSD currently assumes a non-legacy (?) approach where (forgive my lack of
knowledge here) a single read is attempted against a register that then returns
up to 32-bits of data (a 64-bit read is done via 2 reads of 32 bits at 2
registers 4 bytes apart).

The problem is Linux's legacy virtio pci implementation instead tries to read
1 byte from the register address, then continues down the line of registers
until it has read enough data. This causes garbage data to be read a legacy pci
Linux virtio driver. Hence the `virtio_pci_obsd` customizations other
than just matching PCI ids.

# Acknowledgements
1. Thanks to the OpenBSD `vmm(4)`/`vmd(8)` hackers...especially those that put
   together OpenBSD's `vmmci(4)` driver which acted as my reference point.

2. The [bootlin cross-referencer](https://elixir.bootlin.com/linux/latest/source)
   because holy hell is that thing 10x more useful than poking around Torvald's
   mirror of the official Linux Git repo.

3. This page from "The kernel development community" was very helpful in
   figuring out how to schedule "deferred work" in the kernel:
     https://linux-kernel-labs.github.io/master/labs/deferred_work.html

4. The `virtio_balloon.c` driver in the Linux kernel tree is a relatively
   simple virtio example to understand Linux virtio drivers.

# Footnotes
(GitHub might not render these...but believe me they're here :-) )

[1]: https://github.com/openbsd/src/blob/master/usr.sbin/vmd/mc146818.
[2]: https://elixir.bootlin.com/linux/v4.20.12/source/kernel/time/timekeeping.c#L1222
[3]: https://github.com/voutilad/linux/blob/v4.20-obsd/drivers/virtio/virtio_pci_legacy.c#L49-L91
