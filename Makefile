# Cortex-A-OS — Top-level Makefile
#
# Usage:
#   make                  → build for QEMU (default)
#   make PLATFORM=bbb     → build for BeagleBone Black
#   make qemu             → build + run on QEMU
#   make clean            → clean current platform
#   make clean-all        → clean all platforms
#
# All output goes to build/

PLATFORM ?= qemu

# Toolchain
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
AS      := $(CROSS)as
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy

# Build directories
BUILD_DIR    := build/$(PLATFORM)
OBJ_DIR      := build/obj/$(PLATFORM)
USER_DIR     := build/user
PLATFORM_DIR := kernel/platform/$(PLATFORM)

# Linker scripts
LDSCRIPT      := kernel/linker/kernel_$(PLATFORM).ld
USER_LDSCRIPT := user/linker/user.ld

# ----- Kernel flags -----
# -I $(PLATFORM_DIR) picks up the active board.h; no -DPLATFORM_*
# macros are needed because platform selection is done at the file
# level (kernel/platform/<p>/ is linked in, the other folder is not).
CFLAGS := -nostdlib -ffreestanding -nostartfiles \
          -mcpu=cortex-a8 -marm \
          -I kernel/include \
          -I $(PLATFORM_DIR) \
          -Wall -Wextra -g

# ----- User flags -----
# Never add -fpie / -fpic: user programs are position-dependent,
# linked to absolute VA 0x40000000 in user.ld.
UCFLAGS := -nostdlib -ffreestanding -nostartfiles \
           -mcpu=cortex-a8 -marm \
           -I user/libc \
           -Wall -Wextra -g

# ------------------------------------------------------------
# Kernel sources
# ------------------------------------------------------------
# Each platform lists its chip drivers in kernel/platform/<p>/platform.mk.
# That file defines PLATFORM_DRIVERS, which feeds into C_SRCS below.
-include $(PLATFORM_DIR)/platform.mk

DRIVER_CORES := kernel/drivers/uart/uart_core.c \
                kernel/drivers/timer/timer_core.c \
                kernel/drivers/intc/intc_core.c

C_SRCS := kernel/main.c \
          kernel/proc/process.c \
          kernel/proc/thread.c \
          kernel/sched/scheduler.c \
          kernel/syscall/syscall.c \
          kernel/arch/arm/exception/exception_handlers.c \
          kernel/arch/arm/mm/mmu.c \
          kernel/arch/arm/mm/pgtable.c \
          $(PLATFORM_DIR)/board.c \
          $(PLATFORM_DIR)/periph_map.c \
          $(DRIVER_CORES) \
          $(PLATFORM_DRIVERS)

S_SRCS := kernel/arch/arm/boot/start.S \
          kernel/arch/arm/exception/vectors.S \
          kernel/arch/arm/exception/exception_entry.S \
          kernel/arch/arm/mm/mmu_enable.S \
          kernel/arch/arm/proc/context_switch.S \
          kernel/arch/arm/proc/user_binaries.S

C_OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SRCS))
S_OBJS := $(patsubst %.S,$(OBJ_DIR)/%.o,$(S_SRCS))
OBJS   := $(S_OBJS) $(C_OBJS)

# ------------------------------------------------------------
# User programs
#   Each app is: crt0.o + ulib.o + main.o → app.elf → app.bin
# ------------------------------------------------------------
USER_APPS := counter runaway shell
USER_BINS := $(foreach a,$(USER_APPS),$(USER_DIR)/$(a).bin)

USER_LIBC_OBJS := $(USER_DIR)/libc/ulib.o
USER_CRT0_OBJ  := $(USER_DIR)/crt0.o

TARGET_ELF := $(BUILD_DIR)/kernel.elf
TARGET_BIN := $(BUILD_DIR)/kernel.bin

# ------------------------------------------------------------
# BBB-only: SPL bootloader (submodule) → MLO
# ROM code on AM335x reads MLO from FAT, loads into SRAM, runs it.
# MLO then inits DDR + MMC and loads kernel.bin from raw sectors.
# ------------------------------------------------------------
BOOTLOADER_DIR := bootloader
BOOTLOADER_MLO := $(BOOTLOADER_DIR)/build/MLO
MLO_DST        := $(BUILD_DIR)/MLO

ifeq ($(PLATFORM),bbb)
  ALL_TARGETS := $(TARGET_ELF) $(TARGET_BIN) $(MLO_DST)
else
  ALL_TARGETS := $(TARGET_ELF) $(TARGET_BIN)
endif

.DEFAULT_GOAL := all

.PHONY: all clean clean-all qemu user bootloader

all: $(ALL_TARGETS)
	@echo "[OK] $(TARGET_ELF)"

user: $(USER_BINS)

# ------------------------------------------------------------
# Kernel link
# ------------------------------------------------------------
$(TARGET_ELF): $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -T $(LDSCRIPT) -o $@ $(OBJS) -lgcc

$(TARGET_BIN): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@

# user_binaries.S incbin's the user .bin files; make sure they
# exist before that .S is assembled.
$(OBJ_DIR)/kernel/arch/arm/proc/user_binaries.o: $(USER_BINS)

# ------------------------------------------------------------
# Kernel compile rules
# ------------------------------------------------------------
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ------------------------------------------------------------
# User compile rules
# ------------------------------------------------------------
$(USER_DIR)/crt0.o: user/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(USER_DIR)/libc/%.o: user/libc/%.c user/libc/ulib.h user/libc/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

$(USER_DIR)/apps/%.o: user/apps/%.c user/libc/ulib.h user/libc/syscall.h
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c $< -o $@

# For each user app, link its {crt0, ulib, main.o} into an ELF,
# then objcopy to raw binary.
define USER_APP_RULES
$(USER_DIR)/$(1).elf: $(USER_CRT0_OBJ) $(USER_DIR)/apps/$(1)/$(1).o $(USER_LIBC_OBJS) $(USER_LDSCRIPT)
	@mkdir -p $$(dir $$@)
	$(CC) $(UCFLAGS) -T $(USER_LDSCRIPT) -o $$@ \
	      $(USER_CRT0_OBJ) $(USER_DIR)/apps/$(1)/$(1).o $(USER_LIBC_OBJS)

$(USER_DIR)/$(1).bin: $(USER_DIR)/$(1).elf
	$(OBJCOPY) -O binary $$< $$@
endef

$(foreach a,$(USER_APPS),$(eval $(call USER_APP_RULES,$(a))))

# ------------------------------------------------------------
# Bootloader (BBB SPL) — always rebuild, submodule decides if its
# own artifacts are up to date.
# ------------------------------------------------------------
bootloader $(BOOTLOADER_MLO):
	$(MAKE) -C $(BOOTLOADER_DIR)

$(MLO_DST): $(BOOTLOADER_MLO)
	@mkdir -p $(dir $@)
	cp $< $@

# ------------------------------------------------------------
clean:
	rm -rf build/$(PLATFORM) build/obj/$(PLATFORM) build/user
ifeq ($(PLATFORM),bbb)
	$(MAKE) -C $(BOOTLOADER_DIR) clean
endif

clean-all:
	rm -rf build/
	-$(MAKE) -C $(BOOTLOADER_DIR) clean

qemu: all
	@bash scripts/qemu/run.sh
