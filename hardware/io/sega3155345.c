#include "sega3155345.h"
#include "hardware/bus/sega3155308.h"

unsigned short button_state[3];
unsigned short sega3155345_pad_state[3];
unsigned char io_reg[16] = {0x80, 0x7f, 0x7f, 0x00, 0x7f, 0x7f, 0x40, 0xff, 0, 0, 0xff, 0, 0, 0xff, 0, 0}; /* initial state */

void sega3155345_pad_press_button(int pad, int button)
{
    button_state[pad] |= (1 << button);
}

void sega3155345_pad_release_button(int pad, int button)
{
    button_state[pad] &= ~(1 << button);
}

void sega3155345_pad_write(int pad, int value)
{
    unsigned char mask = io_reg[pad + 4];

    sega3155345_pad_state[pad] &= ~mask;
    sega3155345_pad_state[pad] |= value & mask;
}

unsigned char sega3155345_pad_read(int pad)
{
    unsigned char value;

    value = sega3155345_pad_state[pad] & 0x40;
    value |= 0x3f;

    if (value & 0x40)
    {
        value &= ~(button_state[pad] & 0x3f);
    }
    else
    {
        value &= ~(0xc | (button_state[pad] & 3) | ((button_state[pad] >> 2) & 0x30));
    }
    return value;
}

void sega3155345_write_ctrl(unsigned int address, unsigned int value)
{
    address >>= 1;
    // JOYPAD DATA
    if (address >= 0x1 && address < 0x3)
    {
        io_reg[address] = value;
        sega3155345_pad_write(address - 1, value);
        return;
    }
    // JOYPAD CTRL
    else if (address >= 0x4 && address < 0x6)
    {
        if (io_reg[address] != value)
        {
            io_reg[address] = value;
            sega3155345_pad_write(address - 4, io_reg[address - 3]);
        }
        return;
    }

    printf("io_write_memory(%x, %x)\n", address, value);
    return;
}

unsigned int sega3155345_read_ctrl(unsigned int address)
{
    address >>= 1;
    if (address >= 0x1 && address < 0x4)
    {
        unsigned char mask = 0x80 | io_reg[address + 3];
        unsigned char value;
        value = io_reg[address] & mask;
        value |= sega3155345_pad_read(address - 1) & ~mask;
        return value;
    }
    else
    {
        return io_reg[address];
    }
}

void sega3155345_set_reg(unsigned int reg, unsigned int value) {
    io_reg[reg] = value;
    return;
}