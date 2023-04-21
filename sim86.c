#include "sim86.h"

#define ASMD_CURR_BYTE(_d) _d->memory.data[_d->mem_index]
u8 ASMD_NEXT_BYTE(Decoder_Context *_d) { return _d->memory.data[++_d->mem_index]; }
#define ASMD_NEXT_BYTE_WITHOUT_STEP(_d) _d->memory.data[_d->mem_index+1]
#define ASMD_CURR_BYTE_INDEX(_d) _d->mem_index

#define ARITHMETIC_OPCODE_LOOKUP(__byte, __opcode) \
     switch ((__byte >> 3) & 7) { \
        case 0: { __opcode = "add"; break; } \
        case 5: { __opcode = "sub"; break; } \
        case 7: { __opcode = "cmp"; break; } \
        default: { \
            printf("[WARNING/%s:%d]: This arithmetic instruction is not handled yet!\n", __FUNCTION__, __LINE__); \
            goto _debug_parse_end; \
        } \
    } \

////////////////////////////////////////

Memory regmem; // Remove!!

#define REG_ACCUMULATOR 0
Register_Info *get_register(u8 is_16bit, u8 reg)
{
    // [byte/word][register_index_by_table][meta_data]
    static const u32 registers[2][8][3] = {
        // BYTE (8bit)
        { 
        //   Register_index, mem_offset (byte), mem_size (byte)
            {Register_al, 0, 1}, {Register_cl, 2, 1}, {Register_dl, 4, 1}, {Register_bl, 6, 1},
            {Register_ah, 1, 2}, {Register_ch, 3, 1}, {Register_dh, 5, 1}, {Register_bh, 7, 1}
        },
        // WORD (16bit)
        {
            {Register_ax, 0, 2}, {Register_cx, 2,  2}, {Register_dx, 4,  2}, {Register_bx, 6,  2},
            {Register_sp, 8, 2}, {Register_bp, 10, 2}, {Register_si, 12, 2}, {Register_di, 14, 2}
        }
    };

    return (Register_Info *)registers[is_16bit][reg];
}

const char *get_register_name(Register reg)
{
    static const char *register_names[] = {
        "al", "cl", "dl", "bl",
        "ah", "ch", "dh", "bh",
        "ax", "cx", "dx", "bx",
        "sp", "bp", "si", "di"
    };

    return register_names[reg];
}

size_t load_binary_file_to_memory(Decoder_Context *ctx, char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("[ERROR]: Failed to open %s file. Probably it is not exists.\n", filename);
        assert(0);
    }

    fseek(fp, 0, SEEK_END);
    u32 fsize = ftell(fp);
    rewind(fp);

    assert(fsize+1 <= MAX_MEMORY_SIZE);

    ZERO_MEMORY(ctx->memory.data, fsize+1);

    fread(ctx->memory.data, fsize, 1, fp);
    fclose(fp);

    return fsize;
}

Effective_Address_Base get_address_base(u8 r_m, u8 mod) 
{
    switch (r_m) {
        case 0x00: {return Effective_Address_bx_si;}
        case 0x01: {return Effective_Address_bx_di;}
        case 0x02: {return Effective_Address_bp_si;}
        case 0x03: {return Effective_Address_bp_di;}
        case 0x04: {return Effective_Address_si;   }
        case 0x05: {return Effective_Address_di;   }
        case 0x06: {
            if (mod == 0x00) {
                return Effective_Address_direct; 
            }
            return Effective_Address_bp;
        }
        case 0x07: {return Effective_Address_bx;}
        default: {
            fprintf(stderr, "[ERROR]: Invalid effective address expression!\n");
            assert(0);
        } 
    }
}

