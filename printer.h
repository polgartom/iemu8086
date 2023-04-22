#ifndef _H_PRINT_ASM
#define _H_PRINT_ASM

#include "sim86.h"

const char *get_opcode_name(Opcode opcode);
const char *get_register_name(Register reg);

void print_flags(u16 flags);
void print_instruction(Instruction *instruction, u8 with_end_line);

#endif 
