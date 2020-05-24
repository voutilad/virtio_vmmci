# An OpenBSD VMM Control Interface (vmmci) for Linux
_...or "How I learned to shut my x270 laptop and not worry about my VMs."_

[![builds.sr.ht status](https://builds.sr.ht/~voutilad/virtio_vmmci.svg)](https://builds.sr.ht/~voutilad/virtio_vmmci?)

This is an implementation of [vmmci(4)](https://man.openbsd.org/vmmci) for
Linux using a customized version of the `virtio_pci` driver from the
mainline kernel. It currently supports the following:

1. **Clean Shutdowns on Request**
   When requested by `vmctl(8)`...you can safely use
   `vmctl stop <you linux guest>` and it'll nicely stop services and
   sync disks!

2. **System Time Synchronization**
   When the host `vmd(8)` emulation of the hardware clock detects a
   clock drift (most likely due to the host being suspended/resumed),
   it fires a `SYNCRTC` message that the Linux `vmmci` driver responds
   to by synchronizing system time to the hardware clock time. (This
   currently only happens during certain host events like resuming
   from a suspended state.)

3. **Tracking Clock Drift**
   At regular intervals (currently 20s), `vmmci` will measure current
   clock drift, recording the current drift amount in seconds and
   nanoseconds parts readable via `sysctl vmmci`

> **NOTE:** if you're here to deal with constant, excessive clock
> drift, see the [FAQ](#wait-why-isnt-this-fixing-my-clock-drift-issues)!

## Example with Linux Guests
![vmd(8) and 3 Linux guests](/example.png?raw=true "VMD(8) and 3 Linux Guests")

Above is a screenshot of the clock sync in practice. Tmux pane `0` is
my instance of `vmd(8)` running in the foreground with verbose
logging. The other panes:

1. **Alpine 3.8.4** (virt) with kernel 4.14.104-0-virt
2. **Debian Buster** (9.8) with kernel 4.9.0-8-amd64 (yeah, something
   is jacked up with dmesg's time...but it IS correct in
   `journalctl(1)` and when checking `timedatectl(1)`)
3. **Ubuntu 18.04** with my custom kernel 4.20.13-obsd+

Take note of the `rtc_fire1` log events from `vmd(8)`. That's where my
laptop comes out of hibernation and the virtual rtc detects a drift
and sends sync requests to the guests. Each Linux guest receives the
request, performs the clock step, and ack's.

## Known Issues or Caveats
Before you dive in, a few things to note:

1. I test and develop using OpenBSD snapshots, so relatively in sync
   with _-current_. (This should work with OpenBSD 6.7 and later.)

2. I lean heavily on the simplification that OpenBSD virtualization
   guests are single CPU currently.

3. This currenly won't solve larger clock issues, such as major drift.

4. I primarily focus on supporting the newest long-term support
   kernels picked up by major distros, which means Linux 5.4 at the
   moment.

5. I focus my testing on **Alpine Linux** guests using their `-virt`
   releases since it's simple to install and manage without a lot of
   ancillary stuff. Plus, _I personally like Alpine_.

## Installation & Usage
This Linux VMMCI currently comes in **two parts:**

1. `virtio_pci_obsd.ko` -- handles the quirks of getting Linux's
   virtio pci framework to properly work with the VMM Control
   Interface device from `vmd(8)`
2. `virtio_vmmci.ko` -- virtio device driver that replicates the
   behavior of OpenBSD's `vmmci(4)` driver

_You will need both modules installed!_

Assuming you've got a recent Linux distro running as a guest already
under OpenBSD, it shouldn't be more than a few minutes to get things
up and running.

### 1. Prerequisites
Install the tools required to build kernel modules using your package
manager or whatever you normally use to install stuff.

For Alpine systems running the `-virt` flavored kernel:

```sh
# apk add gcc make linux-virt-dev
```

Basically you need your kernel headers and some GCC tooling.

### 2. Compiling
This should be easy and expose issues with your lack of prerequisites
or an incompatability with your kernel version:

```sh
$ make
```

> A common source of compiler warnings are from variations in kernel
> versions. Please share your kernel version and the compiler output
> if you have issues!

### 3. Installation
As root, simply run:

```sh
# make install
```

You'll probably see some SSL errors and complaints about missing key
files. _This is expected as you're building an out-of-tree kernel
module that isn't being signed._ If you'd like to sign the module,
you're on your own at the moment, but maybe read the Linux kernel
documentation on it here:

At this point, you'll have 2 new kernel modules. You should see them
if you run:

```sh
$ ls -l /lib/modules/$(uname -r)/extra
total 36
-rw-r--r--    1 root     root         15272 May  9 20:42 virtio_pci_obsd.ko
-rw-r--r--    1 root     root         19872 May  9 20:42 virtio_vmmci.ko
```

### 4. Loading the modules
This also should be easy now since their properly installed. Simply
run:

```sh
# modprobe virtio_vmmci
```

It should load both the `virtio_vmmci.ko` and `virtio_pci_obsd.ko`
modules. They'll be visible when running `lsmod(8)`, but you won't see
a "depends on" entry due to it being a "soft" dependency.

### 5. Checking it's Loaded
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

Lastly, check the sysctl tables. The driver registers 2 particular
values that contain the seconds and nanoseconds portion of the last
measured drift amount:

```
you@guest:~/virtio_vmmci$ sudo sysctl vmmci
vmmci.drift_nsec = 199647574
vmmci.drift_sec = 1
```

In the above example, the total drift is `1.199647574 seconds`.

> In the future I may expose the last measured time as well

### 5. Configuring autoloading at boot time
This is pretty simple in modern distros that use
`/etc/modules-load.d`. As root, create a file
`/etc/modules-load.d/virtio_vmmci.conf` with the contents:

```
virtio_vmmci
```

At boot, you should see the modules loaded automatically.

## Testing and Confirming Module Installation
There are a few things you can do to validate your installation.

### Clock Sync
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

### Clean Shutdown
How can we test a clean shutdown? It's not too hard, but it might not
work the same between distros and versions. Here's what I've done on
Alpine 3.11.6.

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

There _may_ be some variations. The Linux vmmci driver calls a kernel
helper function that handles orchestrating the shutdown via
userspace. (The question of how to shutdown a Linux system from
kernelspace is quite fascinating to explore.)

# Seldomly Asked Questions
Some questions that people...mainly myself...have had...

## Wait, why isn't this fixing my clock drift issues?
My initial release would constantly adjust the guest clock when
detecting drift. I since removed the functionality, but am considering
it again (see https://github.com/voutilad/virtio_vmmci/issues/6).

Some reasons I removed it:

- It's a bandaid on a bigger issue, not a real solution.
- You can apply a bandaid already using something like `hwclock -us`,
  but since it uses `settimeofday(2)` it may not trigger pending
  timers properly!

Constant, excessive drift shouldn't be the norm.

If you or a loved one experience excessive clock drift in your Linux
guests under OpenBSD's vmm(4)/vmd(8) hypervisor framework, please
consider using my fork of vmd(8) and building my custom Linux kernel
as explained in this `tech@opensd.org` post:

https://marc.info/?l=openbsd-tech&m=159028442625596&w=2

## _Isn't just using settimeofday(2) dangerous?_
This isn't using the userland `settimeofday(2)` system call and
instead using a particular kernel function (`do_settimeofday64`[3])
that appears to be pretty analagous to OpenBSD's kernel's
`tc_setclock` function[4] in that it steps the system clock while
triggering any alarms or timeouts that would fire.

Looking at how VirtualBox handles this with their userland guest
additions services, they look for large clock drifts where "large"
is currently > 30 minutes. If it's large, it just uses
`settimeofday(2)`. Otherwise, it tries to use something like
`adjtimex(2)` to accelerate the clock up to the correct time. (This is
something I may consider for vmmci after some more usage/testing.)

See their source for `VBoxServiceTimeSync.cpp`[5].

## _Can't you just use OpenNTPD or some other NTP daemon?_
Maybe for small clock disturbances/drifts, but it's not ideal for
major stepping and only solves the clock problem.

There are two reasons I'd consider using `virtio_vmmci` either in
addition to or in place of relying on an NTP daemon:

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

A lot of modern Linux distros install and enable an NTP daemon by
default these days. That's fine. But don't forget vmmci gives you
**clean shutdowns** as well as properly stepping the clock after a
long suspend/hibernation!

## Why all the nasty Virtio PCI glue code?
Few reasons, but for more background see my email to
_misc@openbsd.org:_ https://marc.info/?t=155102953000002

In short:

1. OpenBSD purposely uses self-asigned PCI and Virtio device
   identifiers to "hide" the VMM Control Interface device
2. Linux's virtio pci code is a LOT more complex and is trying to
   handle a variety of virtio devices...but can't handle a particular
   quirk with how the VMM Control Interface deals with config register i/o.

# Future Work

See the [issues](https://github.com/voutilad/virtio_vmmci/issues/)
page for my ideas on future enhancements. Feel free to add some
yourself, but keep in mind this is:

1. Not my job...it's a hobby
2. It's for my personal use first and foremost
3. My current job is in software but has nothing to do with kernels,
   virtualization, etc. so this is truly an after-hours thing.

# Acknowledgements!
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

5. The wireguard kernel module source tree for showing how to properly
   build out of tree modules:
   https://git.zx2c4.com/wireguard-linux-compat/tree/src

6. Folks that have helped test on different distros with different
   kernel versions :-)

# Footnotes
GitHub might not render these...but believe me they're here :-)

[1] Linux Kernel documentation on generating a private key for signing
kernel modules:
https://www.kernel.org/doc/html/v4.15/admin-guide/module-signing.html#generating-signing-keys

[2] See this write-up on time-sync in vm's:
http://archive.is/ndiy3

[3] See the `time/timekeeping.c` source file:
https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/time/timekeeping.c?h=v4.20#n1220

[4] OpenBSD's `sys/kern/kern_tc.c`:
https://github.com/openbsd/src/blob/e12a049bd4bbd1e8315c373a739e08972ed6dd1d/sys/kern/kern_tc.c#L382

[5] VirtualBox's `VBoxServiceTimeSync.cpp`:
https://www.virtualbox.org/browser/vbox/trunk/src/VBox/Additions/common/VBoxService/VBoxServiceTimeSync.cpp?rev=76553#L683