void immediate_to_operand(Decoder_Context *ctx, Instruction_Operand *operand, u8 is_signed, u8 is_16bit, u8 immediate_depends_from_signed)
{
    operand->type = Operand_Immediate;
    s8 immediate8 = ASMD_NEXT_BYTE(ctx);

    // If this an arithmetic operation then only can be an u16 or a s8 data type based on
    // that if it is wide and not signed immediate
    if (immediate_depends_from_signed) {
        if (is_16bit && !is_signed) {
            operand->immediate_u16 = BYTE_SWAP_16BIT(immediate8, ASMD_NEXT_BYTE(ctx));
        } else {
            operand->immediate_s16 = immediate8;
        }
        return;
    }

    if (is_16bit) {
        if (!is_signed) {
            operand->immediate_u16 = BYTE_SWAP_16BIT(immediate8, ASMD_NEXT_BYTE(ctx));
        } else {
            operand->immediate_s16 = BYTE_SWAP_16BIT(immediate8, ASMD_NEXT_BYTE(ctx));
        }
    } else {
        operand->immediate_s16 = immediate8;
    }
}

void displacement_to_operand(Decoder_Context *d, Instruction_Operand *operand, u8 mod, u8 r_m)
{
    operand->type = Operand_Memory;
    operand->address.base = get_address_base(r_m, mod);
    operand->address.displacement = 0;

    if ((mod == 0x00 && r_m == 0x06) || mod == 0x02) { 
        operand->address.displacement = (s16)(BYTE_SWAP_16BIT(ASMD_NEXT_BYTE(d), ASMD_NEXT_BYTE(d)));
    } 
    else if (mod == 0x01) { 
        operand->address.displacement = (s8)ASMD_NEXT_BYTE(d);
    }
}

void register_memory_to_from_decode(Decoder_Context *d, u8 reg_dir)
{
    u8 byte = ASMD_NEXT_BYTE(d);
    Instruction *instruction = d->instruction;

    u8 mod = (byte >> 6) & 0x03;
    u8 reg = (byte >> 3) & 0x07;
    u8 r_m = byte & 0x07;
    
    Register src;
    Register dest;

    if (mod == 0x03) { 
        dest = r_m;
        src = reg;
        if (reg_dir == REG_IS_DEST) {
            dest = reg;
            src = r_m;
        }

        instruction->operands[0].type = Operand_Register;
        instruction->operands[1].type = Operand_Register;
        instruction->operands[0].reg = dest;
        instruction->operands[1].reg = src;

        return;
    }

    Instruction_Operand a_operand;
    displacement_to_operand(d, &a_operand, mod, r_m);

    Instruction_Operand b_operand;
    b_operand.type = Operand_Register;
    b_operand.reg = reg;

    if (reg_dir == REG_IS_DEST) {
        instruction->operands[0] = b_operand;
        instruction->operands[1] = a_operand;
    } else {
        instruction->operands[0] = a_operand;
        instruction->operands[1] = b_operand;
    }
}

void immediate_to_register_memory_decode(Decoder_Context *d, s8 is_signed, u8 is_16bit, u8 immediate_depends_from_signed)
{
    char byte = ASMD_NEXT_BYTE(d);
    Instruction *instruction = d->instruction;

    u8 mod = (byte >> 6) & 0x03;
    u8 r_m = byte & 0x07;

    if (mod == 0x03) {
        instruction->operands[0].type = Operand_Register;
        instruction->operands[0].reg  = r_m;
    } 
    else {
        displacement_to_operand(d, &instruction->operands[0], mod, r_m);
    }

    immediate_to_operand(d, &instruction->operands[1], is_signed, is_16bit, immediate_depends_from_signed);
}

void print_flags(u16 flags)
{
    if (flags & F_SIGNED) {
        printf("S");
    }

    if (flags & F_ZERO) {
        printf("Z");
    }
}

