# Kernel Proc Lab

리눅스 커널 모듈을 빌드하고 `/proc/kernel_proc_lab`, `/dev/kernel_proc_lab`로 커널 공간 상태를 읽고 쓰는 학습용 프로젝트입니다.

## 프로젝트 요약

Kernel Proc Lab은 커널 모듈, character device, ioctl ABI, ring buffer, poll 기반 이벤트 스트림을 한 프로젝트 안에서 실습할 수 있도록 만든 Linux kernel driver lab입니다. 커널 쪽 상태는 `/proc`, sysfs, debugfs, tracepoint로 노출하고, 사용자 공간에서는 CLI, TUI 모니터, JSONL collector로 확인합니다.

현재 릴리스 기준:

- 모듈 버전: `0.8.0`
- ioctl ABI: `4`
- 장치 인터페이스: `/dev/kernel_proc_lab`
- 상태 인터페이스: `/proc/kernel_proc_lab`, `/sys/class/kernel_proc_lab/kernel_proc_lab`
- 사용자 도구: `usercli`, `labtop`, `kernel-lab-collector`
- 배포 보조: udev rule, systemd service, logrotate template, DKMS scripts

## 검증 상태

마무리 전 기본 검증은 아래 명령으로 확인합니다.

```bash
make ci-check
make doctor
make runtime-smoke
```

검증 범위:

- user-space tools build: `usercli`, `labtop`, `kernel-lab-collector`
- host tests: ring buffer helper, ioctl ABI layout
- kernel module build check
- shell syntax and ShellCheck
- runtime smoke: device read/write, ioctl stats, JSON stream, collector path
- release metadata: version, ABI, docs, packaging hooks

릴리스용 전체 점검 절차는 [docs/release.md](docs/release.md)를 따릅니다.

## 목표

- 커널 모듈 빌드 흐름 익히기
- `module_init`, `module_exit` 구조 이해
- `/proc` 파일 인터페이스 구현
- character device 드라이버 구현
- user space에서 kernel space로 문자열 전달
- 커널 공간 ring buffer로 최근 이벤트 저장
- `poll`/`select`로 새 메시지 이벤트 감지
- user space CLI에서 `/dev` 장치 파일 사용
- `ioctl`로 드라이버 제어 명령 구현
- `dmesg`, `lsmod`, `insmod`, `rmmod` 사용

## 빠른 실행

```bash
./run
```

자주 쓰는 실행 흐름:

```bash
./run live    # heartbeat 켜고 로드
./run reload  # 재빌드 후 모듈 재로드
./run bench   # 로드 후 부하 테스트
./run ops     # 운영 준비 상태 점검
./run fix-perms # udev rule과 /dev 권한 복구
./run top     # btop 스타일 TUI 모니터
./run trace   # tracefs 이벤트 스트림
./run report  # 진단 리포트 생성
./run holders # 모듈 unload를 막는 프로세스 확인
./run dkms-status # DKMS 등록 상태 확인
./run stop    # 언로드
```

`make`를 선호하면 같은 흐름을 target으로 실행할 수 있습니다.

```bash
make live
make reload
make bench
make ops-check
make fix-perms
make top
make trace
make report
make holders
make dkms-status
```

`btop`처럼 어디서든 명령 하나로 실행하려면:

```bash
make install-command
labtop
```

설치되는 명령:

- `labtop`: 필요한 바이너리를 빌드하고, 모듈이 꺼져 있으면 heartbeat 켜고 로드한 뒤, `/dev/kernel_proc_lab`를 복구하고 btop 스타일 TUI 실행
- `kernel-lab`: `./run`과 같은 통합 실행 명령

삭제:

```bash
make uninstall-command
```

## 설치

소스에서 설치하는 흐름을 권장합니다. 커널 모듈은 실행 중인 커널 버전에 맞춰 빌드되어야 하므로, 릴리스에 포함된 `.ko` 파일을 그대로 사용하는 방식은 권장하지 않습니다.

```bash
git clone https://github.com/<owner>/kernel-proc-lab.git
cd kernel-proc-lab
make doctor
make install-command
labtop
```

