#include <string.h>
#include <libs/NukedOPN2/ym3438.h>
#include "ym2612.h"

#define YM2612_FREQ 7670454
#define YM2612_RATE YM2612_FREQ / 72
#define OUTPUT_FACTOR 11
#define OUTPUT_FACTOR_F 12
#define FILTER_CUTOFF 0.512331301282628 // 5894Hz  single pole IIR low pass
#define FILTER_CUTOFF_I (1 - FILTER_CUTOFF)

enum
{
    eg_num_attack = 0,
    eg_num_decay = 1,
    eg_num_sustain = 2,
    eg_num_release = 3
};

Bit32u *audio;
static ym3438_t ym_chip;
static Bit32u use_filter = 0;
static Bit32u chip_type = YM2612;

void ym2612_init()
{
    OPN2_SetOptions(chip_type);
    OPN2_Reset(&ym_chip, YM2612_RATE, YM2612_FREQ);
}

void ym2612_pulse_reset()
{
    OPN2_Reset(&ym_chip, YM2612_RATE, YM2612_FREQ);
}

unsigned int ym2612_read_memory_8(unsigned int address)
{
    return OPN2_Read(&ym_chip, address);
}

void ym2612_write_memory_8(unsigned int address, unsigned int value)
{
    address &= 0x3;
    //printf("[yamaha w8]0x%x\t %x\n", address, value);
    OPN2_Write(&ym_chip, address, value);
}

unsigned int ym2612_read_memory_16(unsigned int address)
{
    unsigned int value = OPN2_Read(&ym_chip, address) << 8 | OPN2_Read(&ym_chip, address + 1);
    return value;
}

void ym2612_write_memory_16(unsigned int address, unsigned int value)
{
    address &= 0x3;
    //printf("[yamaha w16] 0x%x\n", address);
    OPN2_Write(&ym_chip, address, value >> 8);
    OPN2_Write(&ym_chip, address + 1, value & 0xFF);
}

void ym2612_update()
{
    //OPN2_GenerateResampled(&ym_chip, audio);
}

void ym2612_set_buffer(unsigned char *audio_buffer)
{
    audio = audio_buffer;
}

void _OPN2_Reset(ym3438_t *chip, Bit32u rate, Bit32u clock)
{
    Bit32u i, rateratio;
    rateratio = chip->rateratio;
    memset(chip, 0, sizeof(ym3438_t));
    for (i = 0; i < 24; i++)
    {
        chip->eg_out[i] = 0x3ff;
        chip->eg_level[i] = 0x3ff;
        chip->eg_state[i] = eg_num_release;
        chip->multi[i] = 1;
    }
    for (i = 0; i < 6; i++)
    {
        chip->pan_l[i] = 1;
        chip->pan_r[i] = 1;
    }
    if (rate != 0)
    {
        chip->rateratio = (Bit32u)((((Bit64u)144 * rate) << RSM_FRAC) / clock);
    }
    else
    {
        chip->rateratio = rateratio;
    }
}

void _OPN2_SetChipType(Bit32u type)
{
    use_filter = 0;
    if (type == YM2612)
        use_filter = 1;
    if (type == YM2612_W_FILTER)
        type = YM2612;
    chip_type = type;
}

void OPN2_WriteBuffered(ym3438_t *chip, Bit32u port, Bit8u data)
{
    Bit64u time1, time2;
    Bit32s buffer[2];
    Bit64u skip;

    if (chip->writebuf[chip->writebuf_last].port & 0x04)
    {
        OPN2_Write(chip, chip->writebuf[chip->writebuf_last].port & 0X03,
                   chip->writebuf[chip->writebuf_last].data);

        chip->writebuf_cur = (chip->writebuf_last + 1) % OPN_WRITEBUF_SIZE;
        skip = chip->writebuf[chip->writebuf_last].time - chip->writebuf_samplecnt;
        chip->writebuf_samplecnt = chip->writebuf[chip->writebuf_last].time;
        while (skip--)
        {
            OPN2_Clock(chip, buffer);
        }
    }

    chip->writebuf[chip->writebuf_last].port = (port & 0x03) | 0x04;
    chip->writebuf[chip->writebuf_last].data = data;
    time1 = chip->writebuf_lasttime + OPN_WRITEBUF_DELAY;
    time2 = chip->writebuf_samplecnt;

    if (time1 < time2)
    {
        time1 = time2;
    }

    chip->writebuf[chip->writebuf_last].time = time1;
    chip->writebuf_lasttime = time1;
    chip->writebuf_last = (chip->writebuf_last + 1) % OPN_WRITEBUF_SIZE;
}

