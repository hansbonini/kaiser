#include <stdio.h>
#include "libs/Musashi/m68k.h"
#include "VDP.h"

#define M68K_FREQ_DIVISOR 7
#define Z80_FREQ_DIVISOR 14
#define MCYCLES_PER_LINE 3420          // Cycles per Line

/*
 * Megadrive VDP emulation
 */

unsigned char VRAM[0x10000];
unsigned short CRAM[0x40];
unsigned short VSRAM[0x40];
unsigned char SAT_CACHE[0x400];
unsigned char vdp_regs[0x20];
unsigned short fifo[4];

unsigned char *screen, *scaled_screen;

int control_code = 0;
unsigned int control_address = 0;
int control_pending = 0;
unsigned int vdp_status = 0x3400;

int screen_width;
int screen_height;

unsigned int dma_length;
unsigned int dma_source;
int dma_fill_pending = 0;

unsigned int hvcounter_latch = 0;
int hvcounter_latched = 0;

int mode_h40;
int mode_pal;

int old_hint = 0;

/* Set a pixel on the screen using the Color RAM */
#define set_pixel(scr, x, y, index)                                                           \
    do                                                                                        \
    {                                                                                         \
        int pixel = ((240 - screen_height) / 2 + (y)) * 320 + (x) + (320 - screen_width) / 2; \
        scr[pixel * 4 + 0] = (CRAM[index] >> 4) & 0xe0;                                       \
        scr[pixel * 4 + 1] = (CRAM[index]) & 0xe0;                                            \
        scr[pixel * 4 + 2] = (CRAM[index] << 4) & 0xe0;                                       \
    } while (0);

void push_fifo(unsigned int value)
{
    fifo[3] = fifo[2];
    fifo[2] = fifo[1];
    fifo[1] = fifo[0];
    fifo[0] = value;
}

/*
 * Draw a single pixel of a cell 
 */
void draw_cell_pixel(unsigned int cell, int cell_x, int cell_y, int x, int y)
{
    unsigned char *pattern = &VRAM[0x20 * (cell & 0x7ff)];

    int pattern_index = 0;
    if (cell & 0x1000) /* v flip */
        pattern_index = (7 - (cell_y & 7)) << 2;
    else
        pattern_index = (cell_y & 7) << 2;

    if (cell & 0x800) // h flip
        pattern_index += (7 - (cell_x & 7)) >> 1;
    else
        pattern_index += (cell_x & 7) >> 1;

    unsigned char color_index = pattern[pattern_index];
    if ((cell_x & 1) ^ ((cell >> 11) & 1))
        color_index &= 0xf;
    else
        color_index >>= 4;

    if (color_index)
    {
        color_index += (cell & 0x6000) >> 9;
        set_pixel(screen, x, y, color_index);
    }
}

/*
 * Render the scroll layers (plane A and B)
 */