`labtop` 명령은 필요한 사용자 공간 바이너리를 빌드하고, 모듈이 꺼져 있으면 빌드 후 로드하며, `/dev/kernel_proc_lab` 장치 노드를 복구한 뒤 TUI를 실행합니다. 모듈 로드와 장치 노드 생성에는 `sudo` 권한이 필요할 수 있습니다.

DKMS로 커널 업데이트 이후 자동 재빌드를 관리하려면:

```bash
make install-dkms
make dkms-status
```

삭제:

```bash
make uninstall-command
make uninstall-dkms
```

배포 절차와 릴리스 검증 기준은 [docs/distribution.md](docs/distribution.md), [docs/release.md](docs/release.md)를 참고합니다.

Debian 패키지 형태로 테스트하려면:

```bash
sudo apt install build-essential debhelper dkms
dpkg-buildpackage -us -uc -b
sudo apt install ../kernel-proc-lab_0.8.0_amd64.deb
labtop
```

보안/권한 모델은 [SECURITY.md](SECURITY.md), 테스트 전략은 [docs/testing.md](docs/testing.md)에 정리되어 있습니다.

## 빌드

```bash
make
```

## 환경 진단

커널 헤더, 빌드 도구, 모듈 로드 상태, `/proc`, `/dev`, sysfs, debugfs, Secure Boot 상태를 한 번에 점검합니다.

```bash
make doctor
```

개발 중에는 빌드와 환경 진단을 같이 실행할 수 있습니다.

```bash
make dev-check
```

## 로드

```bash
make load
```

로드 시점 module parameter도 지원합니다.

```bash
MODULE_ARGS="initial_heartbeat_interval_ms=1000 start_heartbeat_on_load=1" make load
```

## 가장 간단한 실행

빌드, 로드, 테스트, 언로드를 한 번에 실행합니다.

```bash
./demo.sh
```

또는:

```bash
make demo
```

처음에 `sudo` 비밀번호를 한 번 물어볼 수 있습니다. 커널 모듈 로드/언로드는 root 권한이 필요합니다.

## Secure Boot 오류

아래 오류가 나오면 코드 문제가 아니라 Secure Boot가 서명되지 않은 커널 모듈을 막은 것입니다.

```text
insmod: ERROR: could not insert module kernel_proc_lab.ko: Key was rejected by service
```

해결 방법은 둘 중 하나입니다.

1. BIOS/UEFI에서 Secure Boot 끄기
2. 모듈 서명용 MOK 키를 등록하기

MOK 키 등록 흐름:

```bash
./scripts/create-mok-key.sh
sudo mokutil --import certs/MOK.der
reboot
```

재부팅 중 파란색 MOK Manager 화면에서 방금 등록한 키를 enroll합니다. 다시 부팅한 뒤:

```bash
cd /home/zion/projects/kernel-proc-lab
make
./scripts/sign-module.sh
./demo.sh
```

## 상태 확인

```bash
cat /proc/kernel_proc_lab
```

예상 출력:

```text
module: kernel_proc_lab
device: /dev/kernel_proc_lab
reads: 1
writes: 0
opens: 0
last_message: hello from kernel space
usage: echo "message" | sudo tee /dev/kernel_proc_lab
```

## 쓰기

```bash
echo "my first kernel message" | sudo tee /dev/kernel_proc_lab
cat /proc/kernel_proc_lab
```

## User Space CLI

```bash
./usercli read
sudo ./usercli write "message from cli"
./usercli read
./usercli stats
./usercli stats --json
./usercli log
./usercli log --json
./usercli config
./usercli health
./usercli health --json
./usercli doctor
./usercli wait
./usercli stream
./usercli stream --nonblock
./usercli stream --json
./usercli filter show
sudo ./usercli filter set type=1
sudo ./usercli filter clear
./usercli watch
sudo ./usercli heartbeat on
./usercli heartbeat status
sudo ./usercli heartbeat interval 1000
sudo ./usercli heartbeat off
sudo ./usercli clear
sudo ./usercli reset-stats
sudo ./usercli reset-log
sudo ./usercli reset-all
./usercli help
./usercli version
./usercli --device /dev/kernel_proc_lab stats
```

