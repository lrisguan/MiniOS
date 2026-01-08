# Lrix
# Copyright (C) 2025 lrisguan <lrisguan@outlook.com>
# 
# This program is released under the terms of the GNU General Public License version 2(GPLv2).
# See https://opensource.org/licenses/GPL-2.0 for more information.
# 
# Project homepage: https://github.com/lrisguan/Lrix
# Description: A scratch implemention of OS based on RISC-V

KDIR := kernel
UDIR := usr

KIMG := $(KDIR)/build/kernel.bin

.PHONY: all kernel clean run info

# Default: build kernel (will also compile user programs in usr directory)
all: kernel

# Actual build logic for kernel and user programs lives in kernel/Makefile
# We do NOT force-pass VIRTIO here, so that kernel/Makefile can fall back to
# its own logic (including using build/.virtio from the last build). Command
# line variables like VIRTIO/FS_DEBUG are automatically propagated to sub-makes.
kernel:
	$(MAKE) -C $(KDIR) all

clean:
	$(MAKE) -C $(KDIR) clean

# Start the whole OS (QEMU) from the project root
run: kernel
	$(MAKE) -C $(KDIR) run

# Show build information and usage
info:
	@echo "==== Lrix Build Info (top-level) ===="
	@echo "---------------------------------------------------------------------------------"
	@echo
	@echo "KDIR       = $(KDIR)"
	@echo "UDIR       = $(UDIR)"
	@echo "KIMG       = $(KIMG)"
	@echo "FS_DEBUG   = $(FS_DEBUG)  # 0: disable fs debug logs, 1: enable"
	@echo "VIRTIO     = $(VIRTIO)    # 1: legacy, 2: modern, others: auto"
	@echo "TRAP_DEBUG = $(TRAP_DEBUG)  # 0: disable trap debug logs, 1: enable"
	@echo
	@echo "---------------------------------------------------------------------------------"
	@echo
	@echo "Common commands:"
	@echo "  make              # equivalent to 'make kernel', build kernel + user programs"
	@echo "  make run          # build and start QEMU with the OS image"
	@echo "  make clean        # clean kernel build outputs"
	@echo "  make info         # show this help and kernel sub-Makefile info"
	@echo
	@echo "---------------------------------------------------------------------------------"
	@echo
	@echo "Examples:"
	@echo "  make FS_DEBUG=1 VIRTIO=1 run  # enable fs debug logs, use legacy virtio"
	@echo "  make FS_DEBUG=0 VIRTIO=2 run  # disable fs debug logs, use modern virtio"
	@echo "  make TRAP_DEBUG=1 run"        # enable trap debug logs (not recommand to enable)"
	@echo
	@echo "---------------------------------------------------------------------------------"
	@echo "=== kernel/Makefile info ==="
	@echo
	$(MAKE) -C $(KDIR) info

