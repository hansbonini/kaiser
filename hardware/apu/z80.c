#include <libs/Z80/Z80.h>
#include "z80.h"
#include "hardware/bus/sega3155308.h"

#define M68K_FREQ_DIVISOR   7
#define Z80_FREQ_DIVISOR    14

int bus_ack = 0;
int reset = 0;
int zclk = 0;
int initialized = 0;

unsigned char *Z80_RAM;
static Z80 cpu;

void ResetZ80(register Z80 *R);

/** DAsm() ***************************************************/
/** DAsm() will disassemble the code at adress A and put    **/
/** the output text into S. It will return the number of    **/
/** bytes disassembled.                                     **/
/*************************************************************/
int DAsm(char *S,word A)
{
  char R[128],H[10],C,*P;
  const char *T;
  byte J,Offset;
  word B;

  Offset=0;
  B=A;
  C='\0';
  J=0;

  switch(RdZ80(B))
  {
    case 0xCB: B++;T=MnemonicsCB[RdZ80(B++)];break;
    case 0xED: B++;T=MnemonicsED[RdZ80(B++)];break;
    case 0xDD: B++;C='X';
               if(RdZ80(B)!=0xCB) T=MnemonicsXX[RdZ80(B++)];
               else
               { B++;Offset=RdZ80(B++);J=1;T=MnemonicsXCB[RdZ80(B++)]; }
               break;
    case 0xFD: B++;C='Y';
               if(RdZ80(B)!=0xCB) T=MnemonicsXX[RdZ80(B++)];
               else
               { B++;Offset=RdZ80(B++);J=1;T=MnemonicsXCB[RdZ80(B++)]; }
               break;
    default:   T=Mnemonics[RdZ80(B++)];
  }

  if(P=strchr(T,'^'))
  {
    strncpy(R,T,P-T);R[P-T]='\0';
    sprintf(H,"%02X",RdZ80(B++));
    strcat(R,H);strcat(R,P+1);
  }
  else strcpy(R,T);
  if(P=strchr(R,'%')) *P=C;

  if(P=strchr(R,'*'))
  {
    strncpy(S,R,P-R);S[P-R]='\0';
    sprintf(H,"%02X",RdZ80(B++));
    strcat(S,H);strcat(S,P+1);
  }
  else
    if(P=strchr(R,'@'))
    {
      strncpy(S,R,P-R);S[P-R]='\0';
      if(!J) Offset=RdZ80(B++);
      strcat(S,Offset&0x80? "-":"+");
      J=Offset&0x80? 256-Offset:Offset;
      sprintf(H,"%02X",J);
      strcat(S,H);strcat(S,P+1);
    }
    else
      if(P=strchr(R,'#'))
      {
        strncpy(S,R,P-R);S[P-R]='\0';
        sprintf(H,"%04X",RdZ80(B)+256*RdZ80(B+1));
        strcat(S,H);strcat(S,P+1);
        B+=2;
      }
      else strcpy(S,R);

  return(B-A);
}

void z80_init() {
    cpu.IPeriod = 1;
    cpu.ICount = 0;
    cpu.Trace = 0;
    cpu.Trap = 0x0009;
    ResetZ80(&cpu);
}

void z80_pulse_reset()
{
    ResetZ80(&cpu);
    reset=0;
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
    initialized = 1;
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
            reset = 1;
            z80_pulse_reset();
        }
        else
        {
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
        return 0x00 | !reset;
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
    WrZ80(address+1, value &0xFF);
}
unsigned int z80_read_memory_16(unsigned int address)
{
    unsigned int value = RdZ80(address) << 8 | RdZ80(address);
    return value;
}

unsigned int z80_disassemble(unsigned char *screen_buffer, unsigned int address) {
    int pc;
    pc = DAsm(screen_buffer, address);
    return pc;
}

word z80_get_reg(int reg_i) {
    switch(reg_i) {
        case 0: return cpu.AF.W; break;
        case 1: return cpu.BC.W; break;
        case 2: return cpu.DE.W; break;
        case 3: return cpu.HL.W; break;
        case 4: return cpu.IX.W; break;
        case 5: return cpu.IY.W; break;
        case 6: return cpu.PC.W; break;
        case 7: return cpu.SP.W; break;
    }
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
void DebugZ80(register Z80 *R) {}