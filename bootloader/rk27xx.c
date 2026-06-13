#include <stdio.h>
#include <inttypes.h>
#include "config.h"
#include "system.h"
#include "../kernel-internal.h"
#include "gcc_extensions.h"
#include "lcd.h"
#include "font.h"
#include "backlight.h"
#include "adc.h"
#include "button-target.h"
#include "button.h"
#include "common.h"
#include "storage.h"
#include "file_internal.h"
#include "disk.h"
#include "panic.h"
#include "power.h"
#include "string.h"
#include "file.h"
#include "crc32-rkw.h"
#include "rkw-loader.h"
#include "version.h"
#include "i2c-rk27xx.h"
#include "loader_strerror.h"
#ifdef COLORFLY_C4_PRO
#include "lcdif-rk27xx.h"
#endif

/* beginning of DRAM */
#define DRAM_ORIG 0x60000000

/* bootloader code runs from 0x60700000
 * so we cannot load more code to not overwrite ourself
 */
#define LOAD_SIZE 0x700000

/* This function setup bare minimum
 * and jumps to rom in order to activate
 * hardcoded rkusb mode
 */
static void enter_rkusb(void)
{
    asm volatile (
        /* Turn off cache */
        "ldr     r0, =0xefff0000 \n"
        "ldrh    r1, [r0] \n"
        "strh    r1, [r0] \n"

        /* Turn off interrupts */
        "mrs     r0, cpsr \n"
        "bic     r0, r0, #0x1f \n"
        "orr     r0, r0, #0xd3 \n"
        "msr     cpsr, r0 \n"

        /* Disable iram remap */
        "mov     r0, #0x18000000 \n"
        "add     r0, r0, #0x1c000 \n"
        "mov     r1, #0 \n"
        "str     r1, [r0, #4] \n"

        /* Ungate all clocks */
        "str     r1, [r0, #0x18] \n"

        /* Read SCU_ID to determine
         * which version of bootrom we have
         * 2706A has ID 0xa1000604
         * 2706B and 2705 have ID 0xa100027b
         */
        "ldr     r1, [r0] \n"
        "ldr     r2, =0xa1000604 \n"
        "cmp     r1, r2 \n"
        "bne     2f \n"

        /* Setup stacks in unmapped
         * iram just as rom will do.
         *
         * We know about two versions
         * of bootrom which are very similar
         * but memory addresses are slightly
         * different.
         */
        "1: \n"  /* rk27xx_old */
        "ldr     r1, =0x18200258 \n"
        "ldr     r0, =0xaf0 \n"
        "b       3f \n"

        "2: \n"  /* rk27xx_new */
        "ldr     r1, =0x18200274 \n"
        "ldr     r0, =0xec0 \n"

        "3: \n"  /* jump to ROM */
        "msr     cpsr, #0xd2 \n"
        "add     r1, r1, #0x200 \n"
        "mov     sp, r1 \n"
        "msr     cpsr, #0xd3 \n"
        "add     r1, r1, #0x400 \n"
        "mov     sp, r1 \n"

        /* Finaly jump to rkusb handler
         * in bootrom.
         */
        "bx      r0 \n"
    );
}

#ifdef COLORFLY_C4_PRO
#define C4PRO_RAM_IMAGE_INFO   ((volatile unsigned long *)0x606fff00)
#define C4PRO_RAM_IMAGE_MAGIC  0x42523443 /* "C4RB" */

static void c4pro_quiesce_usb(void)
{
    EN_INT = 0;
    INTCON = UDC_INTHIGH_ACT;
    DEV_CTL = DEV_SELF_PWR;
    INTC_IMR &= ~IRQ_ARM_UDC;
    INTC_IECR &= ~IRQ_ARM_UDC;
    INTC_ICCR = IRQ_ARM_UDC;
}

