#include <stdio.h>
#include <string.h>
#include "libs/Musashi/m68k.h"
#include "hardware/apu/ym2612.h"
#include "hardware/apu/z80.h"
#include "hardware/vdp/VDP.h"
#include "hardware/io/input.h"

/*
 * Megadrive memory map as well as main execution loop.
 */

unsigned char ROM[0x400000];
unsigned char RAM[0x10000];
unsigned char ZRAM[0x8000];

const int MCLOCK_NTSC = 53693175;
const int MCYCLES_PER_LINE = 3420;

int lines_per_frame = 262; /* NTSC: 262, PAL: 313 */
unsigned int *cycle_counter;

enum mapped_address
{
    ROM_ADDR = 1,
    ROM_MIRRORED_ADDR,
    Z80_ADDR,
    IO_CTRL,
    Z80_CTRL,
    VDP_ADDR,
    RAM_ADDR
};

void load_cartridge(unsigned char *buffer, size_t size)
{
    memset(ROM, 0, 0x100000);
    memset(RAM, 0, 0x10000);
    memset(ZRAM, 0, 0x8000);
    z80_set_memory(ZRAM);
    vdp_clear_memory();

    memcpy(ROM, buffer, size);
}
void power_on()
{
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    z80_init();
    ym2612_init();
}
void reset_emulation(unsigned char *buffer, size_t size)
{
    z80_pulse_reset();
    m68k_pulse_reset();
    ym2612_pulse_reset();
}
unsigned int get_cycle_counter()
{
    return m68k_cycles_run();
}
unsigned int map_address(unsigned int address)
{
    unsigned int range = (address & 0xff0000) >> 16;

    if (range <= 0x3f)
    {
        /* ROM */
        return ROM_ADDR;
    }
    // else if (range >= 0x40 && range < 0x7F) {
    //     /* ROM MIRROR */
    //     return ROM_MIRRORED_ADDR;
    // }
    else if (range == 0xa0)
    {
        /* Z80 RAM */
        if (address >= 0xa00000 && address < 0xa08000)
        {
            return Z80_ADDR;
        }
        return 0;
    }
    else if (range == 0xa1)
    {
        /* I/O and registers */
        if (address >= 0xa10000 && address < 0xa10020)
        {
            return IO_CTRL;
        }
        else if (address >= 0xa11100 && address < 0xa11300)
        {
            return Z80_CTRL;
        }
        return 0;
    }
    else if (range >= 0xc0 && range <= 0xdf)
    {
        /* VDP*/
        return VDP_ADDR;
    }
    else if (range >= 0xe0 && range <= 0xff)
    {
        /* RAM */
        return RAM_ADDR;
    }

    return 0;
}
unsigned int read_memory(unsigned int address)
{
    int mirror_mask = (address & 0xFF0000) >> 16;
    int mirror_address = address / (mirror_mask / 2);
    switch (map_address(address))
    {
    case ROM_ADDR:
        return ROM[address];
    case ROM_MIRRORED_ADDR:
        printf('mirror read(%x)\n', mirror_address);
        return ROM[mirror_address];
    case Z80_ADDR:
        if (address >= 0x4000 && address < 0x5FFF)
            return ym2612_read_memory_8(address & 0xFFFF);
        if (address >= 0x7F00 && address < 0x7F20)
            return vdp_read_memory_8(address & 0xFFFF);
        if (address >= 0x8000 && address < 0xFFFF)
            return ROM[address];
        return z80_read_memory_8(address & 0x7FFF);
    case IO_CTRL:
        return io_read_memory(address & 0x1F);
    case Z80_CTRL:
        return z80_read_ctrl(address & 0xFFFF);
    case VDP_ADDR:
        return vdp_read_memory_8(address & 0xFFFF);
    case RAM_ADDR:
        return RAM[address & 0xFFFF];
    default:
        printf("read(%x)\n", address);
    }
    return 0;
}
void write_memory(unsigned int address, unsigned int value)
{
    switch (map_address(address))
    {
    case ROM_ADDR:
        ROM[address] = value;
        return;
    case Z80_ADDR:
        if (address >= 0x4000 && address < 0x5FFF) {
            ym2612_write_memory_8(address & 0xFFFF, value);
            return;
        }
        if (address >= 0x7F00 && address < 0x7F20) {
            vdp_write_memory_8(address & 0x7FFF, value);
            return;
        }
        if (address >= 0x8000 && address < 0xFFFF) {
            ROM[address] = (value &0xFF);
            return;
        }
        z80_write_memory_8(address & 0x1FFF, value);
        return;
    case IO_CTRL:
        io_write_memory(address & 0x1F, value);
        return;
    case Z80_CTRL:
        z80_write_ctrl(address & 0xFFFF, value);
        return;
    case VDP_ADDR:
        vdp_write_memory_8(address & 0xFFFF, value);
        return;
    case RAM_ADDR:
        RAM[address & 0xFFFF] = value;
        return;
    default:
        printf("write(%x, %x)\n", address, value);
    }
    return 0;
}
unsigned int m68k_read_memory_8(unsigned int address)
{
    return read_memory(address);
}
unsigned int m68k_read_memory_16(unsigned int address)
{
    unsigned int range = (address & 0xff0000) >> 16;
    if (range == 0xa0)
    {
        if (address >= 0x4000 && address < 0x5FFF)
            return ym2612_read_memory_16(address & 0xFFFF);
        if (address >= 0x7F00 && address < 0x7F20)
            return vdp_read_memory_16(address & 0xFFFF);
        if (address >= 0x8000 && address < 0xFFFF)
            return ((ROM[address] << 8) &0xFF) | (ROM[address] &0xFF);
        return z80_read_memory_16(address & 0x7FFF);
    }
    if (range >= 0xc0 && range <= 0xdf)
    {
        return vdp_read_memory_16(address);
    }
    else
    {
        unsigned int word = read_memory(address) << 8 | read_memory(address + 1);
        return word;
    }
}
unsigned int m68k_read_memory_32(unsigned int address)
{
    unsigned int longword = read_memory(address) << 24 |
                            read_memory(address + 1) << 16 |
                            read_memory(address + 2) << 8 |
                            read_memory(address + 3);
    return longword;
}
void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    write_memory(address, value);

    return;
}
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    unsigned int range = (address & 0xff0000) >> 16;
    if (range == 0xa0)
    {
        if (address >= 0x4000 && address < 0x5FFF) {
            ym2612_write_memory_16(address & 0xFFFF, value);
            return;
        }
        if (address >= 0x7F00 && address < 0x7F20) {
            vdp_write_memory_16(address & 0xFFFF, value);
            return;
        }
       if (address >= 0x8000 && address < 0xFFFF) {
            ROM[address] = ((value << 8) &0xFF);
            ROM[address+1] = (value &0xFF);
            return;
        }
        z80_write_memory_16(address & 0x1FFF, value);
        return;
    }
    else if (range >= 0xc0 && range <= 0xdf)
    {
        vdp_write_memory_16(address, value);
        return;
    }
    else
    {
        write_memory(address, (value >> 8) & 0xff);
        write_memory(address + 1, (value)&0xff);
        return;
    }
    return;
}
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address, (value >> 16) & 0xffff);
    m68k_write_memory_16(address + 2, (value)&0xffff);

    return;
}

