#!/usr/bin/env bash
set -euo pipefail

cert_dir="certs"
mkdir -p "${cert_dir}"

if [[ -e "${cert_dir}/MOK.priv" || -e "${cert_dir}/MOK.der" ]]; then
  echo "MOK key already exists in ${cert_dir}/"
  exit 0
fi

openssl req \
  -new \
  -x509 \
  -newkey rsa:2048 \
  -keyout "${cert_dir}/MOK.priv" \
  -outform DER \
  -out "${cert_dir}/MOK.der" \
  -nodes \
  -days 36500 \
  -subj "/CN=Kernel Proc Lab Module Signing/"

chmod 600 "${cert_dir}/MOK.priv"

echo "created ${cert_dir}/MOK.priv and ${cert_dir}/MOK.der"
echo "next: sudo mokutil --import ${cert_dir}/MOK.der"