static bool c4pro_has_ram_image(void)
{
    return C4PRO_RAM_IMAGE_INFO[0] == C4PRO_RAM_IMAGE_MAGIC &&
           C4PRO_RAM_IMAGE_INFO[1] > 0 &&
           C4PRO_RAM_IMAGE_INFO[1] <= LOAD_SIZE;
}

static unsigned c4pro_get_le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static void c4pro_storage_error_halt(const char *where, int rc) NORETURN_ATTR;
static void c4pro_storage_error_halt(const char *where, int rc)
{
    static unsigned char sector[SECTOR_SIZE];
    struct storage_info info;
    int read_rc;
    int i;

    verbose = true;
    reset_screen();
    printf("C4 storage diag");
    printf("%s rc=%d", where, rc);

    storage_get_info(0, &info);
    printf("sector_size %u", info.sector_size);
    printf("num_sectors %llu", (unsigned long long)info.num_sectors);

    read_rc = storage_read_sectors(IF_MD(0,) 0, 1, sector);
    printf("read LBA0 rc=%d", read_rc);

    if (read_rc == 0)
    {
        printf("sig %02x %02x", sector[510], sector[511]);
        printf("b0 %02x %02x %02x %02x %02x %02x %02x %02x",
               sector[0], sector[1], sector[2], sector[3],
               sector[4], sector[5], sector[6], sector[7]);

        for (i = 0; i < 4; i++)
        {
            const unsigned char *p = sector + 446 + i * 16;
            printf("P%d boot%02x type%02x start%08x size%08x",
                   i, p[0], p[4], c4pro_get_le32(p + 8),
                   c4pro_get_le32(p + 12));
        }
    }

    printf("halt: press reset/CE#");
    lcd_update();

    while (1)
        sleep(HZ);
}

#ifdef C4PRO_BOOTLOADER_NAND_PROBE
static void c4pro_nand_delay(void)
{
    volatile int i;

    for (i = 0; i < 1000; i++)
        ;
}

static bool c4pro_nand_wait_ready(void)
{
    int i;

    for (i = 0; i < 100000; i++)
    {
        if (FMCTL & FM_RDY)
            return true;
    }

    return false;
}

static void c4pro_nand_select(unsigned bank)
{
    unsigned long ctl = FMCTL;

    ctl &= 0xf0;
    ctl |= 1u << bank;
    FMCTL = ctl;
}

static void c4pro_nand_deselect(void)
{
    FMCTL &= 0xf0;
}

static bool c4pro_nand_read_id(unsigned bank, unsigned char id[5])
{
    int i;

    FMWAIT = 0x1081;
    FLCTL = FL_RST;

    c4pro_nand_select(bank);
    FLASH_CMD(bank) = 0xff;
    c4pro_nand_delay();

    if (!c4pro_nand_wait_ready())
    {
        c4pro_nand_deselect();
        return false;
    }

    FLASH_CMD(bank) = 0x90;
    FLASH_ADDR(bank) = 0x00;

    for (i = 0; i < 5; i++)
        id[i] = FLASH_DATA(bank);

    c4pro_nand_deselect();
    return true;
}

static bool c4pro_nand_wait_fl_ready(void)
{
    int i;

    for (i = 0; i < 100000; i++)
    {
        if (FLCTL & FL_RDY)
            return true;
    }

    return false;
}