void vdp_render_bg(int line, int plane, int priority)
{
    int h_cells = 32, v_cells = 32;
    int scroll_base;
    unsigned int hscroll_mask;
    unsigned int size = REG16_HSCROLL_SIZE | (REG16_VSCROLL_SIZE<<4);
    unsigned short vscroll_mask;

  	switch (size)
	{
		case 0x00: h_cells = 32; v_cells = 32; break;
		case 0x01: h_cells = 64; v_cells = 32; break;
		case 0x02: h_cells = 64; v_cells = 1; break;
		case 0x03: h_cells = 128;v_cells = 32; break;

		case 0x10: h_cells = 32; v_cells = 64; break;
		case 0x11: h_cells = 64; v_cells = 64; break;
		case 0x12: h_cells = 64; v_cells = 1; break;
		case 0x13: h_cells = 128;v_cells = 32;break;

		case 0x20: h_cells = 32; v_cells = 64; break;
		case 0x21: h_cells = 64; v_cells = 64; break;
		case 0x22: h_cells = 64; v_cells = 1; break;
		case 0x23: h_cells = 128;v_cells = 64; break;

		case 0x30: h_cells = 32; v_cells = 128; break;
		case 0x31: h_cells = 64; v_cells = 64; break;
		case 0x32: h_cells = 64; v_cells = 1; break;
		case 0x33: h_cells = 128;v_cells = 128;break;
	}

    switch (REG11_HSCROLL_MODE)
    {
    case 0x00:
        hscroll_mask = 0x0000;
        break;
    case 0x01:
        hscroll_mask = 0x0007;
        break;
    case 0x02:
        hscroll_mask = 0xfff8;
        break;
    case 0x03:
        hscroll_mask = 0xffff;
        break;
    }

    if (REG11_VSCROLL_MODE)
        vscroll_mask = 0xfff0;
    else
        vscroll_mask = 0x0000;

    if (plane == 0)
        scroll_base = REG4_NAMETABLE_B;
    else
        scroll_base = REG2_NAMETABLE_A;

    int hscroll_addr = REG13_HSCROLL_ADDRESS + ((line & hscroll_mask)) * 4 + (plane ^ 1) * 2;;
    short hscroll = VRAM[hscroll_addr]<<8|VRAM[hscroll_addr+1];
    for (int column = 0; column < screen_width; column++)
    {
        int vscroll_addr = (column & vscroll_mask) / 4 + (plane ^ 1);
        short vscroll = VSRAM[vscroll_addr] & 0x3ff;
        int vcolumn = (line+vscroll)&((v_cells*8)-1);
		int hcolumn = (column-hscroll)&((h_cells*8)-1);
        int base_addr = (scroll_base + (((vcolumn>>3) * h_cells + (hcolumn>>3)) * 2));
        unsigned int cell = VRAM[base_addr] << 8 | VRAM[base_addr+1];
        int pri = ((cell & 0x8000) >> 15);
        if ((pri == 1 && priority == 1) || (pri == 0 && priority == 0))
            draw_cell_pixel(cell, hcolumn, vcolumn, column, line);
        else
            column++;
    }
}

vdp_render_plane_b (int line, int priority){
    vdp_render_bg(line, 0, priority);
}

vdp_render_plane_a (int line, int priority){
    vdp_render_bg(line, 1, priority);
}

/*
 * Render part of a sprite on a given line.
 */
void vdp_render_sprite(int sprite_index, int line)
{
    unsigned char *sprite = &VRAM[(vdp_regs[5] << 9) + sprite_index * 8];

    unsigned short y_pos = ((sprite[0] << 8) | sprite[1]) & 0x3ff;
    int h_size = ((sprite[2] >> 2) & 0x3) + 1;
    int v_size = (sprite[2] & 0x3) + 1;
    unsigned int cell = (sprite[4] << 8) | sprite[5];
    unsigned short x_pos = ((sprite[6] << 8) | sprite[7]) & 0x3ff;

    int y = (128 - y_pos + line) & 7;
    int cell_y = (128 - y_pos + line) >> 3;

    for (int cell_x = 0; cell_x < h_size; cell_x++)
    {
        for (int x = 0; x < 8; x++)
        {
            int e_x, e_cell;
            e_x = cell_x * 8 + x + x_pos - 128;
            e_cell = cell;

            if (cell & 0x1000)
                e_cell += v_size - cell_y - 1;
            else
                e_cell += cell_y;

            if (cell & 0x800)
                e_cell += (h_size - cell_x - 1) * v_size;
            else
                e_cell += cell_x * v_size;
            if (e_x >= 0 && e_x < screen_width)
            {
                draw_cell_pixel(e_cell, x, y, e_x, line);
            }
        }
    }
}

/*
 * Render the sprite layer.
 */
void vdp_render_sprites(int line, int priority)
{
    int mask = mode_h40 ? 0x7E : 0x7F;
    unsigned char *sprite_table = &VRAM[(vdp_regs[5] & mask) << 9];

    int sprite_queue[80];
    int i = 0;
    int cur_sprite = 0;
    while (1)
    {
        unsigned char *sprite = &VRAM[((vdp_regs[5] & mask) << 9) + cur_sprite * 8];
        unsigned char *cache = &SAT_CACHE[cur_sprite * 8];
        unsigned short y_pos = (cache[0] << 8) | cache[1];
        int v_size = (cache[2] & 0x3) + 1;
        unsigned int cell = (cache[4] << 8) | cache[5];

        int y_min = y_pos - 128;
        int y_max = (v_size - 1) * 8 + 7 + y_min;

        if (line >= y_min && line <= y_max)
        {
            if ((cell >> 15) == priority)
                sprite_queue[i++] = cur_sprite;
        }

        cur_sprite = sprite_table[cur_sprite * 8 + 3];
        if (!cur_sprite)
            break;

        if (i >= 80)
            break;
    }
    while (i > 0)
    {
        vdp_render_sprite(sprite_queue[--i], line);
    }
}

