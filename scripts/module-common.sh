#!/usr/bin/env bash

module_name="kernel_proc_lab"
module_file="kernel_proc_lab.ko"
proc_path="/proc/kernel_proc_lab"
device_path="/dev/kernel_proc_lab"
sysfs_dev_path="/sys/class/kernel_proc_lab/kernel_proc_lab/dev"
module_args="${MODULE_ARGS:-}"

is_module_loaded() {
  awk -v name="${module_name}" '$1 == name { found = 1 } END { exit !found }' /proc/modules
}

fix_device_node_permissions() {
  local node_group

  if [[ ! -e "${device_path}" ]]; then
    return 0
  fi

  if [[ -r "${device_path}" && -w "${device_path}" ]]; then
    return 0
  fi

  echo "== fixing device node permissions =="
  node_group="$(id -gn)"
  sudo chown "root:${node_group}" "${device_path}" || true
  sudo chmod 660 "${device_path}"
}

ensure_device_node() {
  local dev_number
  local major
  local minor
  local node_group

  if [[ -e "${device_path}" ]]; then
    fix_device_node_permissions
    return 0
  fi

  if [[ -r "${sysfs_dev_path}" ]]; then
    dev_number="$(cat "${sysfs_dev_path}")"
    major="${dev_number%%:*}"
    minor="${dev_number##*:}"
  else
    major="$(awk -v name="${module_name}" '$2 == name { print $1 }' /proc/devices)"
    minor="0"
  fi

  if [[ -z "${major}" ]]; then
    return 0
  fi

  echo "== creating missing device node =="
  sudo mknod "${device_path}" c "${major}" "${minor}"
  node_group="$(id -gn)"
  sudo chown "root:${node_group}" "${device_path}" || true
  sudo chmod 660 "${device_path}"
  fix_device_node_permissions
}

wait_for_module_paths() {
  for _ in {1..30}; do
    if [[ -e "${proc_path}" ]]; then
      ensure_device_node
      [[ -e "${device_path}" ]] && return 0
    fi
    sleep 0.1
  done

  return 1
}

unload_module_if_loaded() {
  local unload_output

  if is_module_loaded; then
    echo "== unloading module =="
    if ! unload_output="$(sudo rmmod "${module_name}" 2>&1)"; then
      echo "${unload_output}"
      if grep -qi "in use" <<<"${unload_output}"; then
        cat <<EOF

The module is still in use. Close any running labtop/usercli stream/trace
sessions, then check holders with:

  sudo fuser -v ${device_path} ${proc_path}
  lsmod | grep ${module_name}

If a process is listed, stop it and retry:

  make reload

EOF
      fi
      return 1
    fi
  fi
}

load_module() {
  local load_output

  if is_module_loaded; then
    echo "== module already loaded =="
    ensure_device_node
    return 0
  fi

  echo "== loading module =="
  if [[ -n "${module_args}" ]]; then
    echo "== module args: ${module_args} =="
  fi
  # shellcheck disable=SC2086
  if ! load_output="$(sudo insmod "${module_file}" ${module_args} 2>&1)"; then
    echo "${load_output}"
    if grep -qiE "key was rejected|required key not available" <<<"${load_output}"; then
      cat <<'EOF'

Secure Boot blocked this unsigned kernel module.

Choose one:
  1. Disable Secure Boot in BIOS/UEFI, then run ./demo.sh again.
  2. Sign the module and enroll a MOK key:
       ./scripts/create-mok-key.sh
       sudo mokutil --import certs/MOK.der
       reboot
       ./scripts/sign-module.sh
       ./demo.sh

During reboot, the blue MOK manager screen will ask you to enroll the key.
EOF
    fi
    return 1
  fi

  wait_for_module_paths
}
