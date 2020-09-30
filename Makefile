OS := $(shell uname)

CC =     cc
WARNINGS = -Wall -pedantic -Wno-unused-function
CFLAGS = $(WARNINGS) -c -Im68k -I. -O2 --std=c99 -fPIC
CFLAGS_M68K = $(WARNINGS) -c -Im68k -I. -O2 --std=c99 -fPIC
LDFLAGS = -shared

ifdef COVERAGE
ifeq ($(OS),Darwin)
	CFLAGS += -fprofile-instr-generate -fcoverage-mapping
	LDFLAGS += -fprofile-instr-generate
else
	CFLAGS += -fprofile-arcs -ftest-coverage
	LDFLAGS += -lgcov
endif
endif

all: core.so

clean:
	rm core.so libs/Musashi/*.o libs/Musashi/softfloat/*.o libs/Musashi/m68kops.h libs/Musashi/m68kmake hardware/apu/*.o hardware/bus/*.o  hardware/cpu/*.o hardware/io/*.o hardware/filters/*.o hardware/vdp/*.o libs/hqx/src/*.o libs/Z80/*.o libs/NukedOPN2/ym3438.o

core.so: libs/Musashi/m68k.h libs/Musashi/m68kcpu.o libs/Musashi/m68kops.o libs/Musashi/m68kdasm.o libs/Musashi/softfloat/softfloat.o hardware/cpu/m68k.o hardware/vdp/sega3155313.o hardware/bus/sega3155308.o hardware/io/sega3155345.o hardware/filters/scale.o hardware/apu/z80.o hardware/apu/ym2612.o libs/hqx/src/init.o libs/hqx/src/hq2x.o libs/hqx/src/hq3x.o libs/hqx/src/hq4x.o libs/Z80/Z80.o libs/NukedOPN2/ym3438.o
		@echo "Linking core.so"
		@$(CC) libs/Musashi/m68kcpu.o libs/Musashi/m68kops.o libs/Musashi/m68kdasm.o libs/Musashi/softfloat/softfloat.o hardware/cpu/m68k.o hardware/vdp/sega3155313.o hardware/bus/sega3155308.o hardware/io/sega3155345.o hardware/filters/scale.o hardware/apu/z80.o hardware/apu/ym2612.o libs/hqx/src/init.o libs/hqx/src/hq2x.o libs/hqx/src/hq3x.o libs/hqx/src/hq4x.o libs/Z80/Z80.o libs/NukedOPN2/ym3438.o $(LDFLAGS) -o core.so

%.o: %.c
		@echo "Compiling $<"
		$(CC) $(CFLAGS) $^ -std=c99 -o $@

libs/NukedOPN2/ym3438.o: libs/NukedOPN2/ym3438.c
		@echo "Compiling libs/NukedOPN2/ym3438.c"
		$(CC) $(CFLAGS_M68K) libs/NukedOPN2/ym3438.c -o libs/NukedOPN2/ym3438.o

libs/Z80/Z80.o: libs/Z80/Codes.h libs/Z80/CodesED.h libs/Z80/CodesCB.h libs/Z80/CodesXX.h libs/Z80/Tables.h libs/Z80/CodesXCB.h libs/Z80/Z80.h libs/Z80/Debug.c libs/Z80/Z80.c
		@echo "Compiling libs/Z80/Z80.c"
		$(CC) $(CFLAGS_M68K) -DDEBUG libs/Z80/Z80.c -o libs/Z80/Z80.o

libs/Musashi/m68kcpu.o: libs/Musashi/m68kops.h libs/Musashi/m68kmmu.h libs/Musashi/m68kfpu.c libs/Musashi/m68kcpu.c
		@echo "Compiling libs/Musashi/m68kcpu.c"
		$(CC) $(CFLAGS_M68K) libs/Musashi/m68kcpu.c -o libs/Musashi/m68kcpu.o

libs/Musashi/m68kdasm.o: libs/Musashi/m68kdasm.c libs/Musashi/m68k.h libs/Musashi/m68kconf.h
		@echo "Compiling libs/Musashi/m68kdasm.c"
		@$(CC) $(CFLAGS_M68K) libs/Musashi/m68kdasm.c -o libs/Musashi/m68kdasm.o

libs/Musashi/softfloat/softfloat.o: libs/Musashi/m68kcpu.h libs/Musashi/m68k.h libs/Musashi/m68kconf.h libs/Musashi/softfloat/milieu.h libs/Musashi/softfloat/softfloat.h libs/Musashi/softfloat/softfloat.c
		@echo "Compiling libs/Musashi/softfloat/softfloat.c"
		@$(CC) $(CFLAGS_M68K) libs/Musashi/softfloat/softfloat.c -o libs/Musashi/softfloat/softfloat.o 

libs/Musashi/m68kops.o: libs/Musashi/m68kmake libs/Musashi/m68kops.h libs/Musashi/m68kops.c libs/Musashi/m68k.h libs/Musashi/m68kconf.h
		@echo "Compiling libs/Musashi/m68kops.c"
		@$(CC) $(CFLAGS_M68K) libs/Musashi/m68kops.c -o libs/Musashi/m68kops.o

libs/Musashi/m68kops.h: libs/Musashi/m68kmake
		@echo "Generating libs/Musashi/m68kops.h"
		@libs/Musashi/m68kmake libs/Musashi libs/Musashi/m68k_in.c > /dev/null

libs/Musashi/m68kmake: libs/Musashi/m68kmake.c libs/Musashi/m68k_in.c
		@echo "Compiling libs/Musashi/m68kmake"
		@$(CC) $(WARNINGS) libs/Musashi/m68kmake.c -o libs/Musashi/m68kmake

