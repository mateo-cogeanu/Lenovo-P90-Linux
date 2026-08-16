#!/bin/sh
set -eu

if [ "$(uname -m)" != "x86_64" ]; then
    echo "Run this script in an x86_64 Debian environment." >&2
    exit 1
fi

rootfs_archive=${1:-work/p90-native-port/artifacts/debian-bookworm-phosh-amd64.tar.gz}
output_dir=${2:-work/p90-native-port/package-bundles/xorg-fbdev-bookworm}
temporary_dir=$(mktemp -d /tmp/p90-xorg-fbdev-bundle.XXXXXX)
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

mkdir -p "$output_dir" "$temporary_dir/lists/partial" \
    "$temporary_dir/archives/partial"

tar -xOzf "$rootfs_archive" ./var/lib/dpkg/status > "$temporary_dir/status"

cat > "$temporary_dir/sources.list" <<'EOF'
deb https://deb.debian.org/debian bookworm main
deb https://deb.debian.org/debian-security bookworm-security main
deb https://deb.debian.org/debian bookworm-updates main
EOF

apt_options="
    -o APT::Architecture=amd64
    -o APT::Sandbox::User=root
    -o Dir::Etc::sourcelist=$temporary_dir/sources.list
    -o Dir::Etc::sourceparts=-
    -o Dir::State::status=$temporary_dir/status
    -o Dir::State::lists=$temporary_dir/lists
    -o Dir::Cache::archives=$output_dir
    -o Dir::Cache::pkgcache=$temporary_dir/pkgcache.bin
    -o Dir::Cache::srcpkgcache=$temporary_dir/srcpkgcache.bin
    -o APT::Get::List-Cleanup=0
"

# The supplied dpkg status file lets APT download only dependencies absent from
# the exact Debian rootfs already installed on the phone.
# shellcheck disable=SC2086
apt-get $apt_options update
# shellcheck disable=SC2086
apt-get $apt_options --yes --download-only --no-install-recommends install \
    xserver-xorg-core xserver-xorg-video-fbdev xserver-xorg-input-libinput \
    xinit xauth x11-xserver-utils

(cd "$output_dir" && sha256sum ./*.deb > SHA256SUMS)
echo "Package bundle written to $output_dir"