void print_instruction(Instruction *instruction, u8 with_end_line)
{
    FILE *dest = stdout;

    fprintf(dest, "[0x%02x]\t%s", instruction->mem_offset, instruction->opcode);
    
    const char *separator = " ";
    for (u8 j = 0; j < 2; j++) {
        Instruction_Operand *op = &instruction->operands[j];
        if (op->type == Operand_None) {
            continue;
        }
        
        fprintf(dest, "%s", separator);
        separator = ", ";

        switch (op->type) {
            case Operand_None: {
                break;
            }
            case Operand_Register: {
                Register_Info *reg = get_register(instruction->flags & FLAG_IS_16BIT, op->reg);
                fprintf(dest, "%s", get_register_name(reg->reg));

                break;
            }
            case Operand_Memory: {
                if (instruction->operands[0].type != Operand_Register) {
                    fprintf(dest, "%s ", (instruction->flags & FLAG_IS_16BIT) ? "word" : "byte");
                }

                char const *r_m_base[] = {"","bx+si","bx+di","bp+si","bp+di","si","di","bp","bx"};

                fprintf(dest, "[%s", r_m_base[op->address.base]);
                if (op->address.displacement) {
                    fprintf(dest, "%+d", op->address.displacement);
                }
                fprintf(dest, "]");

                break;
            }
            case Operand_Immediate: {
                if ((instruction->flags & FLAG_IS_16BIT) && !(instruction->flags & FLAG_IS_SIGNED)) {
                    fprintf(dest, "%d", op->immediate_u16);
                } else {
                    fprintf(dest, "%d", op->immediate_s16);
                }

                break;
            }
            case Operand_Relative_Immediate: {
                fprintf(dest, "$%+d", op->immediate_s16);

                break;
            }
            default: {
                fprintf(stderr, "[WARNING]: I found a not operand at print out!\n");
            }
        }

    }

    if (with_end_line) {
        fprintf(dest, "\n");
    }

}

s32 get_data_from_register(Register_Info *reg)
{
    u16 lower_mem_index = reg->mem_offset;
    if (reg->mem_size == 2) {
        s16 val_lo = regmem.data[lower_mem_index] << 0;
        s16 val_hi = regmem.data[lower_mem_index+1] << 8;

        return (val_hi | val_lo);
    }
 
    return regmem.data[lower_mem_index];
}

void set_data_to_from_register(Register_Info *dest, Register_Info *src) 
{
    s32 src_value = get_data_from_register(src);

    u16 lower_mem_index = dest->mem_offset;
    if (dest->mem_size == 2) {
        regmem.data[lower_mem_index] = ((src_value >> 0) & 0xFF);
        regmem.data[lower_mem_index+1] = ((src_value >> 8) & 0xFF);

        return;
    }

    regmem.data[lower_mem_index] = src_value;
}

void set_data_to_register_from_immediate(Register_Info *dest, s32 immediate) 
{
    u16 lower_mem_index = dest->mem_offset;

    if (dest->mem_size == 2) {
        regmem.data[lower_mem_index] = ((immediate >> 0) & 0xFF);
        regmem.data[lower_mem_index+1] = ((immediate >> 8) & 0xFF);

        return;
    }

    regmem.data[lower_mem_index] = immediate;
}

