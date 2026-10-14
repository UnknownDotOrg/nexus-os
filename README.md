# NEXUS OS — Project Genesis (0.4)

A tiny, genuinely bootable hobby OS kernel. It boots through GRUB in a virtual machine and writes directly to VGA text memory. This first milestone targets **32-bit x86 protected mode** so the boot path stays small and understandable. We can move to x86-64 in a later milestone.

## Files

- `boot.s` — Multiboot v1 header and kernel entry/stack.
- `kernel.c` — freestanding C kernel and VGA text output.
- `linker.ld` — places the kernel where GRUB can load it.
- `grub.cfg` — GRUB menu entry.
- `Makefile` — builds the kernel and bootable ISO.

## Build on Kubuntu

Install the required build tools (this uses system packages, so ask the computer's administrator before running it):

```bash
sudo apt update
sudo apt install build-essential gcc-multilib grub-pc-bin xorriso qemu-system-x86
```

Then, from this project directory:

```bash
make
```

The ISO will be `build/nexus-os.iso`.

Run it in QEMU:

```bash
make run
```

Or open VirtualBox, create a new **32-bit Other/Unknown** virtual machine, attach `build/nexus-os.iso` as its optical disk, and start it. If VirtualBox does not offer 32-bit guests, QEMU is the easier first test.

## What success looks like

A black screen with cyan `NEXUS OS` and a message saying Project Genesis booted successfully.

## Important notes

- This is a real freestanding kernel, not a program running on Linux.
- The first version is deliberately 32-bit x86. It is not yet an x86-64 kernel.
- The ISO is a live boot image; it does not install itself to a hard drive.
- The build process only writes files inside this project directory.
- Do not run `make install`; no install target is provided.