대부분의 데스크톱 환경에서는 모듈 로드 시 `/dev/kernel_proc_lab`가 자동 생성됩니다. 자동 생성되지 않으면 `dmesg`, sysfs의 `dev` 파일, `/proc/devices`에서 major/minor number를 확인한 뒤 `mknod`로 만들 수 있습니다.

이 저장소의 `make load`, `make device-node`, `make test`, `./demo.sh`는 `/dev/kernel_proc_lab`가 없을 때 major/minor number를 찾아 장치 노드를 자동 생성합니다.

장치 노드의 커널 기본 권한은 보수적으로 `0660`입니다. udev로 로그인 사용자의 접근 권한을 관리하려면 예시 rule을 설치할 수 있습니다.

```bash
make install-udev-rule
```

## btop 스타일 TUI 모니터

모듈을 로드한 뒤 실행합니다.

```bash
./labtop
```

`labtop`은 커널 모듈 카운터와 시스템 상태를 함께 표시합니다.

- CPU 사용률
- 메모리 사용량
- load average
- uptime
- reads/writes/opens 카운터
- reads/writes/opens/log/heartbeat 초당 변화량
- READY/WARN 운영 상태와 ABI/drop 요약
- 마지막 커널 메시지
- 타입이 붙은 최근 커널 이벤트 로그
- 탭 전환으로 `modinfo`, `/proc`/sysfs, trace format, dmesg tail, doctor, selftest, project files 확인

키 조작:

- `q`: 종료
- `1`: dashboard
- `2`: module info
- `3`: `/proc`/sysfs snapshot
- `4`: trace format
- `5`: dmesg tail
- `6`: doctor 결과
- `7`: selftest 상태
- `8`: project/changelog/KUnit/ring helper
- `9`: 운영 준비 상태 점검
- `j`/`k`: snapshot 탭 스크롤
- `t`: selftest 탭에서 `make selftest` 실행
- `i`: `make ci-check` 실행
- `m`: 모듈 unload를 막는 holder 프로세스 확인
- `f`: event filter 상태 확인
- `g`: collector service와 JSONL tail 확인
- `h`: heartbeat on/off 토글 (`CAP_SYS_ADMIN` 필요)
- `r`: read/write/open 카운터 reset (`CAP_SYS_ADMIN` 필요)
- `l`: ring buffer reset (`CAP_SYS_ADMIN` 필요)
- `a`: 전체 reset (`CAP_SYS_ADMIN` 필요)
- `c`: 마지막 메시지 clear (`CAP_SYS_ADMIN` 필요)
- `w`: 테스트 메시지 write
- `+`/`-`: heartbeat interval 줄이기/늘리기 (`CAP_SYS_ADMIN` 필요)

`/dev/kernel_proc_lab`가 없으면 `/proc/kernel_proc_lab`을 읽는 모드로 자동 전환됩니다. 이 경우 조회는 가능하지만 `r`, `c` 같은 ioctl 제어키는 사용할 수 없습니다.

대시보드는 메시지를 `/proc` snapshot에서 읽어서 화면 갱신만으로 `/dev` read counter가 증가하지 않도록 구성되어 있습니다.

## Ring buffer와 poll

메시지를 쓸 때마다 커널 모듈은 최근 64개 이벤트를 ring buffer에 저장합니다.

```bash
sudo ./usercli write "first event"
sudo ./usercli write "second event"
./usercli log
```

`poll()` 테스트:

```bash
./usercli wait
```

다른 터미널에서 메시지를 쓰면 `wait` 명령이 깨어나고 최근 로그를 출력합니다.

```bash
sudo ./usercli write "wake poll"
```

`/proc/kernel_proc_lab`에서도 `recent_log` 섹션으로 최근 이벤트를 볼 수 있습니다. character device는 `poll()`을 지원하므로 user space 프로그램이 새 메시지 이벤트를 기다리는 구조로 확장할 수 있습니다.

## Stream Read

`stream` 명령은 character device를 stream 모드로 열고, 커널 로그를 한 줄씩 blocking read 합니다.

```bash
./usercli stream
sudo ./usercli write "next event"
```