void execute_instruction(Decoder_Context *ctx) 
{
    Instruction *i = ctx->instruction;

    Instruction_Operand dest_op = i->operands[0];
    Register_Info *dest_reg = (Register_Info *)get_register(i->flags & FLAG_IS_16BIT, dest_op.reg);
    const char *dest_reg_name = get_register_name(dest_reg->reg);

    s32 dest_original_value = get_data_from_register(dest_reg);
    s32 result_value = dest_original_value;
    u32 ip_data_before = ctx->ip_data;

    if (STR_EQUAL(i->opcode, "mov")) {
        Instruction_Operand src_op = i->operands[1];

        assert(dest_op.type == Operand_Register); // @Todo: Handle more!

        if (src_op.type == Operand_Register) {
            Register_Info *src_reg = get_register(i->flags & FLAG_IS_16BIT, src_op.reg);
            set_data_to_from_register(dest_reg, src_reg);

        } else if (src_op.type == Operand_Immediate) {
            
            s32 immediate = 0;
            if (i->flags & FLAG_IS_SIGNED) {
                immediate = src_op.immediate_s16;
            } else {
                immediate = src_op.immediate_u16;
            }

            set_data_to_register_from_immediate(dest_reg, immediate);
        }

        result_value = get_data_from_register(dest_reg);

        ctx->ip_data = ctx->ip_data + i->size;
        printf(" ; %s: %#02x -> %#02x | ip: %#02x (%d) -> %#02x (%d)", dest_reg_name, dest_original_value, result_value, ip_data_before, ip_data_before, ctx->ip_data, ctx->ip_data);
    } 
    else if (STR_EQUAL(i->opcode, "add") || STR_EQUAL(i->opcode, "sub") || STR_EQUAL(i->opcode, "cmp")) {
        Instruction_Operand src_op = i->operands[1];

        Register_Info *dest_reg = get_register(i->flags & FLAG_IS_16BIT, dest_op.reg);

        if (src_op.type == Operand_Register) {
            Register_Info *src_reg = get_register(i->flags & FLAG_IS_16BIT, src_op.reg);

            if (STR_EQUAL(i->opcode, "add")) {
                s32 val = get_data_from_register(dest_reg) + get_data_from_register(src_reg);
                set_data_to_register_from_immediate(dest_reg, val);

                result_value = get_data_from_register(dest_reg);
            } 
            else if (STR_EQUAL(i->opcode, "sub")) {
                s32 val = get_data_from_register(dest_reg) - get_data_from_register(src_reg);
                set_data_to_register_from_immediate(dest_reg, val);
                
                result_value = get_data_from_register(dest_reg);
            }
            else if (STR_EQUAL(i->opcode, "cmp")) {
                // Here we set the flags by the result value?
                result_value = get_data_from_register(dest_reg) - get_data_from_register(src_reg);
            }

        } else if (src_op.type == Operand_Immediate) {
            s32 immediate = 0;
            if (i->flags & FLAG_IS_SIGNED) {
                immediate = src_op.immediate_s16;
            } else {
                immediate = src_op.immediate_u16;
            }

            if (STR_EQUAL(i->opcode, "add")) {
                s32 val = get_data_from_register(dest_reg) + immediate;
                set_data_to_register_from_immediate(dest_reg, val);

                result_value = get_data_from_register(dest_reg);
            } 
            else if (STR_EQUAL(i->opcode, "sub")) {
                s32 val = get_data_from_register(dest_reg) - immediate;
                set_data_to_register_from_immediate(dest_reg, val);

                result_value = get_data_from_register(dest_reg);
            } 
            else if (STR_EQUAL(i->opcode, "cmp")) {
                // Here we set the flags by the result value?
                result_value = get_data_from_register(dest_reg) - immediate; 
            }

        } else {
            printf("NOT HANDLED!\n");
            assert(0);
        }

        // Clear out flags
        u16 new_flags = ctx->flags;
        new_flags &= (~(F_SIGNED|F_ZERO));

        if (result_value == 0) {
            new_flags |= F_ZERO;
        } else if (result_value < 0) {
            // @Todo: check the highest bit
            new_flags |= F_SIGNED;
        }

        // current flags
        printf(" | flags: [");
        print_flags(ctx->flags);
        printf("] -> [");
        print_flags(new_flags);
        printf("]");

        ctx->flags = new_flags;

        ctx->ip_data = ctx->ip_data + i->size;
        printf(" ; %s: %#02x -> %#02x | ip: %#02x (%d) -> %#02x (%d)", dest_reg_name, dest_original_value, result_value, ip_data_before, ip_data_before, ctx->ip_data, ctx->ip_data);

    } 
    else if (STR_EQUAL(i->opcode, "jnz")) {
        if (ctx->flags & F_ZERO) {
            ctx->ip_data += i->size;
        } else {
            ctx->ip_data += i->operands[0].immediate_s16;
        }

        printf(" ; ip: %#02x (%d) -> %#02x (%d)", ip_data_before, ip_data_before, ctx->ip_data, ctx->ip_data);
    }
    else {
        printf("!!!!\n");
        assert(0);
    }

    printf("\n");
}

