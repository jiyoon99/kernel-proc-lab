# Distribution Guide

Kernel Proc Lab should be distributed as source plus install automation. Do not publish a prebuilt `kernel_proc_lab.ko` as the primary artifact because kernel modules are tied to the target kernel release, config, compiler, and module signing policy.

## Recommended Channels

1. Git repository
   - Primary channel for source, documentation, CI, and tags.
   - Recommended for portfolio review and development use.

2. GitHub Releases
   - Attach source archives generated from the tag.
   - Include release notes and verified commands.
   - Do not attach local build outputs unless there is a clear compatibility statement.

3. DKMS install flow
   - Preferred local installation path for kernel-module users.
   - Lets the module rebuild when the user upgrades kernels.

4. Debian package
   - Included as a packaging skeleton under `debian/`.
   - Installs DKMS source, command wrappers, udev rules, service templates, logrotate config, and docs.

## Source Install

```bash
git clone https://github.com/<owner>/kernel-proc-lab.git
cd kernel-proc-lab
make doctor
make install-command
labtop
```

The `labtop` command is intentionally self-starting:

- builds the TUI binary when missing
- builds the kernel module when the module is not loaded
- loads `kernel_proc_lab` with heartbeat defaults
- creates or repairs `/dev/kernel_proc_lab`
- starts the terminal monitor

Kernel module load and device-node creation can require `sudo`.

## DKMS Install

```bash
make install-dkms
make dkms-status
```

Uninstall:

```bash
make uninstall-dkms
```

DKMS source is expected under:

```text
/usr/src/kernel-proc-lab-0.8.0/
```

## GitHub Release Flow

Verify the tree:

```bash
make ci-check
make release-check
make doctor
```

If a loaded module is available, also run:

```bash
make runtime-smoke
```

Create and push the tag:

```bash
git tag -a v0.8.0 -m "Kernel Proc Lab v0.8.0"
git push origin main
git push origin v0.8.0
```

Create a GitHub Release from `v0.8.0` and paste the release notes from `docs/release-notes-v0.8.0.md`.

## Debian Package

The included Debian package installs:

```text
/usr/src/kernel-proc-lab-0.8.0/
/usr/bin/labtop
/usr/bin/kernel-lab
/usr/lib/kernel-proc-lab/usercli
/usr/lib/kernel-proc-lab/labtop
/usr/lib/kernel-proc-lab/kernel-lab-collector
/etc/udev/rules.d/99-kernel-proc-lab.rules
/lib/systemd/system/kernel-proc-lab-collector.service
/etc/logrotate.d/kernel-proc-lab
/usr/share/doc/kernel-proc-lab/
```

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

The package attempts DKMS registration during install and does not ship a generic prebuilt `.ko`.

## Publishing Rules

- Commit source, documentation, scripts, tests, and packaging metadata.
- Do not commit local build outputs.
- Do not commit signing keys or MOK material.
- Keep release notes tied to the tagged version.
- Document Secure Boot behavior and module signing requirements.
