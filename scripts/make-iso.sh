#!/usr/bin/env bash
set -euo pipefail

root_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$root_dir/build"
limine_version="v12.9.3"
archive="$build_dir/limine-binary.tar.gz"
archive_tmp="$build_dir/limine-binary.tar.gz.tmp.$$"
limine_dir="$build_dir/limine-binary"
iso_root="$build_dir/iso-root"
iso_path="$build_dir/devos.iso"
source_date_epoch="${SOURCE_DATE_EPOCH:-$(git -C "$root_dir" log -1 --format=%ct)}"

if [[ ! "$source_date_epoch" =~ ^[0-9]+$ ]]; then
    printf 'error: SOURCE_DATE_EPOCH must be a Unix timestamp\n' >&2
    exit 1
fi

trap 'rm -f "$archive_tmp"' EXIT

for tool in curl tar make xorriso; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'error: required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

mkdir -p "$build_dir"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 3 \
        "https://github.com/Limine-Bootloader/Limine/releases/download/${limine_version}/limine-binary.tar.gz" \
        --output "$archive_tmp"
    mv "$archive_tmp" "$archive"
fi

if [[ ! -x "$limine_dir/limine" ]]; then
    mkdir -p "$limine_dir"
    tar -xzf "$archive" --strip-components=1 -C "$limine_dir"
    make -C "$limine_dir" CC="${HOST_CC:-gcc}"
fi

rm -rf "$iso_root"
mkdir -p "$iso_root/boot" "$iso_root/EFI/BOOT"
cp "$build_dir/devos-kernel" "$iso_root/boot/devos-kernel"
cp "$root_dir/boot/limine.conf" "$iso_root/limine.conf"
cp "$limine_dir/limine-bios.sys" "$iso_root/boot/limine-bios.sys"
cp "$limine_dir/limine-bios-cd.bin" "$iso_root/boot/limine-bios-cd.bin"
cp "$limine_dir/limine-uefi-cd.bin" "$iso_root/boot/limine-uefi-cd.bin"
cp "$limine_dir/BOOTX64.EFI" "$iso_root/EFI/BOOT/BOOTX64.EFI"

find "$iso_root" -exec touch -h -d "@$source_date_epoch" {} +
iso_modification_date="$(
    date -u -d "@$source_date_epoch" '+%Y%m%d%H%M%S00'
)"
xorriso -as mkisofs \
    -R -r -J \
    --modification-date="$iso_modification_date" \
    --set_all_file_dates "$iso_modification_date" \
    --gpt_disk_guid 4445564f-532d-4f53-8000-000000000001 \
    -b boot/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
    -apm-block-size 2048 \
    --efi-boot boot/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$iso_root" -o "$iso_path"
"$limine_dir/limine" bios-install "$iso_path"

printf 'Created bootable ISO: %s\n' "$iso_path"
