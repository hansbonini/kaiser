#include <libs/NukedOPN2/ym3438.h>
#include "ym2612.h"

#define YM2612_RATE 53267
#define YM2612_FREQ 0

unsigned char *audio;
static ym3438_t ym_chip;

void ym2612_init() {
    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&ym_chip);
}

void ym2612_pulse_reset() {
    OPN2_Reset(&ym_chip);
}

unsigned int ym2612_read_memory_8(unsigned int address) {
    return OPN2_Read(&ym_chip, address);
}

void ym2612_write_memory_8(unsigned int address, unsigned int value) {
    address &= 0x3;
    OPN2_Write(&ym_chip, address, value);
}

unsigned int ym2612_read_memory_16(unsigned int address) {
    unsigned int value = OPN2_Read(&ym_chip, address) << 8 | OPN2_Read(&ym_chip, address+1);
    return value;
}

void ym2612_write_memory_16(unsigned int address, unsigned int value) {
    address &= 0x3;
    OPN2_Write(&ym_chip, address, value >> 8);
    OPN2_Write(&ym_chip, address+1, value &0xFF);
}

void ym2612_update() {
    //OPN2_GenerateStream(&ym_chip, audio, 24);
}

void ym2612_set_buffer(unsigned char *audio_buffer)
{
    audio = audio_buffer;
}