/*
 * The Megadrive frame, called every 1/60th second 
 * (or 1/50th in PAL mode)
 */
void frame()
{
    extern unsigned char vdp_regs[0x20], *screen;
    extern unsigned int vdp_status;
    extern int screen_width, screen_height;
    int hint_counter = vdp_regs[10];
    int line;
    extern int zclk;

    cycle_counter = 0;

    screen_width = (vdp_regs[12] & 0x01) ? 320 : 256;
    screen_height = (vdp_regs[1] & 0x08) ? 240 : 224;

    vdp_clear_vblank();
    memset(screen, 0, 320 * 240 * 4); /* clear the screen before rendering */

    for (line = 0; line < screen_height; line++)
    {
        m68k_execute(2560 + 120);
        z80_execute(2560 + 120);

        if (--hint_counter < 0)
        {
            hint_counter = vdp_regs[10];
            if (vdp_regs[0] & 0x10)
            {
                m68k_set_irq(4); /* HInt */
                //m68k_execute(7000);
            }
        }

        vdp_set_hblank();
        m68k_execute(64 + 313 + 259); /* HBlank */
        vdp_clear_hblank();

        int enable_planes = BIT(vdp_regs[1], 6);
        if (enable_planes)
            vdp_render_line(line); /* render line */

        m68k_execute(104);
    }
    vdp_set_vblank();

    m68k_execute(588);

    vdp_status |= 0x80;

    m68k_execute(200);

    if (vdp_regs[1] & 0x20)
    {
        m68k_set_irq(6); /* HInt */
    }

    m68k_execute(3420 - 788);
    line++;

    for (; line < lines_per_frame; line++)
    {
        m68k_execute(3420); /**/
    }
}

unsigned int  m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}
unsigned int  m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}
