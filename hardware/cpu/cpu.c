#include <stdio.h>
#include <string.h>
#include "libs/Musashi/m68k.h"
#include "hardware/apu/ym2612.h"
#include "hardware/apu/z80.h"
#include "hardware/vdp/VDP.h"
#include "hardware/io/input.h"

#define MAX_ROM_SIZE 0x400000   // ROM maximum size
#define MAX_RAM_SIZE 0x10000    // RAM maximum size
#define MAX_Z80_RAM_SIZE 0x8000 // ZRAM maximum size
#define MCLOCK_NTSC 53693175    // NTSC CLOCK

// Setup CPU Memory
unsigned char ROM[MAX_ROM_SIZE];      // 68K Main Program
unsigned char RAM[MAX_RAM_SIZE];      // 68K RAM
unsigned char ZRAM[MAX_Z80_RAM_SIZE]; // Z80 RAM

// Define default number of lines per frame
int lines_per_frame = 262; // NTSC: 262 lines
                           // PAL: 313 lines
// Define cycle counter
unsigned int *cycle_counter;

/******************************************************************************
 * 
 *   Define MAIN MEMORY address regions         
 * 
 ******************************************************************************/
enum mapped_address
{
    NONE = 0,
    ROM_ADDR,
    ROM_ADDR_MIRROR,
    Z80_RAM_ADDR,
    YM2612_ADDR,
    Z80_BANK_ADDR,
    Z80_VDP_ADDR,
    Z80_ROM_ADDR,
    IO_CTRL,
    Z80_CTRL,
    VDP_ADDR,
    RAM_ADDR
};

/******************************************************************************
 * 
 *   Load a Sega Genesis Cartridge into CPU Memory              
 * 
 ******************************************************************************/
void load_cartridge(unsigned char *buffer, size_t size)
{
    // Clear all volatile memory
    memset(ROM, 0, MAX_ROM_SIZE);
    memset(RAM, 0, MAX_RAM_SIZE);
    memset(ZRAM, 0, MAX_Z80_RAM_SIZE);

    // Set Z80 program as ZRAM
    z80_set_memory(ZRAM);

    // Clear VDP RAM
    vdp_clear_memory();

    // Copy file contents to CPU ROM memory
    memcpy(ROM, buffer, size);
}

/******************************************************************************
 * 
 *   Power ON the CPU
 *   Initialize 68K, Z80 and YM2612 Cores             
 * 
 ******************************************************************************/
void power_on()
{
    // Set M68K CPU as original MOTOROLA 68000
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    // Initialize M68K CPU
    m68k_init();
    // Initialize Z80 CPU
    z80_init();
    // Initialize YM2612 chip
    ym2612_init();
}

/******************************************************************************
 * 
 *   Reset the CPU Emulation
 *   Send a pulse reset to 68K, Z80 and YM2612 Cores             
 * 
 ******************************************************************************/
void reset_emulation(unsigned char *buffer, size_t size)
{
    // Send a reset pulse to Z80 CPU
    z80_pulse_reset();
    // Send a reset pulse to Z80 M68K
    m68k_pulse_reset();
    // Send a reset pulse to YM2612 chip
    ym2612_pulse_reset();
}

/******************************************************************************
 * 
 *   Main memory address mapper
 *   Map all main memory region address for CPU program             
 * 
 ******************************************************************************/
unsigned int map_z80_address(unsigned int address)
{
    unsigned int range = address & 0xFFFF;
    if (range >= 0x0000 && range <= 0x1FFF) // Z80 RAM ADDRESS 0x0000 - 0x1FFF
        return Z80_RAM_ADDR;
    if (range >= 0x4000 && range <= 0x5FFF) // YM2612 ADDRESS  0x4000 - 0x5FFF
        return YM2612_ADDR;
    if (range >= 0x7F00 && range <= 0x7F1F) // Z80 VDP ADDRESS 0x7F00 - 0x7F1F
        return Z80_VDP_ADDR;
    if (range >= 0x8000 && range <= 0xFFFF) // Z80 ROM ADDRESS 0x8000 - 0xFFFF
        return Z80_ROM_ADDR;
    // If not a valid address return 0
    return 0;
}

/******************************************************************************
 * 
 *   IO memory address mapper
 *   Map all input/output region address for CPU program             
 * 
 ******************************************************************************/
unsigned int map_io_address(unsigned int address)
{
    unsigned int range = address & 0xFFFF;
    if (address >= 0xa10000 && address < 0xa10020) // I/O and registers
        return IO_CTRL;
    else if (address >= 0xa11100 && address < 0xa11300) // Z80 Reset & BUS
        return Z80_CTRL;
    // If not a valid address return 0
    return 0;
}

/******************************************************************************
 * 
 *   Main memory address mapper
 *   Map all main memory region address for CPU program             
 * 
 ******************************************************************************/
unsigned int map_address(unsigned int address)
{
    // Mask address page
    unsigned int range = (address & 0xFF0000) >> 16;

    // Check mask and select memory type
    if (range < 0x40) //                        ROM ADDRESS 0x000000 - 0x3FFFFF
        return ROM_ADDR;
    else if (range >= 0x40 && range <= 0x9F)
        return ROM_ADDR_MIRROR;
    else if (range == 0xA0) //                  Z80 ADDRESS 0xA00000 - 0xA0FFFF
        return map_z80_address(address);
    else if (range == 0xA1) //                  IO ADDRESS  0xA10000 - 0xA1FFFF
        return map_io_address(address);
    else if (range >= 0xC0 && range <= 0xDF) // VDP ADDRESS 0xC00000 - 0xDFFFFFF
        return VDP_ADDR;
    else if (range >= 0xE0 && range <= 0xFF) // RAM ADDRESS 0xE00000 - 0xFFFFFFF
        return RAM_ADDR;
    // If not a valid address return 0
    return 0;
}

