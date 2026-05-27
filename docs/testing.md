# Testing Guide

Kernel Proc Lab uses layered testing because some checks can run without root while runtime module tests require a loaded kernel module.

## Host Checks

Run on a normal development machine:

```bash
make ci-check
shellcheck demo.sh scripts/*.sh
```

Coverage:

- user-space tool builds
- ring-buffer helper tests
- ioctl ABI layout tests
- optional kernel module build
- version and release metadata checks
- shell syntax checks

## Runtime Checks

Run on a machine where loading an out-of-tree module is acceptable:

```bash
make load
make device-node
make runtime-smoke
make stress-test
make ops-check
```

Coverage:

- module load path
- `/proc/kernel_proc_lab`
- `/dev/kernel_proc_lab`
- ioctl stats and config
- JSON log and stream output
- collector persistence path
- reset and heartbeat controls
- basic operational readiness

## KUnit

The repository includes `tests/kernel_proc_lab_kunit.c` for KUnit-capable kernel trees. It targets the pure ring-buffer helper layer rather than user-space or device-node behavior.

The file is intentionally not wired into the external-module Makefile because KUnit availability depends on the target kernel config. To run it, copy or link the test into a KUnit-enabled kernel tree and add it to the relevant Kbuild/Kconfig test target.

Expected suite name:

```text
kernel_proc_lab
```

Covered behavior:

- ABI constants
- retained ring start calculation
- wraparound snapshot order
- retained-entry lookup

## QEMU Smoke

`make qemu-smoke` documents the expected guest flow. A fully automated QEMU job requires a selected kernel image and rootfs that include matching headers and module-loading support.

Recommended future automation:

1. build or download a known-good rootfs
2. boot a matching kernel under QEMU
3. copy the source tree into the guest
4. run `make`
5. load the module
6. run `make runtime-smoke`
7. unload the module

Until that image is pinned, QEMU is tracked as an explicit manual smoke path rather than a fake green CI job.