/*
 * Render the window layer.
 */
void vdp_render_window(int line, int priority)
{
    int h_cells = 64, v_cells = 32;
    int numcolumns = 0;
    int window_firstcol, window_lastcol;
    int window_firstline, window_lastline;
    int window_hsize = 32, window_vsize = 32;
    int window_is_bugged = 0;
    int window_right=REG17_WINDOW_RIGHT;
    int window_down=REG18_WINDOW_DOWN;
    int non_window_firstcol, non_window_lastcol;
    int sw=REG12_RS0 | REG12_RS1 << 1;
    int sh=REG1_240_LINE?240:224;
    
    unsigned int size = REG16_HSCROLL_SIZE | (REG16_VSCROLL_SIZE<<4);
    unsigned int base_w;

	switch (sw)
	{
		case 0: numcolumns = 32; window_hsize = 32; window_vsize = 32; base_w = ((REG3_NAMETABLE_W&0x1f)<<11); break;
		case 1: numcolumns = 32; window_hsize = 32; window_vsize = 32; base_w = ((REG3_NAMETABLE_W&0x1f)<<11); break;
		case 2: numcolumns = 40; window_hsize = 64; window_vsize = 32; base_w = ((REG3_NAMETABLE_W&0x1e)<<11); break;
		case 3: numcolumns = 40; window_hsize = 64; window_vsize = 32; base_w = ((REG3_NAMETABLE_W&0x1e)<<11); break; // talespin cares about base mask, used for status bar
	}
	if (window_right)
	{
		window_firstcol = REG17_WINDOW_HPOS*16;
		window_lastcol = numcolumns*8;
		if (window_firstcol>window_lastcol) window_firstcol = window_lastcol;

		non_window_firstcol = 0;
		non_window_lastcol = window_firstcol;
	}
	else
	{
		window_firstcol = 0;
		window_lastcol = REG17_WINDOW_HPOS*16;
		if (window_lastcol>numcolumns*8) window_lastcol = numcolumns*8;

		non_window_firstcol = window_lastcol;
		non_window_lastcol = numcolumns*8;

		if (window_lastcol!=0) window_is_bugged=1;
	}

	if (window_down)
	{
		window_firstline = REG18_WINDOW_VPOS*8;
		window_lastline = sh; // 240 in PAL?
		if (window_firstline>sh) window_firstline = screen_height;
	}
	else
	{
		window_firstline = 0;
		window_lastline = REG18_WINDOW_VPOS*8;
		if (window_lastline>sh) window_lastline = screen_height;
	}

	/* if we're on a window scanline between window_firstline and window_lastline the window is the full width of the screen */
	if (line>=window_firstline && line < window_lastline)
	{
		window_firstcol = 0; window_lastcol = numcolumns*8; // window is full-width of the screen
		non_window_firstcol = 0; non_window_lastcol=0; // disable non-window
	}

 	switch (size)
	{
		case 0x00: h_cells = 32; v_cells = 32; break;
		case 0x01: h_cells = 64; v_cells = 32; break;
		case 0x02: h_cells = 64; v_cells = 1; break;
		case 0x03: h_cells = 128;v_cells = 32; break;

		case 0x10: h_cells = 32; v_cells = 64; break;
		case 0x11: h_cells = 64; v_cells = 64; break;
		case 0x12: h_cells = 64; v_cells = 1; break;
		case 0x13: h_cells = 128;v_cells = 32;break;

		case 0x20: h_cells = 32; v_cells = 64; break;
		case 0x21: h_cells = 64; v_cells = 64; break;
		case 0x22: h_cells = 64; v_cells = 1; break;
		case 0x23: h_cells = 128;v_cells = 64; break;

		case 0x30: h_cells = 32; v_cells = 128; break;
		case 0x31: h_cells = 64; v_cells = 64; break;
		case 0x32: h_cells = 64; v_cells = 1; break;
		case 0x33: h_cells = 128;v_cells = 128;break;
	}

    for (int column = window_firstcol; column < window_lastcol; column++)
    {

        int vcolumn = (line)&((window_vsize*8)-1);
		int hcolumn = column&((window_hsize*8)-1); 
        int base_addr = (base_w) + ((vcolumn>>3) * window_hsize + (hcolumn>>3)) * 2;
        unsigned int cell = (VRAM[base_addr] << 8) | VRAM[base_addr+1];
        int pri = ((cell & 0x8000) >> 15);
        if ((pri == 1 && priority == 1) || (pri == 0 && priority == 0))
            draw_cell_pixel(cell, hcolumn, vcolumn, column, line);
    }
}