void OPN2_GenerateResampled(ym3438_t *chip, Bit32s *buf)
{
    Bit32u i;
    Bit32s buffer[2];
    Bit32u mute;

    while (chip->samplecnt >= chip->rateratio)
    {
        // printf("ratio     %d\t", chip->rateratio);
        // printf("samplecnt %d\t", chip->samplecnt);
        //if (chip->channel > 0)
        //    printf("channels  %d\n", chip->channel);
        chip->oldsamples[0] = chip->samples[0];
        chip->oldsamples[1] = chip->samples[1];
        chip->samples[0] = chip->samples[1] = 0;
        for (i = 0; i < 24; i++)
        {
            switch (chip->cycles >> 2)
            {
            case 0: // Ch 2
                mute = chip->mute[1];
                break;
            case 1: // Ch 6, DAC
                mute = chip->mute[5 + chip->dacen];
                break;
            case 2: // Ch 4
                mute = chip->mute[3];
                break;
            case 3: // Ch 1
                mute = chip->mute[0];
                break;
            case 4: // Ch 5
                mute = chip->mute[4];
                break;
            case 5: // Ch 3
                mute = chip->mute[2];
                break;
            default:
                mute = 0;
                break;
            }
            OPN2_Clock(chip, buffer);
            if (!mute)
            {

                chip->samples[0] += buffer[0];
                chip->samples[1] += buffer[1];
            }
            while (chip->writebuf[chip->writebuf_cur].time <= chip->writebuf_samplecnt)
            {
                if (!(chip->writebuf[chip->writebuf_cur].port & 0x04))
                {
                    break;
                }
                chip->writebuf[chip->writebuf_cur].port &= 0x03;
                OPN2_Write(chip, chip->writebuf[chip->writebuf_cur].port,
                           chip->writebuf[chip->writebuf_cur].data);
                chip->writebuf_cur = (chip->writebuf_cur + 1) % OPN_WRITEBUF_SIZE;
            }
            chip->writebuf_samplecnt++;
        }
        if (!use_filter)
        {
            chip->samples[0] *= OUTPUT_FACTOR;
            chip->samples[1] *= OUTPUT_FACTOR;
        }
        else
        {
            chip->samples[0] = chip->oldsamples[0] + FILTER_CUTOFF_I * (chip->samples[0] * OUTPUT_FACTOR_F - chip->oldsamples[0]);
            chip->samples[1] = chip->oldsamples[1] + FILTER_CUTOFF_I * (chip->samples[1] * OUTPUT_FACTOR_F - chip->oldsamples[1]);
        }
        chip->samplecnt -= chip->rateratio;
    }
    buf[0] = (Bit32s)((chip->oldsamples[0] * (chip->rateratio - chip->samplecnt) + chip->samples[0] * chip->samplecnt) / chip->rateratio);
    buf[1] = (Bit32s)((chip->oldsamples[1] * (chip->rateratio - chip->samplecnt) + chip->samples[1] * chip->samplecnt) / chip->rateratio);
    chip->samplecnt += 1 << RSM_FRAC;
}

void OPN2_GenerateStream(ym3438_t *chip, Bit32s **sndptr, Bit32u numsamples)
{
    Bit32u i;
    Bit32s *smpl, *smpr;
    Bit32s buffer[2];
    smpl = sndptr[0];
    smpr = sndptr[1];

    for (i = 0; i < numsamples; i++)
    {
        OPN2_GenerateResampled(chip, buffer);
        *smpl++ = buffer[0];
        *smpr++ = buffer[1];
    }
}

void OPN2_SetOptions(Bit8u flags)
{
    switch ((flags >> 3) & 0x03)
    {
    case 0x00: // YM2612
    default:
        OPN2_SetChipType(YM2612);
        break;
    case 0x01: // ASIC YM3438
        OPN2_SetChipType(YM3438_ASIC);
        break;
    case 0x02: // Discrete YM3438
        OPN2_SetChipType(YM3438_DISCRETE);
        break;
    case 0x03: // YM2612 without filter emulation
        OPN2_SetChipType(YM2612_W_FILTER);
        break;
    }
}

void OPN2_SetMute(ym3438_t *chip, Bit32u mute)
{
    Bit32u i;
    for (i = 0; i < 7; i++)
    {
        chip->mute[i] = (mute >> i) & 0x01;
    }
}