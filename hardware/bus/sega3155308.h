#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_ROM_SIZE 0x400000   
#define MAX_RAM_SIZE 0x10000    
#define MAX_Z80_RAM_SIZE 0x8000

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
    TMSS_CTRL,
    VDP_ADDR,
    RAM_ADDR
};

enum sega3155308_pad_button
{
    PAD_UP,
    PAD_DOWN,
    PAD_LEFT,
    PAD_RIGHT,
    PAD_B,
    PAD_C,
    PAD_A,
    PAD_S
};

void load_cartridge(unsigned char *buffer, size_t size);
void power_on();
void reset_emulation();
void set_region();
unsigned int sega3155308_map_z80_address(unsigned int address);
unsigned int sega3155308_map_io_address(unsigned int address);
unsigned int sega3155308_map_address(unsigned int address);
unsigned int sega3155308_read_memory_8(unsigned int address);
unsigned int sega3155308_read_memory_16(unsigned int address);
void sega3155308_write_memory_8(unsigned int address, unsigned int value);
void sega3155308_write_memory_16(unsigned int address, unsigned int value);