/*
 * Render a single line.
 */
void vdp_render_line(int line)
{
    /* TODO */
    mode_h40 = REG12_MODE_H40;
    mode_pal = REG1_PAL;

    /* Fill the screen with the backdrop color set in register 7 */
    for (int i = 0; i < screen_width; i++)
    {
        set_pixel(screen, i, line, vdp_regs[7] & 0x3f);
    }

    vdp_render_plane_b(line, 0); // PLANE B LOW
    vdp_render_plane_a(line, 0); // PLANE A LOW
    vdp_render_sprites(line, 0); // SPRITES LOW
    vdp_render_window(line, 0);  // WINDOW LOW
    vdp_render_plane_b(line, 1); // PLANE B HIGH
    vdp_render_plane_a(line, 1); // PLANE A HIGH
    vdp_render_sprites(line, 1); // SPRITES HIGH
    vdp_render_window(line, 1);  // WINDOW HIGH
}

void vdp_set_buffers(unsigned char *screen_buffer, unsigned char *scaled_buffer)
{
    screen = screen_buffer;
    scaled_screen = scaled_buffer;
}

void vdp_debug_status(char *s)
{
    int i = 0;
    s[0] = 0;
    s += sprintf(s, "VDP: ");
    s += sprintf(s, "%04x ", vdp_status);
    for (i = 0; i < 0x20; i++)
    {
        if (!(i % 16))
            s += sprintf(s, "\n");
        s += sprintf(s, "%02x ", vdp_regs[i]);
    }
}

unsigned int vdp_read_data_port_16()
{
    enum
    {
        CRAM_BITMASK = 0x0EEE,
        VSRAM_BITMASK = 0x07FF,
        VRAM8_BITMASK = 0x00FF
    };
    unsigned int value;
    control_pending = 0;

    if (control_code & 1) /* check if write is set */
    {
        switch (control_code & 0xF)
        {
        case 0x1:
            // No byteswapping here
            value = VRAM[(control_address)&0xFFFE] << 8;
            value |= VRAM[(control_address | 1) & 0xFFFF];
            control_address += REG15_DMA_INCREMENT;
            control_address &= 0xFFFF;
            return value;
        case 0x4:
            if (((control_address & 0x7f) >> 1) >= 0x28)
                value = VSRAM[0];
            else
                value = VSRAM[(control_address & 0x7f) >> 1];
            value = (value & VSRAM_BITMASK) | (fifo[3] & ~VSRAM_BITMASK);
            control_address += REG15_DMA_INCREMENT;
            control_address &= 0x7F;
            return value;
        case 0x8:
            value = CRAM[(control_address & 0x7f) >> 1];
            value = (value & CRAM_BITMASK) | (fifo[3] & ~CRAM_BITMASK);
            control_address += REG15_DMA_INCREMENT;
            control_address &= 0x7F;
            return value;
        case 0xC: /* 8-Bit memory access */
            value = VRAM[(control_address ^ 1) & 0xFFFF];
            value = (value & VRAM8_BITMASK) | (fifo[3] & ~VRAM8_BITMASK);
            control_address += REG15_DMA_INCREMENT;
            control_address &= 0xFFFF;
            return value;
        default:
            printf(!'VDP Data Port unhandled');
            return 0xFF;
        }
    }
}

