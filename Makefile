
BUILD := build
ISO_ROOT := $(BUILD)/iso
CC := gcc
AS := as
LD := ld

SETTINGS_IMAGE := $(HOME)/.local/share/nexus-os-genesis/settings.img

CFLAGS := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
          -fno-stack-protector -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-unwind-tables
ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

.PHONY: all clean run check-tools

all: $(BUILD)/nexus-os.iso

check-tools:
	@command -v $(CC) >/dev/null || (echo "Missing gcc"; exit 1)
	@command -v $(LD) >/dev/null || (echo "Missing ld"; exit 1)
	@command -v grub-mkrescue >/dev/null || (echo "Missing grub-mkrescue (install grub-pc-bin and xorriso)"; exit 1)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot.s | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.o: kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(BUILD)/boot.o $(BUILD)/kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/boot.o $(BUILD)/kernel.o
	@grub-file --is-x86-multiboot $@ || (echo "Kernel is missing a valid Multiboot header"; exit 1)

$(BUILD)/nexus-os.iso: $(BUILD)/kernel.elf grub.cfg
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(BUILD)/kernel.elf $(ISO_ROOT)/boot/kernel.elf
	cp grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_ROOT)

$(SETTINGS_IMAGE):
	mkdir -p "$(dir $(SETTINGS_IMAGE))"
	dd if=/dev/zero of="$@" bs=1M count=1 status=none

run: $(BUILD)/nexus-os.iso $(SETTINGS_IMAGE)
	qemu-system-i386 \
		-drive file="$(SETTINGS_IMAGE)",format=raw,if=ide,index=0 \
		-cdrom $(BUILD)/nexus-os.iso \
		-m 128M

clean:
	rm -rf $(BUILD)
