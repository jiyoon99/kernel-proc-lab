# Kernel Proc Lab v0.8.0

Kernel Proc Lab is a Linux kernel driver lab that exposes kernel-space state through `/proc`, a character device, ioctl, sysfs, debugfs, tracepoints, and a pollable event stream. The release includes user-space tooling for command-line inspection, a btop-style TUI monitor, and a JSONL collector.

## Highlights

- ioctl ABI v4
- dynamic retained event ring capacity through `log_capacity`
- typed event metadata with pid, uid, comm, timestamp, and event type
- `usercli` read/write/stats/log/config/doctor/filter/heartbeat commands
- self-starting `labtop` command for build, load, device-node repair, and TUI launch
- `kernel-lab-collector` for JSONL event collection
- udev rule, systemd service, logrotate template, and DKMS scripts
- Debian packaging skeleton for local `.deb` builds
- security, testing, distribution, and release checklists
- host ring-buffer tests and ioctl ABI layout tests
- CI, ShellCheck, runtime smoke, stress, doctor, and release-check flows

## Install From Source

```bash
git clone https://github.com/<owner>/kernel-proc-lab.git
cd kernel-proc-lab
make doctor
make install-command
labtop
```

The first `labtop` run may ask for `sudo` because kernel module loading and `/dev/kernel_proc_lab` creation require elevated privileges.

## DKMS

```bash
make install-dkms
make dkms-status
```

Uninstall:

```bash
make uninstall-dkms
```

## Debian Package

```bash
sudo apt install build-essential debhelper dkms
dpkg-buildpackage -us -uc -b
sudo apt install ../kernel-proc-lab_0.8.0_amd64.deb
labtop
```

## Verification

This release was prepared with:

```bash
make ci-check
shellcheck demo.sh scripts/*.sh
make release-check
make doctor
```

Runtime verification on a loaded module:

```bash
make runtime-smoke
```

## Secure Boot

If Secure Boot rejects the unsigned module, either disable Secure Boot in firmware settings or sign the module and enroll a MOK key:

```bash
./scripts/create-mok-key.sh
sudo mokutil --import certs/MOK.der
reboot
./scripts/sign-module.sh
```

## Notes

Prebuilt `.ko` files are not the primary distribution artifact. Build the module on the target system so it matches the running kernel and local module-signing policy.
