obj-m += kernel_proc_lab.o
ccflags-y += -I$(src)

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean load reload unload status holders modinfo report test selftest ring-test abi-test qemu-smoke ops-check fix-perms load-test demo run live bench top trace sign usercli labtop collector doctor dev-check ci-check kernel-build-check version-check release-check packaging-check runtime-smoke stress-test device-node install-command uninstall-command install-udev-rule install-dkms uninstall-dkms dkms-status install-collector-service uninstall-collector-service

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(MAKE) usercli
	$(MAKE) labtop
	$(MAKE) collector

kernel_proc_lab.ko: kernel_proc_lab.c kernel_proc_lab_ioctl.h kernel_proc_lab_ring.h kernel_proc_lab_version.h
	$(MAKE) -C $(KDIR) M=$(PWD) modules

usercli: usercli.c kernel_proc_lab_ioctl.h kernel_proc_lab_version.h
	$(CC) -Wall -Wextra -O2 -o usercli usercli.c

labtop: labtop.c kernel_proc_lab_ioctl.h
	$(CC) -Wall -Wextra -O2 -o labtop labtop.c

collector: collector.c kernel_proc_lab_ioctl.h kernel_proc_lab_version.h
	$(CC) -Wall -Wextra -O2 -o kernel-lab-collector collector.c

tests/ring_host_test: tests/ring_host_test.c kernel_proc_lab_ring.h kernel_proc_lab_ioctl.h
	$(CC) -Wall -Wextra -O2 -I. -o tests/ring_host_test tests/ring_host_test.c

tests/abi_host_test: tests/abi_host_test.c kernel_proc_lab_ioctl.h
	$(CC) -Wall -Wextra -O2 -I. -o tests/abi_host_test tests/abi_host_test.c

clean:
	@test ! -d "$(KDIR)" || $(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) usercli labtop kernel-lab-collector tests/ring_host_test tests/abi_host_test

load: all
	./scripts/load-module.sh

reload:
	./scripts/reload-module.sh

unload:
	./scripts/unload-module.sh

status:
	lsmod | grep kernel_proc_lab || true
	@test -e /proc/kernel_proc_lab && cat /proc/kernel_proc_lab || true
	@test -e /dev/kernel_proc_lab && ls -l /dev/kernel_proc_lab || true

holders:
	./scripts/holders.sh

modinfo: all
	modinfo ./kernel_proc_lab.ko

report:
	./scripts/report.sh

test:
	./scripts/smoke-test.sh

selftest:
	./scripts/selftest.sh

ring-test: tests/ring_host_test
	./tests/ring_host_test

abi-test: tests/abi_host_test
	./tests/abi_host_test

qemu-smoke:
	./scripts/qemu-smoke.sh

ops-check:
	./scripts/ops-check.sh

fix-perms:
	./scripts/fix-perms.sh

load-test:
	./scripts/load-test.sh 100

doctor:
	./scripts/doctor.sh

dev-check: all doctor

ci-check: usercli labtop collector ring-test abi-test kernel-build-check version-check release-check packaging-check
	bash -n demo.sh scripts/*.sh

version-check:
	./scripts/version-check.sh

release-check:
	./scripts/release-check.sh

packaging-check:
	@test -f debian/control
	@test -x debian/rules
	@test -x debian/postinst
	@test -x debian/prerm
	@test -f debian/source/format
	@test -x scripts/labtop-system-launcher.sh
	@test -x scripts/kernel-lab-system.sh
	@test -f SECURITY.md
	@test -f docs/testing.md

runtime-smoke: usercli collector
	./scripts/runtime-smoke.sh

stress-test: usercli
	./scripts/stress-test.sh

kernel-build-check:
	@test -d "$(KDIR)" && $(MAKE) -C $(KDIR) M=$(PWD) modules || echo "skip kernel build: missing $(KDIR)"

device-node:
	./scripts/ensure-device-node.sh

install-udev-rule:
	./scripts/install-udev-rule.sh

install-command:
	./scripts/install-command.sh

uninstall-command:
	./scripts/uninstall-command.sh

install-collector-service:
	./scripts/install-collector-service.sh

uninstall-collector-service:
	./scripts/uninstall-collector-service.sh

install-dkms:
	./scripts/install-dkms.sh

uninstall-dkms:
	./scripts/uninstall-dkms.sh

dkms-status:
	dkms status -m kernel-proc-lab -v 0.8.0 || true

demo:
	./demo.sh

run:
	./run demo

live:
	./run live

bench:
	./run bench

top:
	./run top

trace:
	./scripts/trace-live.sh

sign: all
	./scripts/sign-module.sh
