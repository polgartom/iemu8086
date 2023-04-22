#ifndef _H_PRINTER
#define _H_PRINTER

const char *get_opcode_name(Opcode opcode);
const char *get_register_name(Register reg);
void print_flags(u16 flags);
void print_instruction(Instruction *instruction, u8 with_end_line);

#endif