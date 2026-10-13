#!/bin/bash
# Install apt packages on a GitHub-hosted Ubuntu runner.
# The image's default mirrors (azure.archive, archive.ubuntu.com) stall often enough
# to burn a whole job, so pin short timeouts and a live mirror list first.
set -euo pipefail

if [[ $# -eq 0 ]]; then
  echo "usage: apt-install.sh package [package...]" >&2
  exit 2
fi

export DEBIAN_FRONTEND=noninteractive
sudo rm -f /etc/apt/sources.list.d/*microsoft* /etc/apt/sources.list.d/*azure-cli*
printf '%s\n' \
  'Acquire::http::Timeout "10";' \
  'Acquire::https::Timeout "10";' \
  'Acquire::Retries "2";' \
  'APT::Acquire::Retries "2";' \
  | sudo tee /etc/apt/apt.conf.d/99-ci-timeouts >/dev/null
{
  curl -fsSL --max-time 8 http://mirrors.ubuntu.com/mirrors.txt || true
  echo "http://us.archive.ubuntu.com/ubuntu/"
  echo "http://security.ubuntu.com/ubuntu/"
} | sed 's:/*$:/:' \
  | grep -vi 'azure.archive.ubuntu.com' \
  | grep -viE '^https?://archive\.ubuntu\.com/' \
  | awk 'NF && !seen[$0]++' \
  | awk '{ printf "%s\tpriority:%d\n", $0, NR }' \
  | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
sudo apt-get update -o Acquire::Retries=3
sudo apt-get install -y -o Acquire::Retries=3 "$@"