static bool c4pro_nand_read_hw_sector_prefix(unsigned long page,
                                             unsigned sector_in_page,
                                             unsigned char data[4],
                                             unsigned char spare[4])
{
    unsigned i;

    FMWAIT = 0x1081;
    FLCTL = FL_RST;

    c4pro_nand_select(0);
    c4pro_nand_delay();

    if (!c4pro_nand_wait_ready())
    {
        c4pro_nand_deselect();
        return false;
    }

    FLASH_CMD(0) = 0x00;
    FLASH_ADDR(0) = 0x00;
    FLASH_ADDR(0) = 0x00;
    FLASH_ADDR(0) = page & 0xff;
    FLASH_ADDR(0) = (page >> 8) & 0xff;
    FLASH_ADDR(0) = (page >> 16) & 0xff;
    FLASH_CMD(0) = 0x30;

    c4pro_nand_delay();

    if (!c4pro_nand_wait_ready())
    {
        c4pro_nand_deselect();
        return false;
    }

    BCHCTL = 1;

    for (i = 0; i <= sector_in_page; i++)
    {
        FLCTL = 0xA24 | ((i & 3) << 3);
        if (!c4pro_nand_wait_fl_ready())
        {
            c4pro_nand_deselect();
            return false;
        }
    }

    for (i = 0; i < 4; i++)
    {
        data[i] = *((volatile unsigned char *)&PAGE_BUF +
                    ((sector_in_page & 3) << 9) + i);
        spare[i] = *((volatile unsigned char *)&SPARE_BUF +
                     ((sector_in_page & 3) << 4) + i);
    }

    c4pro_nand_deselect();
    return true;
}

static bool c4pro_nand_read_hw_subsector_psec_prefix(unsigned long psec,
                                                     unsigned char data[4],
                                                     unsigned char spare[4])
{
    return c4pro_nand_read_hw_sector_prefix(psec / 4, psec & 3, data, spare);
}

static char c4pro_nand_classify_prefix(const unsigned char data[4],
                                       const unsigned char spare[4])
{
    int i;
    bool data_ff = true;
    bool spare_ff = true;
    bool data_00 = true;

    if (data[0] == 0x55 && data[1] == 0xaa &&
        data[2] == 0xf0 && data[3] == 0x0f)
        return 'M';

    for (i = 0; i < 4; i++)
    {
        if (data[i] != 0xff)
            data_ff = false;
        if (spare[i] != 0xff)
            spare_ff = false;
        if (data[i] != 0x00)
            data_00 = false;
    }

    if (data_ff && spare_ff)
        return 'F';
    if (data_00)
        return 'Z';
    return 'D';
}

static void c4pro_nand_scan_stride(unsigned long start, unsigned long count,
                                   unsigned long stride,
                                   unsigned long *first_data,
                                   unsigned *data_count,
                                   unsigned *ff_count,
                                   unsigned *zero_count,
                                   unsigned *timeout_count)
{
    unsigned char data[4];
    unsigned char spare[4];
    unsigned long i;

    *first_data = 0xffffffff;
    *data_count = 0;
    *ff_count = 0;
    *zero_count = 0;
    *timeout_count = 0;

    for (i = 0; i < count; i++)
    {
        unsigned long psec = start + i * stride;
        char cls;

        if (!c4pro_nand_read_hw_subsector_psec_prefix(psec, data, spare))
        {
            (*timeout_count)++;
            continue;
        }

        cls = c4pro_nand_classify_prefix(data, spare);
        if (cls == 'M' || cls == 'D')
        {
            if (*first_data == 0xffffffff)
                *first_data = psec;
            (*data_count)++;
        }
        else if (cls == 'F')
            (*ff_count)++;
        else if (cls == 'Z')
            (*zero_count)++;
    }
}

