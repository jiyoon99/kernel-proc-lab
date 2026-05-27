# Packaging Plan

The repository now includes `dkms.conf` as the first packaging step.

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

## Next Packaging Step

Add a `.deb` package that installs:

- DKMS source under `/usr/src/kernel-proc-lab-0.8.0`
- `usercli` and `labtop` under `/usr/local/bin` or `/usr/bin`
- udev rule under `/etc/udev/rules.d`
- documentation under `/usr/share/doc/kernel-proc-lab`
