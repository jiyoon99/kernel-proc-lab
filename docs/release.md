# Release Checklist

This checklist defines the final verification path for a Kernel Proc Lab release.

## Scope

A release is ready when the source tree builds from a clean checkout, the exported ioctl ABI matches the documentation, host tests pass, shell scripts pass static checks, and the loaded module works through `/proc`, `/dev`, ioctl, stream, and collector paths.

## Preflight

```bash
make doctor
```

Expected result:

- build tools are available
- kernel headers for `uname -r` are installed
- `shellcheck` is installed
- Secure Boot state is reported
- loaded-module, `/proc`, `/dev`, sysfs, udev, and debugfs state are visible

Warnings about a module not being loaded or debugfs being root-only are acceptable before runtime checks.

## Clean Build

```bash
make clean
make ci-check
```

`make ci-check` covers:

- `usercli`, `labtop`, and `kernel-lab-collector` builds
- ring helper host tests
- ioctl ABI host tests
- kernel module build check
- version check
- release metadata check
- shell syntax check

The compiler identity warning from kbuild is acceptable when the kernel and user compiler are equivalent GCC builds with different binary names.

## Runtime Verification

Load the module before runtime checks:

```bash
make load
make device-node
make runtime-smoke
```

`make runtime-smoke` verifies:

- `/proc/kernel_proc_lab` is readable
- `/dev/kernel_proc_lab` is readable and writable
- `usercli write` and `usercli read` agree
- ioctl stats report the current ABI
- JSON log output contains the written event
- JSON stream receives a new event
- `kernel-lab-collector` persists an event

Optional extended checks:

```bash
make stress-test
make ops-check
make report
```

## Packaging Verification

For DKMS packaging support:

```bash
make install-dkms
make dkms-status
make uninstall-dkms
```

For local command install support:

```bash
make install-command
kernel-lab doctor
labtop
make uninstall-command
```

For collector service templates:

```bash
make install-collector-service
systemctl status kernel-proc-lab-collector.service
make uninstall-collector-service
```

## Documentation Checks

Before tagging a release, update:

- `kernel_proc_lab_version.h`
- `CHANGELOG.md`
- `README.md`
- `docs/abi-v*.md`
- `docs/json-schema.md`
- `docs/packaging.md`

Then run:

```bash
make release-check
```

## Cleanup

Before publishing the source tree:

```bash
make clean
```

Generated artifacts that should not be committed include:

- `*.ko`, `*.o`, `*.mod`, `*.mod.c`
- `.tmp_versions/`, `Module.symvers`, `modules.order`
- `usercli`, `labtop`, `kernel-lab-collector`
- `tests/ring_host_test`, `tests/abi_host_test`
- `reports/`
- `certs/`