void try_to_decode(Decoder_Context *ctx)
{
    do {
        u8 byte = ASMD_CURR_BYTE(ctx);
        u8 reg_dir = 0;
        u8 is_16bit = 0;

        u32 instruction_byte_start_offset = ctx->mem_index;

        ctx->instruction = ALLOC_MEMORY(Instruction);
        ctx->instruction->mem_offset = ctx->mem_index;

        ctx->instructions[ctx->mem_index] = ctx->instruction;
        
        // ARITHMETIC
        if (((byte >> 6) & 7) == 0) {
            ARITHMETIC_OPCODE_LOOKUP(byte, ctx->instruction->opcode);

            if (((byte >> 1) & 3) == 2) {
                // Immediate to accumulator                
                is_16bit = byte & 1;
                if (is_16bit) {
                    ctx->instruction->flags |= FLAG_IS_16BIT;
                }

                ctx->instruction->operands[0].type = Operand_Register;
                ctx->instruction->operands[0].reg = REG_ACCUMULATOR;

                immediate_to_operand(ctx, &ctx->instruction->operands[1], 0, is_16bit, 1);
            }
            else if (((byte >> 2) & 1) == 0) {
                reg_dir = (byte >> 1) & 1;

                is_16bit = byte & 1;
                if (is_16bit) {
                    ctx->instruction->flags |= FLAG_IS_16BIT;
                }

                register_memory_to_from_decode(ctx, reg_dir);
            } else {
                fprintf(stderr, "[ERROR]: Invalid opcode!\n");
                assert(0);
            }
        }
        else if (((byte >> 2) & 63) == 32) {
            ARITHMETIC_OPCODE_LOOKUP(ASMD_NEXT_BYTE_WITHOUT_STEP(ctx), ctx->instruction->opcode);

            is_16bit = byte & 1;
            if (is_16bit) {
                ctx->instruction->flags |= FLAG_IS_16BIT;
            }

            u8 is_signed = (byte >> 1) & 1;
            if (is_signed) {
                ctx->instruction->flags |= FLAG_IS_SIGNED;
            }

            immediate_to_register_memory_decode(ctx, is_signed, is_16bit, 1);
        }
        // MOV
        else if (((byte >> 2) & 63) == 34) {
            ctx->instruction->opcode = "mov";
            
            reg_dir = (byte >> 1) & 1;
            is_16bit = byte & 1;
            if (is_16bit) {
                ctx->instruction->flags |= FLAG_IS_16BIT;
            }

            register_memory_to_from_decode(ctx, reg_dir);
        }
        else if (((byte >> 1) & 127) == 99) {
            ctx->instruction->opcode = "mov";

            char second_byte = ASMD_NEXT_BYTE_WITHOUT_STEP(ctx);
            assert(((second_byte >> 3) & 7) == 0);

            is_16bit = byte & 1;
            if (is_16bit) {
                ctx->instruction->flags |= FLAG_IS_16BIT;
            }
            ctx->instruction->flags |= FLAG_IS_SIGNED;

            immediate_to_register_memory_decode(ctx, 1, is_16bit, 0);
        }
        else if (((byte >> 4) & 0x0F) == 0x0B) {
            ctx->instruction->opcode = "mov";

            u8 reg = byte & 0x07;

            is_16bit = (byte >> 3) & 1;
            if (is_16bit) {
                ctx->instruction->flags |= FLAG_IS_16BIT;
            }
            ctx->instruction->flags |= FLAG_IS_SIGNED;

            ctx->instruction->operands[0].type = Operand_Register;
            ctx->instruction->operands[0].reg = reg;

            immediate_to_operand(ctx, &ctx->instruction->operands[1], 1, is_16bit, 0);
        }
        else if (((byte >> 2) & 0x3F) == 0x28) {
            ctx->instruction->opcode = "mov";

            // Memory to/from accumulator
            is_16bit = byte & 1;
            if (is_16bit) {
                ctx->instruction->flags |= FLAG_IS_16BIT;
            }

            ctx->instruction->opcode = "mov";

            Instruction_Operand a_operand;
            a_operand.type = Operand_Memory;
            a_operand.address.base = Effective_Address_direct;

            a_operand.address.displacement = (s8)ASMD_NEXT_BYTE(ctx);
            if (is_16bit) {
                a_operand.address.displacement = (s16)BYTE_SWAP_16BIT(a_operand.address.displacement, ASMD_NEXT_BYTE(ctx));
            }

            Instruction_Operand b_operand;
            b_operand.type = Operand_Register;
            b_operand.reg = REG_ACCUMULATOR;

            reg_dir = (byte >> 1) & 1;
            if (reg_dir == 0) {
                ctx->instruction->operands[0] = b_operand;
                ctx->instruction->operands[1] = a_operand;
            } else {
                ctx->instruction->operands[0] = a_operand;
                ctx->instruction->operands[1] = b_operand;
            }
            
        }
        // Return from CALL (jumps)
        else if (((byte >> 4) & 15) == 0x07) {
            s8 ip_inc8 = ASMD_NEXT_BYTE(ctx); // 8 bit signed increment to instruction pointer
            const char *opcode = NULL;
            switch (byte & 0x0F) {
                case 0b0000: { opcode = "jo";  break;               }
                case 0b1000: { opcode = "js";  break;               }
                case 0b0010: { opcode = "jb";  break; /* or jnae */ }
                case 0b0100: { opcode = "je";  break; /* or jz */   }
                case 0b0110: { opcode = "jbe"; break; /* or jna */  }
                case 0b1010: { opcode = "jp";  break; /* or jpe */  }
                case 0b0101: { opcode = "jnz"; break; /* or jne */  }
                case 0b1100: { opcode = "jl";  break; /* or jnge */ }
                case 0b1110: { opcode = "jle"; break; /* or jng */  }
                case 0b1101: { opcode = "jnl"; break; /* or jge */  }
                case 0b1111: { opcode = "jg";  break; /* or jnle */ }
                case 0b0011: { opcode = "jae"; break; /* or jnle */ }
                case 0b0111: { opcode = "ja";  break; /* or jnbe */ }
                case 0b1011: { opcode = "jnp"; break; /* or jpo */  }
                case 0b0001: { opcode = "jno"; break;               }
                case 0b1001: { opcode = "jns"; break;               }
                default: {
                    fprintf(stderr, "[ERROR]: This invalid opcode\n");
                    assert(0);
                }
            }

            ctx->instruction->opcode = opcode;

            // We have to add +2, because the relative displacement (offset) start from the second byte (ip_inc8)
            //  of instruction. 
            ctx->instruction->operands[0].type = Operand_Relative_Immediate;
            ctx->instruction->operands[0].immediate_u16 = ip_inc8+2;
        }
        else if (((byte >> 4) & 0x0F) == 0b1110) {
            s8 ip_inc8 = ASMD_NEXT_BYTE(ctx);
            const char *opcode = NULL;
            switch (byte & 0x0F) {
                case 0b0010: { opcode = "loop"; break;                    }
                case 0b0001: { opcode = "loopz"; break;  /* or loope */   }
                case 0b0000: { opcode = "loopnz"; break; /* or loopne */  }
                case 0b0011: { opcode = "jcxz"; break;                    }
                default: {
                    fprintf(stderr, "[ERROR]: This is invalid opcode\n");
                    assert(0);
                }
            }

            ctx->instruction->opcode = opcode;

            // We have to add +2, because the relative displacement (offset) start from the second byte (ip_inc8)
            //  of instruction. 
            ctx->instruction->operands[0].type = Operand_Relative_Immediate;
            ctx->instruction->operands[0].immediate_u16 = ip_inc8+2;
        }
        else {
            fprintf(stderr, "[WARNING]: Instruction is not handled!\n");
            break;
        }

        ctx->mem_index++;

        ctx->instruction->size = ctx->mem_index - instruction_byte_start_offset; 

    } while(ctx->memory.size != ctx->mem_index);

_debug_parse_end:;

}

void run(Decoder_Context *ctx)
{
    do {
        Instruction *instruction = ctx->instructions[ctx->ip_data];
        ctx->instruction = instruction;

        print_instruction(instruction, 0);

        execute_instruction(ctx);

    } while (ctx->instructions[ctx->ip_data]);
}

int main(int argc, char **argv)
{
    if (argc < 2 || STR_LEN(argv[1]) == 0) {
        fprintf(stderr, "No binary file specified!\n");
    }
    printf("\nbinary: %s\n\n", argv[1]);

    Decoder_Context ctx = {};

    ctx.memory.size = load_binary_file_to_memory(&ctx, argv[1]);

    regmem.size = 20;
    ZERO_MEMORY(regmem.data, regmem.size);

    try_to_decode(&ctx);
    run(&ctx);

    return 0;
}