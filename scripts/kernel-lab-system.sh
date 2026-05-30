#!/usr/bin/env bash
set -euo pipefail

package_name="kernel-proc-lab"
package_version="0.8.0"
source_dir="/usr/src/${package_name}-${package_version}"
lib_dir="/usr/lib/kernel-proc-lab"

require_source_dir() {
  if [[ ! -d "${source_dir}" ]]; then
    echo "missing DKMS source directory: ${source_dir}" >&2
    echo "reinstall the ${package_name} package" >&2
    exit 1
  fi

  cd "${source_dir}"
}

case "${1:-top}" in
  top|labtop)
    exec /usr/bin/labtop
    ;;
  doctor)
    require_source_dir
    exec ./scripts/doctor.sh
    ;;
  load)
    require_source_dir
    make kernel_proc_lab.ko >/dev/null
    exec ./scripts/load-module.sh
    ;;
  unload|stop)
    require_source_dir
    exec ./scripts/unload-module.sh
    ;;
  reload)
    require_source_dir
    make kernel_proc_lab.ko >/dev/null
    exec ./scripts/reload-module.sh
    ;;
  status)
    require_source_dir
    lsmod | grep kernel_proc_lab || true
    if [[ -e /proc/kernel_proc_lab ]]; then
      cat /proc/kernel_proc_lab
    fi
    if [[ -e /dev/kernel_proc_lab ]]; then
      ls -l /dev/kernel_proc_lab
    fi
    ;;
  device-node|fix-perms)
    require_source_dir
    exec ./scripts/ensure-device-node.sh
    ;;
  collector)
    shift
    exec "${lib_dir}/kernel-lab-collector" "$@"
    ;;
  usercli)
    shift
    exec "${lib_dir}/usercli" "$@"
    ;;
  help|-h|--help)
    cat <<'EOF'
usage: kernel-lab [top|doctor|load|reload|unload|status|device-node|collector|usercli]

commands:
  top          start labtop
  doctor      inspect environment and module state
  load        build and load kernel_proc_lab
  reload      rebuild and reload kernel_proc_lab
  unload      unload kernel_proc_lab
  status      print module, proc, and device state
  device-node create or repair /dev/kernel_proc_lab
  collector   run the JSONL collector
  usercli     pass remaining args to usercli
EOF
    ;;
  *)
    echo "unknown command: $1" >&2
    echo "run: kernel-lab help" >&2
    exit 2
    ;;
esac