static void c4pro_nand_probe_halt(void) NORETURN_ATTR;
static void c4pro_nand_probe_halt(void)
{
    unsigned char id[5];
    unsigned long first_data;
    unsigned d_count;
    unsigned f_count;
    unsigned z_count;
    unsigned t_count;

    verbose = true;
    reset_screen();
    lcd_puts(0, 0, "C4 raw NAND BL diag");
    lcd_update();

    SCU_CLKCFG &= ~CLKCFG_NAND;
    c4pro_nand_deselect();

    memset(id, 0, sizeof(id));
    if (c4pro_nand_read_id(0, id))
        lcd_putsf(0, 2, "ID %02x%02x%02x%02x%02x",
                  id[0], id[1], id[2], id[3], id[4]);
    else
        lcd_puts(0, 2, "ID timeout");
    lcd_update();

    c4pro_nand_scan_stride(0, 1024, 256, &first_data, &d_count,
                            &f_count, &z_count, &t_count);
    if (first_data == 0xffffffff)
        lcd_putsf(0, 4, "B64 first---- d%u f%u z%u t%u",
                  d_count, f_count, z_count, t_count);
    else
        lcd_putsf(0, 4, "B64 first%lu d%u f%u z%u t%u",
                  first_data, d_count, f_count, z_count, t_count);
    lcd_update();

    c4pro_nand_scan_stride(0, 1024, 512, &first_data, &d_count,
                            &f_count, &z_count, &t_count);
    if (first_data == 0xffffffff)
        lcd_putsf(0, 5, "B128 first---- d%u f%u z%u t%u",
                  d_count, f_count, z_count, t_count);
    else
        lcd_putsf(0, 5, "B128 first%lu d%u f%u z%u t%u",
                  first_data, d_count, f_count, z_count, t_count);
    lcd_update();

    lcd_puts(0, 7, "halt: reset/CE#");
    lcd_update();

    while (1)
        sleep(HZ);
}
#endif
#endif

#if defined(COLORFLY_C4_PRO) && defined(C4PRO_BOOTLOADER_PROBE)
static void c4pro_probe_loop(void) NORETURN_ATTR;

static void c4pro_fill(int x, int y, int width, int height, unsigned color)
{
    lcd_set_foreground(color);
    lcd_fillrect(x, y, width, height);
}

static void c4pro_puts(int x, int y, const unsigned char *text, unsigned color)
{
    lcd_set_foreground(color);
    lcd_putsxy(x, y, text);
}

static void c4pro_delay_100ms(void)
{
    int i;

    for (i = 0; i < 10; i++)
        udelay(10000);
}

static int c4pro_abs(int value)
{
    return value < 0 ? -value : value;
}

static void c4pro_adc_bar(int x, int y, int width, int value, int base)
{
    int bar = (value * width + 511) / 1023;
    int mark = (base * width + 511) / 1023;
    unsigned color = c4pro_abs(value - base) > 8
        ? LCD_RGBPACK(255, 220, 0)
        : LCD_RGBPACK(60, 190, 255);

    c4pro_fill(x, y, width, 8, LCD_RGBPACK(20, 20, 20));
    c4pro_fill(x, y, bar, 8, color);

    lcd_set_foreground(LCD_WHITE);
    lcd_vline(x + mark, y - 1, y + 8);
}

static void c4pro_probe_gpio_line(int y, const char *name, unsigned long value,
                                  unsigned long base, unsigned long sticky)
{
    unsigned color = value == base ? LCD_WHITE : LCD_RGBPACK(255, 220, 0);
    char line[48];

    snprintf(line, sizeof(line), "%s %02lx x%02lx s%02lx",
             name, value & 0xff, (value ^ base) & 0xff, sticky & 0xff);
    c4pro_puts(12, y, line, color);
}

