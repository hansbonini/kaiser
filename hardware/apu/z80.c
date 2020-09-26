#include "z80.h"
#include <libs/Z80/Z80.h>

#define M68K_FREQ_DIVISOR 7
#define Z80_FREQ_DIVISOR 14

int bus_ack = 0;
int reset = 0;
int zclk = 0;

unsigned char *Z80_RAM;
static Z80 cpu;

void ResetZ80(register Z80 *R);

void z80_pulse_reset()
{
    cpu.IPeriod = 1;
    cpu.ICount = 0;
    cpu.Trace = 0;
    cpu.Trap = 0x0009;
    ResetZ80(&cpu);
}

void z80_execute(unsigned int target)
{
    extern int cycle_counter;
    int rem;
    int mclk = cycle_counter * M68K_FREQ_DIVISOR;
    zclk = (target-mclk) / Z80_FREQ_DIVISOR;
    if (zclk >= target)
        return;
    rem = ExecZ80(&cpu, zclk);
    zclk = target - rem*Z80_FREQ_DIVISOR;
}

void z80_set_memory(unsigned int *buffer)
{
    Z80_RAM = buffer;
}

void z80_write_ctrl(unsigned int address, unsigned int value)
{
    if (address == 0x1100) // BUSREQ
    {
        if (value)
        {
            bus_ack = 1;
        }
        else
        {
            bus_ack = 0;
        }
    }
    else if (address == 0x1200) // RESET
    {
        if (value)
        {
            cpu.IRequest = INT_IRQ;
            reset = 1;
        }
        else
        {
            cpu.IRequest = INT_NONE;
            reset = 0;
        }
    }
}

unsigned int z80_read_ctrl(unsigned int address)
{
    if (address == 0x1100)
    {
        return 0x00 | !bus_ack;
    }
    else if (address == 0x1101)
    {
        return 0x00;
    }
    else if (address == 0x1200)
    {
        return 0x00 | reset;
    }
    else if (address == 0x1201)
    {
        return 0x00;
    }
    return 0;
}

void z80_write_memory_8(unsigned int address, unsigned int value)
{
    WrZ80(address, value &0xFF);
}
unsigned int z80_read_memory_8(unsigned int address)
{
    return RdZ80(address);
}

void z80_write_memory_16(unsigned int address, unsigned int value)
{
    WrZ80(address, value >> 8);
}
unsigned int z80_read_memory_16(unsigned int address)
{
    unsigned int value = RdZ80(address);
    return (value << 8) | value;
}

word LoopZ80(register Z80 *R) {}
byte RdZ80(register word Addr)
{
    return Z80_RAM[Addr];
}
void WrZ80(register word Addr, register byte Value)
{
    Z80_RAM[Addr] = Value;
}
byte InZ80(register word Port) {}
void OutZ80(register word Port, register byte Value) {}
void PatchZ80(register Z80 *R) {}