`--nonblock`을 붙이면 읽을 이벤트가 없을 때 `EAGAIN`을 바로 확인할 수 있습니다.

## Heartbeat delayed work

커널 내부 `delayed_work`로 5초마다 heartbeat 이벤트를 만들 수 있습니다.

```bash
sudo ./usercli heartbeat on
./usercli wait
./usercli heartbeat status
sudo ./usercli heartbeat interval 1000
sudo ./usercli heartbeat off
```

heartbeat가 켜져 있으면 `labtop`의 `heart` 카운터와 `recent log` 패널이 주기적으로 갱신됩니다.

## sysfs device attributes

device model 학습용으로 `/sys/class/kernel_proc_lab/kernel_proc_lab/` 아래에 sysfs attributes를 제공합니다.

```bash
cat /sys/class/kernel_proc_lab/kernel_proc_lab/stats
cat /sys/class/kernel_proc_lab/kernel_proc_lab/last_message
cat /sys/class/kernel_proc_lab/kernel_proc_lab/heartbeat_enabled
cat /sys/class/kernel_proc_lab/kernel_proc_lab/heartbeat_interval_ms
echo 1 | sudo tee /sys/class/kernel_proc_lab/kernel_proc_lab/heartbeat_enabled
echo 1000 | sudo tee /sys/class/kernel_proc_lab/kernel_proc_lab/heartbeat_interval_ms
echo 0 | sudo tee /sys/class/kernel_proc_lab/kernel_proc_lab/heartbeat_enabled
```

`heartbeat_interval_ms`는 250ms 이상 60000ms 이하 값만 허용합니다.

## debugfs

디버깅용으로 `/sys/kernel/debug/kernel_proc_lab/` 아래에 덤프를 제공합니다.

```bash
sudo mount -t debugfs none /sys/kernel/debug
cat /sys/kernel/debug/kernel_proc_lab/status
cat /sys/kernel/debug/kernel_proc_lab/log
```

`debugfs`는 디버깅 전용이므로, 비활성화되어 있거나 마운트되지 않았으면 생성되지 않을 수 있습니다. 그 경우 모듈 동작에는 영향이 없습니다.

## tracepoints

커널 tracepoint를 등록해서 `write`, `clear`, `heartbeat`, `stream` 동작을 tracefs에서 볼 수 있습니다.

```bash
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/kernel_proc_lab/enable'
sudo cat /sys/kernel/tracing/trace | grep kernel_proc_lab
```

또는 `trace_pipe`를 열어두고 사용자 공간에서 메시지를 쓰면 실시간으로 확인할 수 있습니다.

## 테스트

```bash
make test
make selftest
make ops-check
```

`selftest`와 `ops-check`는 모듈이 이미 로드되어 있어야 합니다. 일반적인 흐름은 다음과 같습니다.

```bash
make reload
make device-node
make selftest
make ops-check
```

권한 문제가 있으면:

```bash
make fix-perms
```

부하 테스트는 `/dev/kernel_proc_lab`에 여러 메시지를 쓰고 write counter와 ring buffer 마지막 이벤트를 검증합니다.

```bash
make load
make load-test
```

## 개발자 노트

이 프로젝트는 단순한 `/proc` 예제가 아니라 커널 모듈에서 자주 만나는 인터페이스를 한 곳에 묶어 실험할 수 있게 구성했습니다.