void vdp_write_data_port_16(unsigned int value)
{
    control_pending = 0;

    push_fifo(value);

    if (control_code & 1) /* check if write is set */
    {
        switch (control_code & 0xF)
        {
        case 0x1: /* VRAM write */
            VRAM_W(control_address, (value >> 8) & 0xFF);
            VRAM_W(control_address + 1, (value)&0xFF);
            control_address += REG15_DMA_INCREMENT;
            break;
        case 0x3: /* CRAM write */
            CRAM[(control_address & 0x7f) >> 1] = value;
            control_address += REG15_DMA_INCREMENT;
            break;
        case 0x5: /* VSRAM write */
            VSRAM[(control_address & 0x7f) >> 1] = value;
            control_address += REG15_DMA_INCREMENT;
            break;
        case 0x0:
        case 0x4:
        case 0x8: // Write operation after setting up
                  // Makes Compatible with Alladin and Ecco 2
            break;
        case 0x9: // VDP FIFO TEST
            break;
        default:
            printf(!'VDP Data Port invalid');
        }
    }
    /* if a DMA is scheduled, do it */
    if (dma_fill_pending)
    {
        dma_fill_pending = 0;
        vdp_dma_fill(value);
    }
}

void vdp_set_reg(int reg, unsigned char value)
{
    // Mode4 is not emulated yet. Anyway, access to registers > 0xA is blocked.
    if (!BIT(vdp_regs[0x1], 2) && reg > 0xA)
        return;

    vdp_regs[reg] = value;

    // Writing a register clear the first command word
    // (see sonic3d intro wrong colors, and vdpfifotesting)
    control_code &= ~0x3;
    control_address &= ~0x3FFF;

    switch (reg)
    {
    case 0:
        if (REG0_HVLATCH && !hvcounter_latched)
        {
            hvcounter_latch = vdp_hvcounter_read_16();
            hvcounter_latched = 1;
        }
        else if (!REG0_HVLATCH && hvcounter_latched)
            hvcounter_latched = 0;
        break;
    }
}

unsigned int vdp_get_reg(int reg)
{
    return vdp_regs[reg];
}

void vdp_control_port_write(unsigned int value)
{
    if (!control_pending)
    {
        if ((value & 0xc000) == 0x8000)
        {
            int reg = (value >> 8) & 0x1f;
            unsigned char reg_value = value & 0xff;

            vdp_set_reg(reg, reg_value);
        }
        else
        {
            control_code = (control_code & 0x3c) | ((value >> 14) & 3);
            control_address = (control_address & 0xc000) | (value & 0x3fff);
            control_pending = 1;
        }
    }
    else
    {
        control_code = (control_code & 3) | ((value >> 2) & 0x3c);
        control_address = (control_address & 0x3fff) | ((value & 3) << 14);
        control_pending = 0;

        if ((control_code & 0x20))
        {
            vdp_dma_trigger();
        }
    }
}

void vdp_write_memory_8(unsigned int address, unsigned int value)
{
    switch (address & 0x1F)
    {
    case 0x11:
    case 0x13:
    case 0x15:
    case 0x17:
        // SN76489 TODO
        return;
    default:
        vdp_write_memory_16(address & ~1, (value << 8) | value);
        return;
    }
}

void vdp_write_memory_16(unsigned int address, unsigned int value)
{
    switch (address & 0x1F)
    {
    case 0x0:
    case 0x2:
        vdp_write_data_port_16(value);
        return;
    case 0x4:
    case 0x6:
        vdp_control_port_write(value);
        return;
    case 0x18:
        // VDP FIFO TEST
        return;
    case 0x1C:
        // DEBUG REGISTER
        return;
    default:
        // UNHANDLED
        printf("unhandled vdp_write(%x, %x)\n", address, value);
    }
}

unsigned int vdp_read_memory_8(unsigned int address)
{
    unsigned int ret = vdp_read_memory_16(address & ~1);
    if (address & 1)
        return ret & 0xFF;
    return ret >> 8;
}
unsigned int vdp_read_memory_16(unsigned int address)
{
    unsigned int ret;
    switch (address & 0x1F)
    {
    case 0x0:
    case 0x2:
        return vdp_read_data_port_16();
    case 0x4:
    case 0x6:
        return vdp_status;
    case 0x8:
    case 0xA:
    case 0xC:
    case 0xE:
        return vdp_hvcounter_read_16(address);
    case 0x18:
        // VDP FIFO TEST
        return 0xFFFF;
    case 0x1C:
        // DEBUG REGISTER
        return 0xFFFF;
    default:
        printf("unhandled vdp_read(%x)\n", address);
        return 0xFF;
    }
}

