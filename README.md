# A VMM Control Interface (vmmci) for Linux
_...or "How I learned to shut my x270 laptop and not worry about my VMs."_

This is an implementation of [vmmci(4)](https://man.openbsd.org/vmmci) for
Linux using a customized version of the `virtio_pci` driver from the
mainline kernel. It currently supports the following:

1. **Clean Shutdowns** when requested by `vmctl(8)`...you can safely use
   `vmctl stop <you linux guest>` and it'll nicely stop services and
   sync disks!
2. **System Time Synchronization** when the guest clock drifts from
   the host, letting you run Linux guests on your OpenBSD laptop,
   close the lid, and comfortably resume work much later knowing
   something else will set the time :-)

> Next in the work queue is handling the RTC-setting events so the
> module can respond to RTC sync events fired via the emulated
> mc146818 clock.

The Linux VMMCI comes in two parts:

1. `virtio_pci_obsd.ko` -- handles the quirks of getting Linux's
   virtio pci framework to properly work with the VMM Control
   Interface device from `vmd(8)`
2. `virtio_vmmci.ko` -- virtio device driver that replicates the
   behavior of OpenBSD's `vmmci(4)` driver

_You need both modules installed!_

## Installation & Usage
Assuming you've got a recent Linux distro running as a guest already
under OpenBSD, it shouldn't be more than a few minutes to get things
up and running.

However, keep in mind my testing has been mostly on
_Ubuntu 18.04_ with it's stock 4.15.0 Linux kernel as well as my own
[4.20.12](https://github.com/voutilad/linux) customized
kernel. Chances are if the module won't compile on a 4.x kernel, it's
a simple matter of navigating the moving target that is virtio in the
4.x Linux kernel versions. Feel free to either submit a PR or just
open an issue letting me know your kernel version and distro your using.

### 1. Prerequisites
Install the tools required to build kernel modules using your package
manager or whatever you normally use to install stuff. For
Ubuntu/Debian-like systems, you can try the following:

```sh
$ sudo apt install build-essential libelf-dev linux-headers-$(uname -r)
```

### 2. Compiling
This should be easy:

```sh
$ make
```

If that worked and you see _no warnings_, you should have both a
`virtio_pci_obsd.ko` file and a `virtio_vmmci.ko` file.

### 3. Loading the Modules
You can either use `make insmod` or manually load the modules:

```sh
$ sudo insmod virtio_pci_obsd.ko
$ sudo insmod virtio_vmmci.ko
```

### 4. Checking it's Loaded
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
module is loaded:

```
[  256.030878] virtio_vmmci: started VMM Control Interface driver
```

You can enable debug mode either by passing a `debug=1` argument when
loading the `virtio_vmmci.ko` module or toggle it afterwards by
writing either a `0` (off) or `1`/any positive integer (on) to
`/sys/modules/virtio_vmmci/parameters/debug` as the root user. When
debug mode is on, you'll get extra dmesg noise like:

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

### 5. Testing that it Works

#### Testing Clock Sync
You can easily test the clock synchronization by suspending your
OpenBSD host by triggering `zzz` manually or by something like closing
your laptop lid. Wait at least 10 seconds or so and resume your
OpenBSD system. In the Linux guest, your `dmesg(1)` output will tell
you (in less than 30 seconds) that it's detected a clock drift and
it's sync'ing the clock:

```
[15670.027879] virtio_vmmci: [clock_work_func] current time delta: 91.482370612
[15670.027879] virtio_vmmci: detected drift greater than 5 seconds, synchronizing clock
[15670.027879] virtio_vmmci: [clock_work_func] clock synchronization routine finished
```

If you check `date` or `timedatectl` on the Linux guest you should see
the system time is very close to our host time.

#### Testing Clean Shutdown
How can we test a clean shutdown? It's not too hard, but it might not
work the dame between distros and versions. Here's what I've done on
Ubuntu 18.04.

Assuming your vm is up and running:

1. Use `tmux(1)` or another means of getting 2 terminal sessions going
   at once.
2. In one session, `vmctl console <vm name or id>` to connect to the
   VM over the serial console. (This obviously assumes your guest is
   configured to work that way.)
3. In another session, issue `vmctl stop <vm name or id>`.
4. Back in the serial console session, you should see your init
   system...probably `systemd`...start running through the shutdown
   process.


# Seldomly Asked Questions
Some questions people...mainly myself...have had...

## _Isn't just using settimeofday(2) dangerous?_
Maybe? Probably? This isn't a userland `settimeofday(2)` call so it
might be slightly different. I honestly don't know.

Looking at how VirtualBox handles this with their userland guest
additions services, they look for large clock drifts where "large"
is currently > 30 minutes. If it's large, it just uses
`settimeofday(2)`. Otherwise, it tries to use something like
`adjtimex(2)` to accelerate the clock up to the correct time.

See their source for `VBoxServiceTimeSync.cpp` [1].

## _Can't you just use OpenNTPD or some other NTP daemon?_
There are two reasons I'd consider using `virtio_vmmci` versus trying
to rely on an NTP daemon in the guest:

1. **Not every guest has network access.** This precludes NTP as an
   option. Even if the guest has limited network access, it still
   needs access to an NTP server, ideally multiple. This isn't always
   the case.

2. **Large clock drifts like when you suspend your laptop for an
   evening make most NTP daemons sad.** I've never seen an NTP daemon
   that is cool with just jumping the system time ahead
   (i.e. _stepping_) like that. Some require special config to even
   do. Yes, `ntpd(8)` supports a `-s` flag to do an actual set of the time
   and not just an adjustment, but even as the man page says it's for
   startup. (Useful for embedded, clock-less systems like a Raspberry
   Pi.)

In short: why the headache when, if you trust your host's clock, you
can just rely on the host?

## Why all the nasty Virtio PCI glue code?
Few reasons, but for more background see my email to
_misc@openbsd.org:_ https://marc.info/?t=155102953000002

In short:

1. OpenBSD purposely uses self-asigned PCI and Virtio device
   identifiers to "hide" the VMM Control Interface device
2. Linux's virtio pci code is a LOT more complex and is trying to
   handle a variety of virtio devices...but can't handle a particular
   quirk with how the VMM Control Interface deals with config register i/o.

# Current State & Known Caveats
As of _26 Feb 2019_, `virtio_vmmci` will:

1. register as a virtio device when loading the module (`virtio_vmmci.ko`)
2. regularly read the OpenBSD host clock via the virtio config registers...
  a) compare the guest clock looking for a drift of `5 seconds` or more
  b) if too much drift, set the guest's system clock via `do_settimeofday64`[2]
3. listen for shutdown/reboot command requests from the OpenBSD host,
  acknowledge them, and use the Linux kernel's built-in routines for
  initiating shutdowns/reboots via userland, allowing `pid 1` to "do
  the right thing."

Currently, it doesn't _yet_:
* listen for RTC sync control messages from the OpenBSD host and
  adjust the mc146818 device.


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

[1] https://www.virtualbox.org/browser/vbox/trunk/src/VBox/Additions/common/VBoxService/VBoxServiceTimeSync.cpp?rev=76553#L683
[2] https://elixir.bootlin.com/linux/v4.20.12/source/kernel/time/timekeeping.c#L1222
