
#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define INPUT_SIZE 128

#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define COLOR 0x0B

enum {
    LAYOUT_QWERTY = 0,
    LAYOUT_AZERTY = 1
};

static size_t cursor_row = 0;
static size_t cursor_col = 0;

static int keyboard_layout = LAYOUT_QWERTY;
static int shift_pressed = 0;
static int caps_lock = 0;

/* --------------------------------------------------
   Low-level I/O
   -------------------------------------------------- */

static uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint16_t inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

/* --------------------------------------------------
   VGA text output
   -------------------------------------------------- */

static uint16_t vga_entry(char ch)
{
    return (uint16_t)(uint8_t)ch
        | ((uint16_t)COLOR << 8);
}

static void clear_screen(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = vga_entry(' ');
    }

    cursor_row = 0;
    cursor_col = 0;
}

static void scroll_screen(void)
{
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ');
    }

    cursor_row = VGA_HEIGHT - 1;
}

static void put_char(char ch)
{
    if (ch == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else {
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
            vga_entry(ch);

        cursor_col++;

        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    if (cursor_row >= VGA_HEIGHT) {
        scroll_screen();
    }
}

static void print(const char *text)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        put_char(text[i]);
    }
}

static void print_uint(uint32_t value)
{
    char digits[10];
    size_t length = 0;

    if (value == 0) {
        put_char('0');
        return;
    }

    while (value > 0 && length < sizeof(digits)) {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (length > 0) {
        put_char(digits[--length]);
    }
}

static int strings_equal(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }

        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static int starts_with(const char *text, const char *prefix)
{
    size_t i = 0;

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }

        i++;
    }

    return 1;
}

/* --------------------------------------------------
   ATA PIO: dedicated settings disk
   -------------------------------------------------- */

/*
 * QEMU attaches a separate writable IDE disk as primary
 * master. NEXUS stores its keyboard settings in sector 0.
 *
 * Never point this driver at a real host disk.
 */

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALTSTATUS  0x3F6

#define ATA_BSY 0x80
#define ATA_DRQ 0x08
#define ATA_ERR 0x01

static void ata_delay(void)
{
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS);
}

static int ata_wait_ready(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0 || status == 0xFF) {
            return 0;
        }

        if (!(status & ATA_BSY)) {
            return !(status & ATA_ERR);
        }
    }

    return 0;
}

static int ata_wait_data(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status == 0 || status == 0xFF) {
            return 0;
        }

        if (status & ATA_ERR) {
            return 0;
        }

        if (!(status & ATA_BSY) && (status & ATA_DRQ)) {
            return 1;
        }
    }

    return 0;
}

static void ata_select_sector(uint32_t lba)
{
    outb(ATA_DRIVE,
         (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));

    ata_delay();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
}

static int ata_read_sector(uint32_t lba, uint16_t *buffer)
{
    if (!ata_wait_ready()) {
        return 0;
    }

    ata_select_sector(lba);
    outb(ATA_COMMAND, 0x20);

    if (!ata_wait_data()) {
        return 0;
    }

    /* ATA transfers 256 16-bit words per sector. */
    for (size_t i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_DATA);
    }

    return ata_wait_ready();
}

static int ata_write_sector(uint32_t lba, const uint16_t *buffer)
{
    if (!ata_wait_ready()) {
        return 0;
    }

    ata_select_sector(lba);
    outb(ATA_COMMAND, 0x30);

    if (!ata_wait_data()) {
        return 0;
    }

    for (size_t i = 0; i < 256; i++) {
        outw(ATA_DATA, buffer[i]);
    }

    if (!ata_wait_ready()) {
        return 0;
    }

    /* Flush the disk's write cache. */
    outb(ATA_COMMAND, 0xE7);

    return ata_wait_ready();
}

static int valid_settings(const uint16_t *sector)
{
    const uint8_t *bytes = (const uint8_t *)sector;

    return bytes[0] == 'N'
        && bytes[1] == 'E'
        && bytes[2] == 'X'
        && bytes[3] == 'U'
        && bytes[4] == 'S'
        && bytes[5] == 'C'
        && bytes[6] == 'F'
        && bytes[7] == 'G'
        && bytes[8] == 1
        && (bytes[9] == LAYOUT_QWERTY
            || bytes[9] == LAYOUT_AZERTY)
        && bytes[10] == (uint8_t)(0xA5 ^ bytes[8] ^ bytes[9]);
}

/*
 * Returns:
 *   1 = valid settings restored
 *   0 = disk available, but no valid settings found
 *  -1 = disk unavailable or read failed
 */
static int load_keyboard_layout(void)
{
    uint16_t sector[256];

    if (!ata_read_sector(0, sector)) {
        return -1;
    }

    if (!valid_settings(sector)) {
        return 0;
    }

    const uint8_t *bytes = (const uint8_t *)sector;
    keyboard_layout = bytes[9];

    return 1;
}

