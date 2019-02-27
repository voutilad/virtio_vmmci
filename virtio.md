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
