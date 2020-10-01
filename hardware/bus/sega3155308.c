#include <stdio.h>
#include <string.h>
#include <libs/Musashi/m68k.h>
#include "sega3155308.h"

// Setup CPU Memory
unsigned char ROM[MAX_ROM_SIZE];      // 68K Main Program
unsigned char RAM[MAX_RAM_SIZE];      // 68K RAM
unsigned char ZRAM[MAX_Z80_RAM_SIZE]; // Z80 RAM
unsigned char TMSS[0x4];

// TMSS
int tmss_state = 0;
int tmss_count = 0;

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

    // Set Z80 Memory as ZRAM
    z80_set_memory(ZRAM);
    z80_pulse_reset();

    // Copy file contents to CPU ROM memory
    memcpy(ROM, buffer, size);
    set_region();
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
void reset_emulation()
{
   // Send a reset pulse to Z80 CPU
    z80_pulse_reset();
    // Send a reset pulse to Z80 M68K
    m68k_pulse_reset();
    // Send a reset pulse to YM2612 chip
    ym2612_pulse_reset();
    // Send a reset pulse to SEGA 315-5313 chip
    sega3155313_reset();
}


/******************************************************************************
 * 
 *   Set Region
 *   Look at ROM to set console compatible region            
 * 
 ******************************************************************************/
void set_region() {
    if (ROM[0x1F0]==0x31 || ROM[0x1F0]==0x4a)
        sega3155345_set_reg(0, 0x00);
    else if (ROM[0x1F0]==0x41)
        sega3155345_set_reg(0, 0xE0);
    else
        sega3155345_set_reg(0, 0xA0);
}


/******************************************************************************
 * 
 *   Main memory address mapper
 *   Map all main memory region address for CPU program             
 * 
 ******************************************************************************/
unsigned int sega3155308_map_z80_address(unsigned int address)
{
    unsigned int range = address & 0xFFFF;
    if (range >= 0x4000 && range <= 0x5FFF) // YM2612 ADDRESS  0x4000 - 0x5FFF
        return YM2612_ADDR;
    if (range >= 0x7F00 && range <= 0x7F1F) // Z80 VDP ADDRESS 0x7F00 - 0x7F1F
        return Z80_VDP_ADDR;
    if (range >= 0x8000 && range <= 0xFFFF) // Z80 ROM ADDRESS 0x8000 - 0xFFFF
        return Z80_ROM_ADDR;
    // If not a valid address return 0
    return Z80_RAM_ADDR;
}

/******************************************************************************
 * 
 *   IO memory address mapper
 *   Map all input/output region address for CPU program             
 * 
 ******************************************************************************/
unsigned int sega3155308_map_io_address(unsigned int address)
{
    unsigned int range = address & 0xFFFF;
    if (address >= 0xa10000 && address < 0xa10020) //      I/O and registers
        return IO_CTRL;
    else if (address >= 0xa11100 && address < 0xa11300) // Z80 Reset & BUS
        return Z80_CTRL;
    else if (address >= 0xa14000 && address < 0xa11404)
        return (tmss_state ==0) ? TMSS_CTRL : NONE;
    // If not a valid address return 0
    return 0;
}

/******************************************************************************
 * 
 *   Main memory address mapper
 *   Map all main memory region address for CPU program             
 * 
 ******************************************************************************/
