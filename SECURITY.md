# Security Policy

Kernel Proc Lab is an educational Linux kernel module. Treat it as privileged code and test it only on systems where loading out-of-tree modules is acceptable.

## Supported Versions

| Version | Supported |
| --- | --- |
| 0.8.x | yes |

## Privilege Model

- Loading and unloading `kernel_proc_lab.ko` requires root privileges.
- Creating or repairing `/dev/kernel_proc_lab` requires root privileges.
- Destructive or control operations are restricted in the driver and user tools.
- The default device-node mode is `0660`; broad world-writable permissions are not used.
- udev integration is provided to grant access through the active local user session.

## Secure Boot

Systems with Secure Boot enabled may reject unsigned out-of-tree modules.

Supported options:

1. Disable Secure Boot in firmware settings for local lab machines.
2. Sign the module and enroll a MOK key:

```bash
./scripts/create-mok-key.sh
sudo mokutil --import certs/MOK.der
reboot
./scripts/sign-module.sh
```

Do not commit generated keys or certificates. The `certs/` directory is ignored by git.

## Reporting Issues

For public repositories, open a GitHub issue with:

- kernel release from `uname -r`
- distribution version
- `make doctor` output
- exact command that failed
- relevant `dmesg` lines

Do not include private signing keys, local credentials, or unrelated system logs.

## Intended Use

This project is not a hardened production driver. It is intended for learning and portfolio demonstration around:

- character devices
- procfs/sysfs/debugfs
- ioctl ABI design
- event buffering
- pollable streams
- DKMS and module packaging

