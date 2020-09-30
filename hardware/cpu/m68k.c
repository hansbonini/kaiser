#include <stdio.h>
#include <string.h>
#include "libs/Musashi/m68k.h"
#include "hardware/apu/ym2612.h"
#include "hardware/apu/z80.h"
#include "hardware/bus/sega3155308.h"
#include "hardware/io/sega3155345.h"
#include "hardware/vdp/sega3155313.h"

#define MCLOCK_NTSC 53693175    // NTSC CLOCK

// Define default number of lines per frame
int lines_per_frame = 262; // NTSC: 262 lines
                           // PAL: 313 lines
// Define cycle counter
unsigned int *cycle_counter;

/******************************************************************************
 * 
 *   68K CPU read address R8
 *   Read an address from memory mapped and return value as byte
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_8(unsigned int address)
{
    return sega3155308_read_memory_8(address);
}

/******************************************************************************
 * 
 *   68K CPU read address R16
 *   Read an address from memory mapped and return value as word
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_16(unsigned int address)
{
    return sega3155308_read_memory_16(address);
}

/******************************************************************************
 * 
 *   68K CPU read address R32
 *   Read an address from memory mapped and return value as long
 * 
 ******************************************************************************/
unsigned int m68k_read_memory_32(unsigned int address)
{
    unsigned int l = m68k_read_memory_8(address) << 24 |
                     m68k_read_memory_8(address + 1) << 16 |
                     m68k_read_memory_8(address + 2) << 8 |
                     m68k_read_memory_8(address + 3);
    return l;
}

/******************************************************************************
 * 
 *   68K CPU write address W8
 *   Write an value as byte to memory mapped on specified address
 * 
 ******************************************************************************/
void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    sega3155308_write_memory_8(address, value);

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
    sega3155308_write_memory_16();
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
    extern unsigned char sega3155313_regs[0x20], *screen, *audio;
    extern unsigned int sega3155313_status;
    extern int screen_width, screen_height;
    int hint_counter = sega3155313_regs[10];
    int line;
    extern int zclk;

    cycle_counter = 0;

    screen_width = (sega3155313_regs[12] & 0x01) ? 320 : 256;
    screen_height = (sega3155313_regs[1] & 0x08) ? 240 : 224;

    sega3155313_clear_vblank();

    memset(screen, 0, 320 * 240 * 4); /* clear the screen before rendering */
    memset(audio, 0, 1080 * 2);       /* clear the audio before rendering */

    for (line = 0; line < screen_height; line++)
    {
        m68k_execute(2560 + 120);
        z80_execute(2560 + 120);

        if (--hint_counter < 0)
        {
            hint_counter = sega3155313_regs[10];
            if (sega3155313_regs[0] & 0x10)
            {
                m68k_set_irq(4); /* HInt */
                //m68k_execute(7000);
            }
        }

        sega3155313_set_hblank();
        m68k_execute(64 + 313 + 259); /* HBlank */
        sega3155313_clear_hblank();

        int enable_planes = BIT(sega3155313_regs[1], 6);
        if (enable_planes)
            sega3155313_render_line(line); /* render line */

        ym2612_update();
        m68k_execute(104);
    }
    sega3155313_set_vblank();

    m68k_execute(588);

    sega3155313_status |= 0x80;

    m68k_execute(200);

    if (sega3155313_regs[1] & 0x20)
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