void vdp_dma_trigger()
{
    // Check master DMA enable, otherwise skip
    if (!REG1_DMA_ENABLED)
        return;

    switch (REG23_DMA_TYPE)
    {
    case 0:
    case 1:
        vdp_dma_m68k();
        break;

    case 2:
        // VRAM fill will trigger on next data port write
        dma_fill_pending = 1;
        break;

    case 3:
        vdp_dma_copy();
        break;
    }
}

void vdp_dma_fill(unsigned int value)
{
    int dma_length = REG19_DMA_LENGTH;

    // This address is not required for fills,
    // but it's still updated by the DMA engine.
    unsigned int dma_source = REG21_DMA_SRCADDR_LOW;

    if (dma_length == 0)
        dma_length = 0xFFFF;

    if (control_code & 0x1)
    {
        switch (control_code & 0xF)
        {
        case 0x1:
            do
            {
                VRAM_W(control_address + 1 & 0xFFFF, value >> 8);
                control_address += REG15_DMA_INCREMENT;
                dma_source++;
            } while (--dma_length);
            break;
        case 0x3: // undocumented and buggy, see vdpfifotesting
            do
            {
                CRAM[(control_address & 0x7f) >> 1] = fifo[3];
                control_address += REG15_DMA_INCREMENT;
                dma_source++;
            } while (--dma_length);
            break;
        case 0x5: // undocumented and buggy, see vdpfifotesting:
            do
            {
                VSRAM[(control_address & 0x7f) >> 1] = fifo[3];
                control_address += REG15_DMA_INCREMENT;
                dma_source++;
            } while (--dma_length);
            break;
        default:
            printf("Invalid code during DMA fill\n");
        }
    }

    // Clear DMA length at the end of transfer
    vdp_regs[19] = vdp_regs[20] = 0;

    // Update DMA source address after end of transfer
    vdp_regs[21] = dma_source & 0xFF;
    vdp_regs[22] = dma_source >> 8;
}

void vdp_dma_m68k(unsigned int value)
{
    int dma_length = REG19_DMA_LENGTH;

    // This address is not required for fills,
    // but it's still updated by the DMA engine.
    unsigned int dma_source_low = REG21_DMA_SRCADDR_LOW;
    unsigned int dma_source_high = REG23_DMA_SRCADDR_HIGH;

    if (dma_length == 0)
        dma_length = 0xFFFF;

    do
    {
        unsigned int value = m68k_read_memory_16((dma_source_high | dma_source_low) << 1);
        push_fifo(value);

        if (control_code & 0x1)
        {
            switch (control_code & 0xF)
            {
            case 0x1:
                VRAM_W((control_address)&0xFFFF, value >> 8);
                VRAM_W((control_address ^ 1) & 0xFFFF, value & 0xFF);
                break;
            case 0x3:
                CRAM[(control_address & 0x7f) >> 1] = value;
                break;
            case 0x5:
                VSRAM[(control_address & 0x7f) >> 1] = value;
                break;
            default:
                printf("Invalid code during DMA fill\n");
            }
        }
        control_address += REG15_DMA_INCREMENT;
        dma_source_low += 1;
    } while (--dma_length);

    // Update DMA source address after end of transfer
    vdp_regs[21] = dma_source_low & 0xFF;
    vdp_regs[22] = dma_source_low >> 8;

    // Clear DMA length at the end of transfer
    vdp_regs[19] = vdp_regs[20] = 0;
}

void vdp_dma_copy()
{
    int dma_length = REG19_DMA_LENGTH;
    unsigned int dma_source = REG21_DMA_SRCADDR_LOW;

    if (dma_length == 0)
        dma_source = 0xFFFF;

    do
    {
        unsigned int value = VRAM[dma_source ^ 1];
        VRAM_W((control_address ^ 1) & 0xFFFF, value);

        control_address += REG15_DMA_INCREMENT;
        dma_source++;
    } while (--dma_length);

    // Update DMA source address after end of transfer
    vdp_regs[21] = dma_source & 0xFF;
    vdp_regs[22] = dma_source >> 8;

    // Clear DMA length at the end of transfer
    vdp_regs[19] = vdp_regs[20] = 0;
}

