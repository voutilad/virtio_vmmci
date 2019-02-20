# virtio_vmmci

# requirements
Before you go any further, you'll need my custom [linux
kernel](https://github.com/voutilad/linux) that contains my hacks on the
`virtio_pci` drivers.

See this diff:
[https://patch-diff.githubusercontent.com/raw/voutilad/linux/pull/1.patch] 

# background

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

[1]: https://github.com/openbsd/src/blob/master/usr.sbin/vmd/mc146818.c  