unsigned int sega3155308_map_address(unsigned int address)
{
    // Mask address page
    unsigned int range = (address & 0xFF0000) >> 16;

    // Check mask and select memory type
    if (range < 0x40) //                        ROM ADDRESS 0x000000 - 0x3FFFFF
        return ROM_ADDR;
    else if (range >= 0x40 && range <= 0x80)
        return ROM_ADDR_MIRROR;
    else if (range == 0xA0) //                  Z80 ADDRESS 0xA00000 - 0xA0FFFF
        return sega3155308_map_z80_address(address);
    else if (range == 0xA1) //                  IO ADDRESS  0xA10000 - 0xA1FFFF
        return sega3155308_map_io_address(address);
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
unsigned int sega3155308_read_memory_8(unsigned int address)
{
    int ret;
    int mirror_address = address % 0x400000-2;
    switch (sega3155308_map_address(address))
    {
    case NONE:
        return 0xFF;
    case ROM_ADDR_MIRROR:
        return ROM[mirror_address];
    case ROM_ADDR:
        return ROM[address];
    case Z80_RAM_ADDR:
        return z80_read_memory_8(address & 0x7FFF);
    case YM2612_ADDR:
        return ym2612_read_memory_8(address & 0xFFFF);
    case Z80_VDP_ADDR:
        return sega3155313_read_memory_8(address & 0xFFFF);
    case Z80_ROM_ADDR:
        return ROM[address];
    case IO_CTRL:
        return sega3155345_read_ctrl(address & 0x1F);
    case Z80_CTRL:
        return z80_read_ctrl(address & 0xFFFF);
    case TMSS_CTRL:
        if (tmss_state == 0)
            return TMSS[address&0x4];
        return 0xFF;
    case VDP_ADDR:
        return sega3155313_read_memory_8(address & 0xFFFF);
    case RAM_ADDR:
        return RAM[address & 0xFFFF];
    default:
        printf("read(%x)\n", address);
    }
    return 0;
}

unsigned int sega3155308_read_memory_16(unsigned int address) {
    unsigned int w;
    int mirror_address = address % 0x400000-2;
    switch (sega3155308_map_address(address))
    {
    case NONE:
        return 0;
    case ROM_ADDR_MIRROR:
        return (ROM[mirror_address + 1] << 8) | ROM[mirror_address];
    case Z80_RAM_ADDR:
        return z80_read_memory_16(address & 0x7FFF);
    case YM2612_ADDR:
        return ym2612_read_memory_16(address & 0xFFFF);
    case Z80_VDP_ADDR:
        return sega3155313_read_memory_16(address);
    case Z80_ROM_ADDR:
        return ((ROM[address] << 8) & 0xFF) | (ROM[address] & 0xFF);
    case VDP_ADDR:
        return sega3155313_read_memory_16(address & 0xFFFF);
    default:
        w = (sega3155308_read_memory_8(address) << 8) | sega3155308_read_memory_8(address + 1);
        return w;
    }
    return 0;
}

/******************************************************************************
 * 
 *   Main write address routine
 *   Write an value to memory mapped on specified address 
 * 
 ******************************************************************************/
void sega3155308_write_memory_8(unsigned int address, unsigned int value)
{
    int mirror_address = address % 0x400000-2;
    switch (sega3155308_map_address(address))
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
        sega3155313_write_memory_8(address & 0x7FFF, value);
    case Z80_ROM_ADDR:
         ROM[address] = (value & 0xFF);
         return;
    case IO_CTRL:
        sega3155345_write_ctrl(address & 0x1F, value);
        return;
    case Z80_CTRL:
        z80_write_ctrl(address & 0xFFFF, value);
        return;
    case TMSS_CTRL:
        if (tmss_state == 0) {
            TMSS[address&0x4] = value;
            tmss_count++;
            if (tmss_count==4)
                tmss_state=1;
        }
        return;
    case VDP_ADDR:
        sega3155313_write_memory_8(address & 0xFFFF, value);
        return;
    case RAM_ADDR:
        RAM[address & 0xFFFF] = value;
        return;
    default:
        printf("write(%x, %x)\n", address, value);
    }
    return;
}

void sega3155308_write_memory_16(unsigned int address, unsigned int value) {
    int mirror_address = address % 0x400000-2;
    unsigned int w;
    switch (sega3155308_map_address(address))
    {
    case NONE:
        return;
    case ROM_ADDR_MIRROR:
        ROM[mirror_address + 1] = value << 8;
        ROM[mirror_address] = value & 0xFF;
        return;
    case Z80_RAM_ADDR:
        z80_write_memory_16(address & 0x1FFF, value);
        return;
    case YM2612_ADDR:
        ym2612_write_memory_16(address & 0xFFFF, value);
        return;
    case Z80_VDP_ADDR:
        sega3155313_write_memory_16(address, value);
        return;
    case Z80_ROM_ADDR:
        ROM[address] = ((value << 8) & 0xFF);
        ROM[address + 1] = (value & 0xFF);
        return;
    case VDP_ADDR:
        sega3155313_write_memory_16(address & 0xFFFF, value);
        return;
    default:
        sega3155308_write_memory_8(address, (value >> 8) & 0xff);
        sega3155308_write_memory_8(address + 1, (value)&0xff);
        w = value;
        return;
    }
    return;
}