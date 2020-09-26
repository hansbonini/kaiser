/*
 * Megadrive VDP emulation
 */

#define BIT(v, idx) (((v) >> (idx)) & 1)
#define BITS(v, idx, n) (((v) >> (idx)) & ((1 << (n)) - 1))
#define REG0_HVLATCH BIT(vdp_regs[0], 1)
#define REG0_LINE_INTERRUPT BIT(vdp_regs[0], 4)
#define REG1_PAL BIT(vdp_regs[1], 3)
#define REG1_DMA_ENABLED BIT(vdp_regs[1], 4)
#define REG1_VBLANK_INTERRUPT BIT(vdp_regs[1], 5)
#define REG1_DISP_ENABLED BIT(vdp_regs[1], 6)
#define REG2_NAMETABLE_A (BITS(vdp_regs[2], 3, 3) << 13)
#define REG3_NAMETABLE_W (BITS(vdp_regs[3], 1, 5) << 11)
#define REG4_NAMETABLE_B (BITS(vdp_regs[4], 0, 3) << 13)
#define REG5_SAT_ADDRESS ((vdp_regs[5] & (mode_h40 ? 0x7E : 0x7F)) << 9)
#define REG5_SAT_SIZE (mode_h40 ? (1 << 10) : (1 << 9))
#define REG10_LINE_COUNTER BITS(vdp_regs[10], 0, 8)
#define REG12_MODE_H40 BIT(vdp_regs[12], 0)
#define REG15_DMA_INCREMENT vdp_regs[15]
#define REG19_DMA_LENGTH (vdp_regs[19] | (vdp_regs[20] << 8))
#define REG21_DMA_SRCADDR_LOW (vdp_regs[21] | (vdp_regs[22] << 8))
#define REG23_DMA_SRCADDR_HIGH ((vdp_regs[23] & 0x7F) << 16)
#define REG23_DMA_TYPE BITS(vdp_regs[23], 6, 2)

void draw_cell_pixel(unsigned int cell, int cell_x, int cell_y, int x, int y);
void vdp_render_bg(int line, int priority);
void vdp_render_sprite(int sprite_index, int line);
void vdp_render_sprites(int line, int priority);
void vdp_render_line(int line);

void vdp_set_buffers(unsigned char *screen_buffer, unsigned char *scaled_buffer);
void vdp_debug_status(char *s);

void vdp_set_reg(int reg, unsigned char value);
unsigned int vdp_get_reg(int reg);
unsigned int vdp_read_data_port_16();
void vdp_write_data_port_16(unsigned int value);
void vdp_write_memory_8(unsigned int address, unsigned int value);
void vdp_write_memory_16(unsigned int address, unsigned int value);
unsigned int vdp_read_memory_8(unsigned int address);
unsigned int vdp_read_memory_16(unsigned int address);

unsigned int vdp_get_status();
unsigned short vdp_get_cram(int index);
unsigned short vdp_get_vram(unsigned char *raw_buffer, int palette);
unsigned short vdp_get_vram_raw(unsigned char *raw_buffer);

void vdp_set_hblank();
void vdp_clear_hblank();
void vdp_set_vblank();
void vdp_clear_vblank();