static void c4pro_probe_loop(void)
{
    unsigned short base_adc[NUM_ADC_CHANNELS];
    unsigned short min_adc[NUM_ADC_CHANNELS];
    unsigned short max_adc[NUM_ADC_CHANNELS];
    unsigned long base_gpio[6];
    unsigned long sticky_gpio[6];
    bool have_base = false;
    unsigned frame = 0;

    verbose = false;

    while (1)
    {
        unsigned short adc[NUM_ADC_CHANNELS];
        unsigned long gpio[6];
        int i;
        char line[64];

        for (i = 0; i < NUM_ADC_CHANNELS; i++)
            adc[i] = adc_read(i);

        gpio[0] = GPIO_PADR;
        gpio[1] = GPIO_PBDR;
        gpio[2] = GPIO_PCDR;
        gpio[3] = GPIO_PDDR;
        gpio[4] = GPIO_PEDR;
        gpio[5] = GPIO_PFDR;

        if (!have_base)
        {
            for (i = 0; i < NUM_ADC_CHANNELS; i++)
            {
                base_adc[i] = adc[i];
                min_adc[i] = adc[i];
                max_adc[i] = adc[i];
            }

            for (i = 0; i < 6; i++)
            {
                base_gpio[i] = gpio[i];
                sticky_gpio[i] = 0;
            }

            have_base = true;
        }
        else
        {
            for (i = 0; i < NUM_ADC_CHANNELS; i++)
            {
                if (adc[i] < min_adc[i])
                    min_adc[i] = adc[i];
                if (adc[i] > max_adc[i])
                    max_adc[i] = adc[i];
            }

            for (i = 0; i < 6; i++)
                sticky_gpio[i] |= gpio[i] ^ base_gpio[i];
        }

        lcd_set_drawmode(DRMODE_SOLID);
        lcd_clear_display();

        c4pro_fill(0, 0, LCD_WIDTH / 4, 8, LCD_RGBPACK(220, 0, 0));
        c4pro_fill(LCD_WIDTH / 4, 0, LCD_WIDTH / 4, 8,
                   LCD_RGBPACK(0, 180, 0));
        c4pro_fill(LCD_WIDTH / 2, 0, LCD_WIDTH / 4, 8,
                   LCD_RGBPACK(0, 70, 220));
        c4pro_fill(3 * LCD_WIDTH / 4, 0, LCD_WIDTH / 4, 8, LCD_WHITE);

        lcd_set_foreground(LCD_WHITE);
        lcd_hline(0, LCD_WIDTH - 1, 0);
        lcd_hline(0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
        lcd_vline(0, 0, LCD_HEIGHT - 1);
        lcd_vline(LCD_WIDTH - 1, 0, LCD_HEIGHT - 1);

        c4pro_puts(12, 18, "C4 BUTTON/ADC PROBE", LCD_WHITE);

        snprintf(line, sizeof(line), "frame %u  btn %08x",
                 frame++, button_read_device());
        c4pro_puts(12, 34, line, LCD_RGBPACK(180, 220, 255));

        for (i = 0; i < NUM_ADC_CHANNELS; i++)
        {
            int y = 56 + i * 24;
            unsigned color = c4pro_abs(adc[i] - base_adc[i]) > 8
                ? LCD_RGBPACK(255, 220, 0)
                : LCD_WHITE;

            snprintf(line, sizeof(line), "A%d %04u d%+04d %04u-%04u",
                     i, adc[i], adc[i] - base_adc[i],
                     min_adc[i], max_adc[i]);
            c4pro_puts(12, y, line, color);
            c4pro_adc_bar(158, y + 2, 142, adc[i], base_adc[i]);
        }

        c4pro_probe_gpio_line(156, "PA", gpio[0], base_gpio[0], sticky_gpio[0]);
        c4pro_probe_gpio_line(170, "PB", gpio[1], base_gpio[1], sticky_gpio[1]);
        c4pro_probe_gpio_line(184, "PC", gpio[2], base_gpio[2], sticky_gpio[2]);
        c4pro_probe_gpio_line(198, "PD", gpio[3], base_gpio[3], sticky_gpio[3]);
        c4pro_probe_gpio_line(212, "PE", gpio[4], base_gpio[4], sticky_gpio[4]);
        c4pro_probe_gpio_line(226, "PF", gpio[5], base_gpio[5], sticky_gpio[5]);

        lcd_update();
        c4pro_delay_100ms();
    }
}
#endif


void main(void) NORETURN_ATTR;
void main(void)
{
    char filename[MAX_PATH];
    unsigned char* loadbuffer;
    void(*kernel_entry)(void);
    int ret;
    enum {rb, of} boot = rb;

    power_init();
    system_init();

#if defined(COLORFLY_C4_PRO) && 0
    /* C4 Pro safety net: USB plugged at boot jumps straight to Mask-ROM
     * before touching LCD, ADC, backlight, buttons, or NAND. These blocks
     * are still being brought up for this target, so they must not be on
     * the recovery path.
     */
    if (DEV_INFO & (1<<20))
        enter_rkusb();
#endif

    kernel_init();
    i2c_init();
#ifdef COLORFLY_C4_PRO
    c4pro_quiesce_usb();
#endif
    enable_irq();

    adc_init();
    lcd_init();
#ifndef COLORFLY_C4_PRO
    backlight_init();
#endif
    button_init_device();

    font_init();
    lcd_setfont(FONT_SYSFIXED);

#if defined(COLORFLY_C4_PRO) && defined(C4PRO_BOOTLOADER_PROBE)
    c4pro_probe_loop();
#endif

    show_logo();

#ifdef COLORFLY_C4_PRO
#ifdef C4PRO_BOOTLOADER_NAND_PROBE
    c4pro_nand_probe_halt();
#endif

    if (c4pro_has_ram_image())
    {
        printf("RAM image: %lu bytes", C4PRO_RAM_IMAGE_INFO[1]);
        sleep(HZ);
        kernel_entry = (void*)DRAM_ORIG;
        kernel_entry();

        printf("ERR: RAM image returned");
        lcd_update();
        while (1)
            sleep(HZ);
    }
#endif

    int btn = button_read_device();

    /* if there is some other button pressed
     * besides POWER/PLAY we boot into OF
     */
    if ((btn & ~POWEROFF_BUTTON))
        boot = of;

    /* if we are woken up by USB insert boot into OF */
#ifndef COLORFLY_C4_PRO
    if (DEV_INFO & (1<<20))
        boot = of;
#endif

    lcd_clear_display();

    ret = storage_init();
#ifdef COLORFLY_C4_PRO
    if(ret < 0)
        c4pro_storage_error_halt("storage_init", ret);
#else
    if(ret < 0)
        error(EATA, ret, true);
#endif

    filesystem_init();

    while((ret = disk_mount_all()) <= 0)
#ifdef COLORFLY_C4_PRO
        c4pro_storage_error_halt("disk_mount_all", ret);
#else
        error(EDISK, ret, true);
#endif

    loadbuffer = (unsigned char*)DRAM_ORIG; /* DRAM */

    if (boot == rb)
        snprintf(filename,sizeof(filename), BOOTDIR "/%s", BOOTFILE);
    else if (boot == of)
        snprintf(filename,sizeof(filename), BOOTDIR "/%s", "BASE.RKW");

    printf("Bootloader version: %s", rbversion);
    printf("Loading: %s", filename);

    ret = load_rkw(loadbuffer, filename, LOAD_SIZE);

    if (ret <= EFILE_EMPTY)
    {
        error(EBOOTFILE, ret, false);

        /* if we boot rockbox we shutdown on error
         * if we boot OF we fall back to rkusb mode on error
         */
        if (boot == rb)
        {
            power_off();
        }
        else
        {
            /* give visual feedback what we are doing */
            printf("Entering rockchip USB mode...");
            lcd_update();

            enter_rkusb();
        }
    }
    else
    {
        /* print 'Loading OK' */
        printf("Loading OK");
        sleep(HZ);
    }

    /* jump to entrypoint */
    kernel_entry = (void*) loadbuffer;
    commit_discard_idcache();

    printf("Executing");
    kernel_entry();

    /* this should never be reached actually */
    printf("ERR: Failed to boot");
    sleep(5*HZ);

    if (boot == rb)
    {
        power_off();
    }
    else
    {
        /* give visual feedback what we are doing */
        printf("Entering rockchip USB mode...");
        lcd_update();

        enter_rkusb();
    }

    /* hang */
    while(1);
}