/******************************************************************************
 * 
 *   Main read address routine
 *   Read an address from memory mapped and return a value 
 * 
 ******************************************************************************/
unsigned int read_memory(unsigned int address)
{
    int mirror_address = address % 0x400000;
    switch (map_address(address))
    {
    case NONE:
        return 0;
    case ROM_ADDR_MIRROR:
        return ROM[mirror_address];
    case ROM_ADDR:
        return ROM[address];
    case Z80_RAM_ADDR:
        return z80_read_memory_8(address & 0x7FFF);
    case YM2612_ADDR:
        return ym2612_read_memory_8(address & 0xFFFF);
    case Z80_VDP_ADDR:
        return vdp_read_memory_8(address & 0xFFFF);
    case Z80_ROM_ADDR:
        return ROM[address];
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

/******************************************************************************
 * 
 *   Main write address routine
 *   Write an value to memory mapped on specified address 
 * 
 ******************************************************************************/
void write_memory(unsigned int address, unsigned int value)
{
    int mirror_address = address % 0x400000;
    switch (map_address(address))
    {
    case ROM_ADDR:
        ROM[address] = value;
        return;
    case ROM_ADDR_MIRROR:
        ROM[mirror_address] = value;
        return;
    case Z80_RAM_ADDR:
        z80_write_memory_8(address & 0x1FFF, value);
        return;
    case YM2612_ADDR:
        ym2612_write_memory_8(address & 0xFFFF, value);
        return;
    case Z80_VDP_ADDR:
        vdp_write_memory_8(address & 0x7FFF, value);
    case Z80_ROM_ADDR:
        ROM[address] = (value & 0xFF);
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
    return;
}

/******************************************************************************
 * 
 *   68K CPU read address R8
 *   Read an address from memory mapped and return value as byte
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_8(unsigned int address)
{
    return read_memory(address);
}

/******************************************************************************
 * 
 *   68K CPU read address R16
 *   Read an address from memory mapped and return value as word
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_16(unsigned int address)
{
    unsigned int w;
    int mirror_address = address % 0x400000;
    switch (map_address(address))
    {
    case NONE:
        return 0;
    case ROM_ADDR_MIRROR:
        return (ROM[mirror_address] << 8) | ROM[mirror_address + 1];
    case Z80_RAM_ADDR:
        return z80_read_memory_16(address & 0x7FFF);
    case YM2612_ADDR:
        return ym2612_read_memory_16(address & 0xFFFF);
    case Z80_VDP_ADDR:
        return vdp_read_memory_16(address);
    case Z80_ROM_ADDR:
        return ((ROM[address] << 8) & 0xFF) | (ROM[address] & 0xFF);
    case VDP_ADDR:
        return vdp_read_memory_16(address);
    default:
        w = (read_memory(address) << 8) | read_memory(address + 1);
        return w;
    }
    return 0;
}

/******************************************************************************
 * 
 *   68K CPU read address R32
 *   Read an address from memory mapped and return value as long
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_32(unsigned int address)
{
    unsigned int longword = read_memory(address) << 24 |
                            read_memory(address + 1) << 16 |
                            read_memory(address + 2) << 8 |
                            read_memory(address + 3);
    return longword;
}

/******************************************************************************
 * 
 *   68K CPU write address W8
 *   Write an value as byte to memory mapped on specified address
 * 
 ******************************************************************************/
void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    write_memory(address, value);

    return;
}

/******************************************************************************
 * 
 *   68K CPU write address W16
 *   Write an value as word to memory mapped on specified address
 * 
 ******************************************************************************/
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    int mirror_address = address % 0x400000;
    unsigned int w;
    switch (map_address(address))
    {
    case NONE:
        return;
    case ROM_ADDR_MIRROR:
        ROM[mirror_address] = value << 8;
        ROM[mirror_address + 1] = value & 0xFF;
        return;
    case Z80_RAM_ADDR:
        z80_write_memory_16(address & 0x1FFF, value);
        return;
    case YM2612_ADDR:
        ym2612_write_memory_16(address & 0xFFFF, value);
        return;
    case Z80_VDP_ADDR:
        vdp_write_memory_16(address, value);
        return;
    case Z80_ROM_ADDR:
        ROM[address] = ((value << 8) & 0xFF);
        ROM[address + 1] = (value & 0xFF);
        return;
    case VDP_ADDR:
        vdp_write_memory_16(address, value);
        return;
    default:
        write_memory(address, (value >> 8) & 0xff);
        write_memory(address + 1, (value)&0xff);
        w = value;
        return;
    }
    return;
}

/******************************************************************************
 * 
 *   68K CPU write address W32
 *   Write an value as word to memory mapped on specified address
 * 
 ******************************************************************************/
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address, (value >> 16) & 0xffff);
    m68k_write_memory_16(address + 2, (value)&0xffff);

    return;
}

/******************************************************************************
 * 
 *   68K CPU Main Loop
 *   Perform a frame which is called every 1/60th second on NTSC
 *   and called every 1/50th on PAL
 * 
 ******************************************************************************/
void frame()
{
    extern unsigned char vdp_regs[0x20], *screen, *audio;
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
    memset(audio, 0, 1080 * 2);       /* clear the audio before rendering */

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

        ym2612_update();
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

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}
unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

unsigned int get_cycle_counter()
{
    return m68k_cycles_run();
}