static int save_keyboard_layout(void)
{
    uint16_t sector[256] = {0};
    uint16_t verify[256];

    uint8_t *bytes = (uint8_t *)sector;

    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'X';
    bytes[3] = 'U';
    bytes[4] = 'S';
    bytes[5] = 'C';
    bytes[6] = 'F';
    bytes[7] = 'G';

    bytes[8] = 1;
    bytes[9] = (uint8_t)keyboard_layout;
    bytes[10] = (uint8_t)(0xA5 ^ bytes[8] ^ bytes[9]);

    if (!ata_write_sector(0, sector)) {
        return 0;
    }

    /* Read the sector back to verify the saved setting. */
    if (!ata_read_sector(0, verify)) {
        return 0;
    }

    if (!valid_settings(verify)) {
        return 0;
    }

    const uint8_t *check = (const uint8_t *)verify;

    return check[9] == (uint8_t)keyboard_layout;
}

/* --------------------------------------------------
   Keyboard layout mapping
   -------------------------------------------------- */

static char translate_scancode(uint8_t scancode)
{
    char ch = 0;

    switch (scancode) {
        /* Number row: basic ASCII mapping for now. */
        case 0x02: ch = '1'; break;
        case 0x03: ch = '2'; break;
        case 0x04: ch = '3'; break;
        case 0x05: ch = '4'; break;
        case 0x06: ch = '5'; break;
        case 0x07: ch = '6'; break;
        case 0x08: ch = '7'; break;
        case 0x09: ch = '8'; break;
        case 0x0A: ch = '9'; break;
        case 0x0B: ch = '0'; break;

        /* Top letter row. */
        case 0x10:
            ch = keyboard_layout == LAYOUT_AZERTY ? 'a' : 'q';
            break;
        case 0x11:
            ch = keyboard_layout == LAYOUT_AZERTY ? 'z' : 'w';
            break;
        case 0x12: ch = 'e'; break;
        case 0x13: ch = 'r'; break;
        case 0x14: ch = 't'; break;
        case 0x15: ch = 'y'; break;
        case 0x16: ch = 'u'; break;
        case 0x17: ch = 'i'; break;
        case 0x18: ch = 'o'; break;
        case 0x19: ch = 'p'; break;

        /* Home row. */
        case 0x1E:
            ch = keyboard_layout == LAYOUT_AZERTY ? 'q' : 'a';
            break;
        case 0x1F: ch = 's'; break;
        case 0x20: ch = 'd'; break;
        case 0x21: ch = 'f'; break;
        case 0x22: ch = 'g'; break;
        case 0x23: ch = 'h'; break;
        case 0x24: ch = 'j'; break;
        case 0x25: ch = 'k'; break;
        case 0x26: ch = 'l'; break;
        case 0x27:
            ch = keyboard_layout == LAYOUT_AZERTY ? 'm' : ';';
            break;

        /* Bottom letter row. */
        case 0x2C:
            ch = keyboard_layout == LAYOUT_AZERTY ? 'w' : 'z';
            break;
        case 0x2D: ch = 'x'; break;
        case 0x2E: ch = 'c'; break;
        case 0x2F: ch = 'v'; break;
        case 0x30: ch = 'b'; break;
        case 0x31: ch = 'n'; break;
        case 0x32:
            ch = keyboard_layout == LAYOUT_AZERTY ? ',' : 'm';
            break;

        /* Punctuation. */
        case 0x0C: ch = '-'; break;
        case 0x0D: ch = '='; break;
        case 0x1A: ch = '['; break;
        case 0x1B: ch = ']'; break;
        case 0x28: ch = '\''; break;
        case 0x29: ch = '`'; break;
        case 0x2B: ch = '\\'; break;
        case 0x33: ch = ','; break;
        case 0x34: ch = '.'; break;
        case 0x35: ch = '/'; break;
        case 0x39: ch = ' '; break;

        default:
            return 0;
    }

    if (ch >= 'a' && ch <= 'z') {
        if (shift_pressed ^ caps_lock) {
            ch = (char)(ch - 'a' + 'A');
        }

        return ch;
    }

    if (shift_pressed) {
        switch (ch) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '4': return '$';
            case '5': return '%';
            case '6': return '^';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
            case '-': return '_';
            case '=': return '+';
            case '[': return '{';
            case ']': return '}';
            case ';': return ':';
            case '\'': return '"';
            case '`': return '~';
            case '\\': return '|';
            case ',': return '<';
            case '.': return '>';
            case '/': return '?';
            default: break;
        }
    }

    return ch;
}

