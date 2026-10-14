
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
   Low-level keyboard input
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

/* --------------------------------------------------
   Keyboard layout mapping
   -------------------------------------------------- */

/*
 * These tables use PS/2 Set 1 scancodes.
 *
 * Both layouts support the same basic ASCII
 * number-row mapping for now. AZERTY changes
 * the letter positions; French accents and
 * AltGr symbols are future improvements.
 */

static char translate_scancode(uint8_t scancode)
{
    char ch = 0;

    switch (scancode) {
        /* Number row: shared ASCII mapping */
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

        /* Top letter row */
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

        /* Home row */
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

        /* Bottom letter row */
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

        /* Common punctuation */
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

    /* Apply Shift and Caps Lock to letters. */
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

/* Wait for and translate one keyboard character. */
static char read_char(void)
{
    for (;;) {
        /* Bit 0 indicates that keyboard data is available. */
        if ((inb(0x64) & 1) == 0) {
            continue;
        }

        uint8_t scancode = inb(0x60);

        /* Key release events have the high bit set. */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;

            if (released == 0x2A || released == 0x36) {
                shift_pressed = 0;
            }

            continue;
        }

        /* Track modifier keys. */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            continue;
        }

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

            print("Changing keyboard layout to QWERTY...\n");
            print("Done!\n");
            return;
        }

        if (strings_equal(choice, "2")) {
            keyboard_layout = LAYOUT_AZERTY;

            print("Changing keyboard layout to AZERTY...\n");
            print("Done!\n");
            return;
        }

        print("Invalid choice. Enter 1 or 2.\n\n");
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
            print("Commands: help, about, clear, keyset\n");
        } else if (strings_equal(command, "about")) {
            print("NEXUS OS - Project Genesis\n");
            print("Version 0.3\n");
            print("A freestanding C kernel.\n");
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
    clear_screen();

    print("NEXUS OS\n");
    print("Project Genesis | Version 0.3\n");
    print("-----------------------------------\n\n");
    print("Welcome to NEXUS OS!\n");
    print("Type help for available commands.\n\n");

    shell();

    /* The shell normally runs forever. */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
