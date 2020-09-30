OS := $(shell uname)
WARNINGS = -Wall -pedantic -Wno-unused-function
CFLAGS = $(WARNINGS) -c -Im68k -I. -O2 --std=c99 -fPIC
CFLAGS_M68K = $(WARNINGS) -c -Im68k -I. -O2 --std=c99 -fPIC
LDFLAGS = -shared

LIB_MUSASHI_DIR = libs/Musashi
LIB_Z80_DIR = libs/Z80
LIB_NUKEDOPN2_DIR = libs/NukedOPN2
LIB_HQX_DIR = libs/hqx
LIB_IL_DIR  = libs/IL

ifeq ($(TARGET),Windows)
LIB_MUSASHI_MAKE = m68kmake.exe
CORE_NAME = core.dll
else
LIB_MUSASHI_MAKE = m68kmake
CORE_NAME = core.so
endif

ifdef COVERAGE
ifeq ($(OS),Darwin)
	CFLAGS += -fprofile-instr-generate -fcoverage-mapping
	LDFLAGS += -fprofile-instr-generate
else
	CFLAGS += -fprofile-arcs -ftest-coverage
	LDFLAGS += -lgcov
endif
endif


all: core

clean:
	rm $(CORE_NAME) $(LIB_MUSASHI_DIR)/*.o $(LIB_MUSASHI_DIR)/softfloat/*.o $(LIB_MUSASHI_DIR)/m68kops.h $(LIB_MUSASHI_DIR)/m68kmake hardware/apu/*.o hardware/bus/*.o  hardware/cpu/*.o hardware/io/*.o hardware/filters/*.o hardware/vdp/*.o $(LIB_HQX_DIR)/src/init.o $(LIB_HQX_DIR)/src/hq2x.o $(LIB_HQX_DIR)/src/hq3x.o $(LIB_HQX_DIR)/src/hq4x.o $(LIB_Z80_DIR)/*.o $(LIB_NUKEDOPN2_DIR)/ym3438.o

core: $(LIB_MUSASHI_DIR)/m68kcpu.o $(LIB_MUSASHI_DIR)/m68kops.o $(LIB_MUSASHI_DIR)/m68kdasm.o $(LIB_MUSASHI_DIR)/softfloat/softfloat.o hardware/cpu/m68k.o hardware/vdp/sega3155313.o hardware/bus/sega3155308.o hardware/io/sega3155345.o hardware/filters/scale.o hardware/apu/z80.o hardware/apu/ym2612.o $(LIB_Z80_DIR)/Z80.o $(LIB_NUKEDOPN2_DIR)/ym3438.o
		@echo "Linking $(CORE_NAME)"
		@$(LD) $(LIB_MUSASHI_DIR)/m68kcpu.o $(LIB_MUSASHI_DIR)/m68kops.o $(LIB_MUSASHI_DIR)/m68kdasm.o $(LIB_MUSASHI_DIR)/softfloat/softfloat.o hardware/cpu/m68k.o hardware/vdp/sega3155313.o hardware/bus/sega3155308.o hardware/io/sega3155345.o hardware/filters/scale.o hardware/apu/z80.o hardware/apu/ym2612.o $(LIB_Z80_DIR)/Z80.o $(LIB_NUKEDOPN2_DIR)/ym3438.o $(LDFLAGS) -o $(CORE_NAME)

%.o: %.c
		@echo "Compiling $<"
		$(CC) $(CFLAGS) $^ -std=c99 -o $@

$(LIB_NUKEDOPN2_DIR)/ym3438.o: $(LIB_NUKEDOPN2_DIR)/ym3438.c
		@echo "Compiling $(LIB_NUKEDOPN2_DIR)/ym3438.o"
		$(CC) $(CFLAGS_M68K) $(LIB_NUKEDOPN2_DIR)/ym3438.c -o $(LIB_NUKEDOPN2_DIR)/ym3438.o

$(LIB_Z80_DIR)/Z80.o: $(LIB_Z80_DIR)/Codes.h $(LIB_Z80_DIR)/CodesED.h $(LIB_Z80_DIR)/CodesCB.h $(LIB_Z80_DIR)/CodesXX.h $(LIB_Z80_DIR)/Tables.h $(LIB_Z80_DIR)/CodesXCB.h $(LIB_Z80_DIR)/Z80.h $(LIB_Z80_DIR)/Debug.c $(LIB_Z80_DIR)/Z80.c
		@echo "Compiling $(LIB_Z80_DIR)/Z80.o"
		$(CC) $(CFLAGS_M68K) -DDEBUG $(LIB_Z80_DIR)/Z80.c -o $(LIB_Z80_DIR)/Z80.o

$(LIB_MUSASHI_DIR)/m68kcpu.o: $(LIB_MUSASHI_DIR)/m68kops.h $(LIB_MUSASHI_DIR)/m68kmmu.h $(LIB_MUSASHI_DIR)/m68kfpu.c $(LIB_MUSASHI_DIR)/m68kcpu.c
		@echo "Compiling $(LIB_MUSASHI_DIR)/m68kcpu.o"
		$(CC) $(CFLAGS_M68K) $(LIB_MUSASHI_DIR)/m68kcpu.c -o $(LIB_MUSASHI_DIR)/m68kcpu.o

$(LIB_MUSASHI_DIR)/m68kdasm.o: $(LIB_MUSASHI_DIR)/m68kdasm.c $(LIB_MUSASHI_DIR)/m68k.h $(LIB_MUSASHI_DIR)/m68kconf.h
		@echo "Compiling $(LIB_MUSASHI_DIR)/m68kdasm.o"
		@$(CC) $(CFLAGS_M68K) $(LIB_MUSASHI_DIR)/m68kdasm.c -o $(LIB_MUSASHI_DIR)/m68kdasm.o

$(LIB_MUSASHI_DIR)/softfloat/softfloat.o: $(LIB_MUSASHI_DIR)/m68kcpu.h $(LIB_MUSASHI_DIR)/m68k.h $(LIB_MUSASHI_DIR)/m68kconf.h $(LIB_MUSASHI_DIR)/softfloat/milieu.h $(LIB_MUSASHI_DIR)/softfloat/softfloat.h $(LIB_MUSASHI_DIR)/softfloat/softfloat.c
		@echo "Compiling $(LIB_MUSASHI_DIR)/softfloat/softfloat.o"
		@$(CC) $(CFLAGS_M68K) $(LIB_MUSASHI_DIR)/softfloat/softfloat.c -o $(LIB_MUSASHI_DIR)/softfloat/softfloat.o 

$(LIB_MUSASHI_DIR)/m68kops.o: $(LIB_MUSASHI_DIR)/$(LIB_MUSASHI_MAKE) $(LIB_MUSASHI_DIR)/m68kops.h $(LIB_MUSASHI_DIR)/m68kops.c $(LIB_MUSASHI_DIR)/m68k.h $(LIB_MUSASHI_DIR)/m68kconf.h
		@echo "Compiling $(LIB_MUSASHI_DIR)/m68kops.o"
		@$(CC) $(CFLAGS_M68K) $(LIB_MUSASHI_DIR)/m68kops.c -o $(LIB_MUSASHI_DIR)/m68kops.o

$(LIB_MUSASHI_DIR)/m68kops.h: $(LIB_MUSASHI_DIR)/$(LIB_MUSASHI_MAKE)
		@echo "Generating $(LIB_MUSASHI_DIR)/m68kops.h"
		@$(LIB_MUSASHI_DIR)/$(LIB_MUSASHI_MAKE) libs/Musashi $(LIB_MUSASHI_DIR)/m68k_in.c

$(LIB_MUSASHI_DIR)/$(LIB_MUSASHI_MAKE):
		@echo "Building for $(TARGET)"
		@echo "Compiling $(LIB_MUSASHI_DIR)/m68kmake"
		@$(CC) $(WARNINGS) $(LIB_MUSASHI_DIR)/m68kmake.c -o $(LIB_MUSASHI_DIR)/$(LIB_MUSASHI_MAKE)
