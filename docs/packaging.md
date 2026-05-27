# Packaging

The repository includes DKMS support and a Debian packaging skeleton. The package is designed to install source and tooling, not a generic prebuilt `.ko`.

## DKMS Layout

Expected install location:

```text
/usr/src/kernel-proc-lab-0.8.0/
```

Minimum files:

- `dkms.conf`
- `Makefile`
- `kernel_proc_lab.c`
- `kernel_proc_lab_ioctl.h`
- `kernel_proc_lab_ring.h`
- `trace/events/kernel_proc_lab_trace.h`

Manual flow:

```bash
make install-dkms
make dkms-status
make uninstall-dkms
```

## Debian Package Layout

The `debian/` directory builds a native package that installs:

- DKMS source under `/usr/src/kernel-proc-lab-0.8.0`
- user tools under `/usr/lib/kernel-proc-lab`
- command wrappers under `/usr/bin`
- udev rule under `/etc/udev/rules.d`
- systemd service under `/lib/systemd/system`
- logrotate config under `/etc/logrotate.d`
- documentation under `/usr/share/doc/kernel-proc-lab`

Build locally:

```bash
sudo apt install build-essential debhelper dkms
dpkg-buildpackage -us -uc -b
```

Install locally:

```bash
sudo apt install ../kernel-proc-lab_0.8.0_amd64.deb
labtop
```

The package post-install script attempts DKMS registration/build/install when `dkms` is available. Failures are non-fatal so the package can still install documentation and user-space tooling on systems that are not ready to build kernel modules.

## Installed Commands

- `labtop`: self-starting TUI launcher
- `kernel-lab`: system wrapper for load/reload/unload/status/doctor/usercli/collector commands

Examples:

```bash
labtop
kernel-lab doctor
kernel-lab usercli stats
kernel-lab collector
```