- `/proc/kernel_proc_lab`: 사람이 읽기 쉬운 상태 스냅샷
- `/dev/kernel_proc_lab`: character device read/write/ioctl/poll/stream 인터페이스
- `/sys/class/kernel_proc_lab/kernel_proc_lab/*`: device model 기반 설정과 상태 노출
- `/sys/kernel/debug/kernel_proc_lab/*`: 디버깅용 원시 덤프
- `trace/events/kernel_proc_lab_trace.h`: ftrace/trace-cmd로 관찰 가능한 tracepoint
- `kernel_proc_lab_ring.h`: ring buffer index/snapshot/lookup helper
- `tests/kernel_proc_lab_kunit.c`: KUnit 환경에 연결 가능한 ring buffer helper 테스트
- `labtop`: 커널 모듈 상태와 시스템 지표를 한 화면에서 보는 TUI
- `scripts/load-module.sh`: 이미 로드된 모듈과 누락된 장치 노드를 처리하는 재실행 가능한 로더
- `scripts/reload-module.sh`: 재빌드, 언로드, 로드를 한 번에 수행
- `scripts/load-test.sh`: write counter와 ring buffer를 검증하는 부하 테스트
- `scripts/selftest.sh`: ioctl ABI, JSON 출력, reset, heartbeat, stream을 검증하는 런타임 selftest
- `scripts/runtime-smoke.sh`: 로드된 모듈에서 `/proc`, `/dev`, ioctl, stream, collector 저장 경로를 검증
- `scripts/stress-test.sh`: write/stream/filter/heartbeat/reset 조합을 반복 실행하는 런타임 스트레스 테스트
- `scripts/version-check.sh`: 모듈, user tool, DKMS, 문서의 버전/ABI 표기를 검증
- `tests/ring_host_test.c`: 커널 로드 없이 ring buffer helper와 ABI 상수를 검증하는 host unit test
- `tests/abi_host_test.c`: ioctl ABI struct 크기와 offset을 검증하는 host unit test
- `scripts/trace-live.sh`: tracefs에서 모듈 tracepoint를 실시간 스트리밍
- `scripts/qemu-smoke.sh`: QEMU 기반 smoke test 구성을 위한 골격 스크립트
- `scripts/report.sh`: modinfo, `/proc`, sysfs, debugfs, tracefs, dmesg를 묶은 진단 리포트 생성
- `scripts/ops-check.sh`: 운영 준비 상태를 pass/warn/fail로 판정
- `scripts/fix-perms.sh`: udev rule과 `/dev` 권한을 복구
- `docs/trace-observability.md`: trace-cmd/bpftrace 관찰성 예제
- `docs/abi-v3.md`: typed event 로그와 control ioctl 권한 모델
- `docs/abi-v4.md`: 동적 ring capacity와 event filter ABI
- `docs/json-schema.md`: `usercli --json` 출력 계약
- `docs/packaging.md`: DKMS와 향후 `.deb` 패키징 흐름
- `scripts/doctor.sh`: 실행 전 환경과 런타임 상태를 점검하는 진단 스크립트
- `udev/99-kernel-proc-lab.rules`: 장치 노드 권한 관리를 위한 배포 예시
- `.github/workflows/ci.yml`: user-space 빌드와 shell script 검증용 CI

ioctl ABI는 `KERNEL_PROC_LAB_ABI_VERSION`으로 노출됩니다. 로그 조회는 고정 크기 요청 구조체와 사용자 제공 entry buffer를 사용하는 `GET_LOG_V2` 방식이라, 내부 ring buffer 크기를 바꿔도 ioctl 번호가 바뀌지 않습니다. 모듈과 user tool 버전은 `0.8.0`, ABI 버전은 `4`입니다. ABI v4 로그는 event type, timestamp, pid, uid, comm, message를 포함하고 event filter ioctl을 제공합니다.

현업식 검증 흐름:

```bash
make ci-check       # 빌드, host ABI/ring test, 커널 모듈 빌드, 버전/문서 체크
make runtime-smoke  # 로드된 모듈 대상으로 실제 /dev/ioctl/collector smoke
make stress-test    # stream/filter/reset/heartbeat 반복 스트레스
```

`/proc/kernel_proc_lab`는 사람이 읽기 쉬운 상태 출력입니다. 자동화나 안정적인 파싱에는 ioctl 또는 `usercli --json` 출력을 사용하세요.

## 언로드

```bash
make unload
```

## 로그 확인

```bash
sudo dmesg | tail -30
```

## 다음 개선 아이디어

- QEMU smoke test를 실제 kernel/rootfs 이미지에 연결
- DKMS와 `.deb` 패키징을 배포 파이프라인에 연결
- perf/ftrace 실험 문서 추가: wait queue, delayed work 동작을 관찰하는 절차 정리
- 에러 주입 모드 추가: nonblock stream, poll timeout 같은 경계 상황 재현
