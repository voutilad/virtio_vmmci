# Building on Alpine

## About this guide
This was tested using:
- OpenBSD-current snapshot from 7 May 2020
- Alpine Linux v3.11.6 (kernel version 5.4.34-virt)

## Dependencies
Install the following packages:

- gcc
- make
- linux-virt-dev

# Building

1. `$ make`
2. `# make install`

> You'll probably see some SSL errors [1] and complaints about missing keys...this is expected. If you'd like the key to be signed, feel free to follow [2] to figure out how to do so yourself.

# Random notes about timekeeping

https://wiki.postmarketos.org/wiki/Out-of-tree_kernel_modules

https://kb.meinbergglobal.com/kb/time_sync/time_synchronization_in_virtual_machines

[1] An example of the errors you'll probably see: https://github.com/andikleen/simple-pt/issues/8#issue-227415517

[2] Official docs on generating the private key: https://www.kernel.org/doc/html/v4.15/admin-guide/module-signing.html#generating-signing-keys

