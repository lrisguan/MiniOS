# Lrix
# Copyright (C) 2025 lrisguan <lrisguan@outlook.com>
# 
# This program is released under the terms of the GNU General Public License version 2(GPLv2).
# See https://opensource.org/licenses/GPL-2.0 for more information.
# 
# Project homepage: https://github.com/lrisguan/Lrix
# Description: A scratch implemention of OS based on RISC-V

#!/bin/bash
# This script runs the os.

echo "checking options..."

if [ "$1" == "--help" ]; then
    echo "Usage: ./run.sh [options]"
    echo "Options:"
    echo "  --help       Show this help message"
    echo "  --version    Show version information"
    echo "  --VIRTIO     select virtio version. 1 or 2"
    echo "               1 for virtio legacy, 2 for virtio modern"
    echo "  --FS_DEBUG   Enable filesystem debug mode"
    echo "               1 to enable, 0 to disable"
    echo "  --TRAP_DEBUG Enable trap debug mode"
    echo "               1 to enable, 0 to disable"
    exit 0
fi

virtio=""
fs_debug=""
recreate_disk=""
trap_debug=""

echo "checking virtio option..."
echo "1 for virtio legacy, 2 for virtio modern"
read -p "Select virtio version (1 or 2): " virtio
if [ "$virtio" != "1" ] && [ "$virtio" != "2" ]; then
    echo "Invalid virtio option. Please select 1 or 2."
    exit 1
fi  

echo "checking FS_DEBUG option..."
read -p "Enable filesystem debug mode? (1 to enable, 0 to disable): " fs_debug
if [ "$fs_debug" != "0" ] && [ "$fs_debug" != "1" ]; then
    echo "Invalid FS_DEBUG option. Please select 0 or 1."
    exit 1
fi

echo "checking TRAP_DEBUG option..."
read -p "Enable trap debug mode? (1 to enable, 0 to disable): " trap_debug
if [ "$trap_debug" != "0" ] && [ "$trap_debug" != "1" ]; then
    echo "Invalid FS_DEBUG option. Please select 0 or 1."
    exit 1
fi

echo "checking whether created disk.img"
if [ ! -f "kernel/disk.img" ]; then
    echo "disk.img not found! Auto generating disk.img..."
    dd if=/dev/zero of=kernel/disk.img bs=1k count=64
    else
    echo "disk.img found."
    echo "whether to recreate disk.img? (y to recreate, n to skip)"
    read -p "(y/n): " recreate_disk
    if [ "$recreate_disk" == "y" ]; then
        echo "Recreating disk.img..."
        rm -f kernel/disk.img
        dd if=/dev/zero of=kernel/disk.img bs=1k count=64
        echo "disk.img recreated."
    else
        echo "Skipping disk.img recreation."
    fi
fi

echo "All options set."

echo "Starting the OS with virtio version $virtio, FS_DEBUG set to $fs_debug and TRAP_DEBUG set to $trap_debug..."

make clean

make VIRTIO=$virtio FS_DEBUG=$fs_debug  TRAP_DEBUG=$trap_debug run