void VRAM_W(unsigned int address, unsigned int value)
{
    VRAM[address] = value;
    // Update internal SAT Cache
    // used in Castlevania Bloodlines
    if (address >= REG5_SAT_ADDRESS && address < REG5_SAT_ADDRESS + REG5_SAT_SIZE)
        SAT_CACHE[address - REG5_SAT_ADDRESS] = value;
}

unsigned int vdp_get_status()
{
    return vdp_status;
}

unsigned short vdp_get_cram(int index)
{
    return CRAM[index & 0x3f];
}

unsigned short vdp_get_vram(unsigned char *raw_buffer, int palette)
{
    int pixel = 0;
    unsigned char temp_buffer[2048 * 64];
    unsigned char temp_buffer2[128][1024];
    for (int i = 0; i < (2048 * 32); i++)
    {
        int pix1 = VRAM[i] >> 4;
        int pix2 = VRAM[i] & 0xF;
        temp_buffer[pixel] = pix1;
        pixel++;
        temp_buffer[pixel] = pix2;
        pixel++;
    }
    for (int i = 0; i < (2048 * 64); i++)
    {
        int pixel_column = i % 8;
        int pixel_line = (int)(i / 8) % 8;
        int tile = (int)(i / 64);
        int x = pixel_column + (tile * 8) % 128;
        int y = pixel_line + ((tile * 8) / 128) * 8;
        temp_buffer2[x][y] = temp_buffer[i];
    }
    int index = 0;
    for (int y = 0; y < 1024; y++)
    {
        for (int x = 0; x < 128; x++)
        {
            raw_buffer[index + 0] = (CRAM[temp_buffer2[x][y]] >> 4) & 0xe0;
            raw_buffer[index + 1] = (CRAM[temp_buffer2[x][y]]) & 0xe0;
            raw_buffer[index + 2] = (CRAM[temp_buffer2[x][y]] << 4) & 0xe0;
            index += 4;
        }
    }
}

unsigned short vdp_get_vram_raw(unsigned char *raw_buffer)
{
    for (int pixel = 0; pixel < 0x10000; pixel++)
    {
        raw_buffer[pixel] = VRAM[pixel];
    }
}

unsigned short vdp_get_cram_raw(unsigned char *raw_buffer)
{
    for (int color = 0; color < 0x40; color++)
    {
        raw_buffer[color] = CRAM[color];
    }
}

void vdp_hvcounter_read_16(unsigned int address)
{
    /* H/V Counter */
    if (hvcounter_latched)
        return hvcounter_latch;

    int vcounter, hcounter;
    vcounter = vdp_vcounter();
    if (vdp_regs[12] & 0x01)
    {
        hcounter = 0;
    }
    else
    {
        hcounter = vdp_hcounter();
    }

    return ((vcounter & 0xFF) << 8) | (hcounter >> 1);
}

void vdp_set_hblank()
{
    vdp_status |= 4;
}
void vdp_clear_hblank()
{
    vdp_status &= ~4;
}
void vdp_set_vblank()
{
    vdp_status |= 8;
}
void vdp_clear_vblank()
{
    vdp_status &= ~8;
}

int vdp_hcounter()
{
    int mclk = m68k_cycles_run() * M68K_FREQ_DIVISOR;
    int pixclk;

    // Accurate 9-bit hcounter emulation, from timing posted here:
    // http://gendev.spritesmind.net/forum/viewtopic.php?p=17683#17683
    if (REG12_MODE_H40)
    {
        pixclk = mclk * 420 / MCYCLES_PER_LINE;
        pixclk += 0xD;
        if (pixclk >= 0x16D)
            pixclk += 0x1C9 - 0x16D;
    }
    else
    {
        pixclk = mclk * 342 / MCYCLES_PER_LINE;
        pixclk += 0xB;
        if (pixclk >= 0x128)
            pixclk += 0x1D2 - 0x128;
    }

    return pixclk & 0x1FF;
}
int vdp_vcounter()
{
    extern int lines_per_frame;
    extern int cycle_counter;
    int vc = cycle_counter / MCYCLES_PER_LINE - 1;
    if (vc > (vdp_regs[1] & 0x08 ? 262 : 234))
    {
        vc -= lines_per_frame;
    }
    return vc;
}
void vdp_clear_memory()
{
    memset(VRAM, 0, 0x10000);
    memset(CRAM, 0, 0x40);
    memset(VSRAM, 0, 0x40);
}