static char read_char(void)
{
    for (;;) {
        if ((inb(0x64) & 1) == 0) {
            continue;
        }

        uint8_t scancode = inb(0x60);

        /* Key release events. */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;

            if (released == 0x2A || released == 0x36) {
                shift_pressed = 0;
            }

            continue;
        }

        /* Shift. */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }

        /* Caps Lock. */
        if (scancode == 0x3A) {
            caps_lock = !caps_lock;
            continue;
        }

        if (scancode == 0x1C) {
            return '\n';
        }

        if (scancode == 0x0E) {
            return '\b';
        }

        char ch = translate_scancode(scancode);

        if (ch != 0) {
            return ch;
        }
    }
}

/* --------------------------------------------------
   Shell input
   -------------------------------------------------- */

static void erase_character(void)
{
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    } else {
        return;
    }

    VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] =
        vga_entry(' ');
}

static void read_line(char *buffer, size_t capacity)
{
    size_t length = 0;

    if (capacity == 0) {
        return;
    }

    for (;;) {
        char ch = read_char();

        if (ch == '\n') {
            buffer[length] = '\0';
            put_char('\n');
            return;
        }

        if (ch == '\b') {
            if (length > 0) {
                length--;
                erase_character();
            }

            continue;
        }

        if (ch >= 32 && ch <= 126 && length < capacity - 1) {
            buffer[length++] = ch;
            put_char(ch);
        }
    }
}

/* --------------------------------------------------
   Keyboard settings command
   -------------------------------------------------- */

static void keyboard_settings(void)
{
    char choice[INPUT_SIZE];

    for (;;) {
        print("\nKEYBOARD SETTINGS\n");
        print("-----------------\n");
        print("Choose your keyboard setup:\n");
        print("1) QWERTY\n");
        print("2) AZERTY\n\n");
        print("> ");

        read_line(choice, sizeof(choice));

        if (strings_equal(choice, "1")) {
            keyboard_layout = LAYOUT_QWERTY;
        } else if (strings_equal(choice, "2")) {
            keyboard_layout = LAYOUT_AZERTY;
        } else {
            print("Invalid choice. Enter 1 or 2.\n\n");
            continue;
        }

        print("Keyboard layout changed to ");
        print(keyboard_layout == LAYOUT_AZERTY ? "AZERTY" : "QWERTY");
        put_char('\n');

        if (save_keyboard_layout()) {
            print("Settings saved and verified on the virtual disk.\n");
        } else {
            print("ERROR: Could not verify settings on the virtual disk.\n");
        }

        return;
    }
}

/* --------------------------------------------------
   NEXUS OS shell
   -------------------------------------------------- */

static void shell(void)
{
    char command[INPUT_SIZE];

    for (;;) {
        print("nexus> ");
        read_line(command, sizeof(command));

        if (strings_equal(command, "help")) {
            print("Commands: help, about, version, uptime, echo, clear, keyset\n");
        } else if (strings_equal(command, "about")) {
            print("NEXUS OS - Project Genesis\n");
            print("A freestanding C kernel for x86.\n");
            print("Built to explore how operating systems work.\n");
        } else if (strings_equal(command, "version")) {
            print("NEXUS OS Project Genesis 0.4\n");
        } else if (strings_equal(command, "uptime")) {
            print("Uptime is temporarily unavailable while the timer is being debugged.\n");
        } else if (strings_equal(command, "echo")) {
            put_char('\n');
        } else if (starts_with(command, "echo ")) {
            print(command + 5);
            put_char('\n');
        } else if (strings_equal(command, "clear")) {
            clear_screen();
        } else if (strings_equal(command, "keyset") ||
                   strings_equal(command, "keysett")) {
            keyboard_settings();
        } else if (command[0] != '\0') {
            print("Unknown command. Type help.\n");
        }
    }
}

/* --------------------------------------------------
   Kernel entry point
   -------------------------------------------------- */

void kernel_main(void)
{
    __asm__ volatile ("cli");

    clear_screen();

    /*
     * Timer initialization remains disabled because it
     * caused a reboot loop. We will debug it separately.
     */

    int settings_status = load_keyboard_layout();

    print("NEXUS OS\n");
    print("Project Genesis | Version 0.4\n");
    print("-----------------------------------\n\n");
    print("Welcome to NEXUS OS!\n");

    if (settings_status == 1) {
        print("Restored keyboard layout: ");
        print(keyboard_layout == LAYOUT_AZERTY ? "AZERTY\n" : "QWERTY\n");
    } else if (settings_status == 0) {
        print("No valid saved settings found; using QWERTY.\n");
        print("Use keyset to choose a layout.\n");
    } else {
        print("Settings disk unavailable; using QWERTY.\n");
        print("Check the QEMU settings disk configuration.\n");
    }

    print("Type help for available commands.\n\n");

    shell();

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
