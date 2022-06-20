//Nintendo DS CPU: ARM946E-S (T, E, M variants included)
//Architecture: ARMv5TE
//ARM version: 5
//THUMB version: 2

/*
Instructions available in ARMv6 but not ARMv5TE:

ARM: BXJ, CPS, CPY, MCRR2, MRRC2, PKH, QADD16, QADD8, QADDSUBX, QSUB16, QSUB8, QSUBADDX, REV, RFE, SADD, SEL, SETEND, SHADD, SHSUB, SMLAD,
SMLALD, SMLSD, SMLSLD, SMMLA, SMMLS, SMMUL, SMUAD, SMUSD, SRS, SSAT, SSUB, STREX, SXT, UADD, UHADD, UQADD, UQSUB, USAD, USAT, USUB, UXT,

THUMB: CPS, CPY, REV, SETEND, SXTB, SXTH, UXTB, UXTH
*/

/* INCLUDES */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* MACROS */

//#define _CRT_SECURE_NO_WARNINGS 1
#define PATH_LENGTH (256)
#define RANGE_LENGTH (18)
#define STRING_LENGTH (64)
#define CONDITIONS_MAX (16)

#define BITS(x, b, n) ((x >> b) & ((1 << n) - 1)) //retrieves n bits from x starting at bit b
#define SIGNEX32_BITS(x, b, n) ((BITS(x,b,n) ^ (1<<(n-1))) - (1<<(n-1))) //convert n-bit value to signed 32 bits
#define SIGNEX32_VAL(x, n) ((x ^ (1<<(n-1))) - (1<<(n-1))) //convert n-bit value to signed 32 bits
#define ROR(x, n) ((x>>n)|(x<<(32-n))) //rotate right 32-bit value x by n bits

/* TYPEDEFS */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct {
    int start; //starting address
    int end; //end address
}FILERANGE;

typedef enum {
    ARMv4T, //ARM v4, THUMB v1
    ARMv5TE, //ARM v5, THUMB v2
    ARMv6, //ARM v6, THUMB v3
}ARMARCH; //only 32-bit legacy architectures with THUMB support

typedef enum {
    EQ, //equal, Z set
    NE, //not equal, Z clear
    CS, //carry set, C set (or HS, unsigned higher or same)
    CC, //carry clear, C clear (or LO, unsigned lower)
    MI, //minus/negative, N set
    PL, //plus/positive/zero, N clear
    VS, //overflow, V set
    VC, //no overflow, V clear
    HI, //unsigned higher, C set and Z clear
    LS, //unsigned lower or same, C clear or Z set
    GE, //signed greater than or equal, N==V
    LT, //signed less than, N!=V
    GT, //signed greater than, Z==0 and N==V
    LE, //signed less than or equal, Z==1 or N!=V
    AL, //unconditional, only with IT instructions
    NV  //unconditional, usually undefined
}CONDITION;

/* GLOBALS */

const u8 Conditions[CONDITIONS_MAX][3] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "" }; //last two are "al" and "nv", but never displayed

const u8 IT_xyz_0[CONDITIONS_MAX][4] = { //if then block suffixes
    "", //doesn't exist
    "ttt",
    "tt",
    "tte",
    "t",
    "tet",
    "te",
    "tee",
    "", //ommitted all
    "eee",
    "ee",
    "eet",
    "e",
    "ete",
    "et",
    "ett"
};

const u8 IT_xyz_1[CONDITIONS_MAX][4] = { //inverse of the one above
    "", //doesn't exist
    "eee",
    "ee",
    "eet",
    "e",
    "ete",
    "et",
    "ett",
    "", //ommitted all
    "tee",
    "te",
    "tet",
    "t",
    "tte",
    "tt",
    "ttt"
};

const u8 AddressingModes[4][3] = {
    "da", //Decrement after
    "ia", //Increment after
    "db", //Decrement before
    "ib"  //Increment before
};

const u8 DataProcessing_thumb[16][4] = {
    "and",
    "eor",
    "lsl",
    "lsr",
    "asr",
    "adc",
    "sbc",
    "ror",
    "tst", //no s
    "rsb",
    "cmp", //no s
    "cmn", //no s
    "orr",
    "mul",
    "bic",
    "mvn"
};

const u8 DataProcessing_arm[16][4] = {
    "and",
    "eor",
    "sub",
    "rsb",
    "add",
    "adc",
    "sbc",
    "rsc",
    "tst", //no s
    "teq", //no s
    "cmp", //no s
    "cmn", //no s
    "orr",
    "mov", //only 1 source operand
    "bic",
    "mvn"  //only 1 source operand
};

const u8 MSR_cxsf[16][5] = {
    "",
    "c",
    "x",
    "xc",
    "s",
    "sc",
    "sx",
    "sxc",
    "f",
    "fc",
    "fx",
    "fxc",
    "fs",
    "fsc",
    "fsx",
    "fsxc"
};

const u8 MultiplyLong[4][6] = { "umull", "umlal", "smull", "smlal" };
const u8 MovAddSubImmediate[4][4] = { "mov","cmp","add","sub" }; //cmp won't be used
const u8 LoadStoreRegister[8][6] = { "str", "strh", "strb", "ldrsb", "ldr", "ldrh", "ldrb", "ldrsh" };
const u8 CPS_effect[2][3] = { "ie","id" };
const u8 CPS_flags[8][5] = { "none", "f", "i", "if", "a", "af", "ai", "aif" };
const u8 SignZeroExtend[4][5] = { "sxth","sxtb","uxth","uxtb" };
const u8 Shifters[4][4] = { "lsl","lsr","asr","ror" }; //+rrx
u32 debug_na_count = 0;

/* FUNCTIONS */

static u32 FormatStringRegisterList_thumb(u8 str[STRING_LENGTH], u16 reg, const u8 pclr[3]) {
    /**/
    //todo for later: maybe group consecutive registers together with a -
    u32 bits = 0;
    u32 pos = 0; //position in the str array
    for (u32 i = 0; i < 9; i++) //9 instead of 16 because of clever grouping
    {
        if (BITS(reg, i, 1))
        {
            bits++;
            u8 tmp[4] = { 0 };
            if (i == 8) sprintf(tmp, pclr, i); //pc or lr
            else sprintf(tmp, "r%u,", i); //r0-r7
            memcpy(&str[pos], tmp, sizeof(tmp));
            pos += 3; //depends on size of tmp
        }
    }
    if (pos) str[pos - 1] = 0; //removes the coma on the last register
    return bits; //number of 1 bits
}

static u32 FormatStringRegisterList_arm(u8 str[STRING_LENGTH], u16 reg) {
    /**/
    //todo for later: maybe group consecutive registers together with a -
    u32 bits = 0;
    u32 pos = 0; //position in the str array
    for (u32 i = 0; i < 16; i++) //all registers
    {
        if (BITS(reg, i, 1))
        {
            bits++;
            u8 tmp[5] = { 0 };
            sprintf(tmp, "r%u,", i);
            memcpy(&str[pos], tmp, sizeof(tmp));
            pos = (i < 10) ? pos + 3 : pos + 4;
        }
    }
    if (pos) str[pos - 1] = 0; //removes the coma on the last register
    return bits; //number of 1 bits
}

static u32 CountBits(u8 b) {
    /* Count bits in a byte */
    static const u8 lut[16] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };
    return lut[b & 0x0f] + lut[b >> 4];
}

static void IfThen_imm_2(const u8* op, u8 str[STRING_LENGTH], u32 it, const u8* cond, u8 rm, u8 imm) {
    /* Formatting */
    if (it) sprintf(str, "%s%s r%u, #%X", op, cond, rm, imm);
    else sprintf(str, "%ss r%u, #%X", op, rm, imm);
}

static void IfThen_imm_3(const u8* op, u8 str[STRING_LENGTH], u32 it, const u8* cond, u8 rd, u8 rm, u8 imm) {
    /* Formatting */
    if (it) sprintf(str, "%s%s r%u, r%u, #%X", op, cond, rd, rm, imm);
    else sprintf(str, "%ss r%u, r%u, #%X", op, rd, rm, imm);
}

static void IfThen_reg_2(const u8* op, u8 str[STRING_LENGTH], u32 it, const u8* cond, u8 rd, u8 rm) {
    /* Formatting */
    if (it) sprintf(str, "%s%s r%u, r%u", op, cond, rd, rm);
    else sprintf(str, "%ss r%u, r%u", op, rd, rm);
}

static void IfThen_reg_3(const u8* op, u8 str[STRING_LENGTH], u32 it, const u8* cond, u8 rd, u8 rm, u8 rn) {
    /* Formatting */
    if (it) sprintf(str, "%s%s r%u, r%u, r%u", op, cond, rd, rm, rn);
    else sprintf(str, "%ss r%u, r%u, r%u", op, rd, rm, rn);
}

static void FormatExtraLoadStore(u32 c, u8* str, u8 cond, const u8* op) {
    /*  */
    u8 rd = BITS(c, 12, 4);
    u8 rn = BITS(c, 16, 4);
    u8 w = BITS(c, 21, 1);
    u8 p = BITS(c, 24, 1);
    if (BITS(c, 22, 1)) //immediate
    {
        u8 ofs = (BITS(c, 8, 4) << 4) | BITS(c, 0, 4);
        u8* sign = BITS(c, 23, 1) ? "+" : "-";
        if (p) //offset, pre-indexed
        {
            u8* pre = w ? "!" : "";
            sprintf(str, "%s%s r%u, [r%u, #%s%X]%s", op, Conditions[cond], rd, rn, sign, ofs, pre);
        }
        else //post-indexed
        {
            if (w) return; //w must be 0, else UNPREDICTABLE //todo: check if really invalid
            sprintf(str, "%s%s r%u, [r%u], #%s%X", op, Conditions[cond], rd, rn, sign, ofs);
        }
    }
    else //register
    {
        u8 rm = BITS(c, 0, 4);
        u8* sign = BITS(c, 23, 1) ? "" : "-"; //implicit "+" in front of registers
        if (p) //offset, pre-indexed
        {
            u8* pre = w ? "!" : "";
            sprintf(str, "%s%s r%u, [r%u, %sr%u]%s", op, Conditions[cond], rd, rn, sign, rm, pre);
        }
        else //post-indexed
        {
            if (w) return; //w must be 0, else UNPREDICTABLE //todo: check if really invalid
            sprintf(str, "%s%s r%u, [r%u], %sr%u", op, Conditions[cond], rd, rn, sign, rm);
        }
    }
}

static u32 Disassemble_thumb(u32 code, u8 str[STRING_LENGTH], u32 it, const u8* cond, ARMARCH tv) {
    /* Convert a code into a string, return 0 if processed THUMB 16-bit, 1 if processed THUMB 32-bit  */

    u32 thumb_size = 0; //return value
    u16 c = code & 0xffff; //low 16 bits

    switch (c >> 13)
    {
    case 0: //0x0000 //LSL, LSR, ASR, ADD, SUB
    {
        switch (BITS(c, 11, 2))
        {
        case 0:
        {
            if (tv >= ARMv5TE)
            {
                if (BITS(c, 6, 5)) //LSL immediate
                {
                    IfThen_imm_3("lsl", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
                }
                else //MOV register
                {
                    sprintf(str, "movs r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3));
                }
            }
            else //ARMv4T
            {
                sprintf(str, "lsl r%u, r%u, #%X", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            }
            break;
        }
        case 1: //LSR immediate
        {
            if (tv >= ARMv5TE)
            {
                IfThen_imm_3("lsr", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            }
            else //ARMv4T
            {
                sprintf(str, "lsr r%u, r%u, #%X", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            }
            break;
        }
        case 2: //ASR immediate
        {
            if (tv >= ARMv5TE)
            {
                IfThen_imm_3("asr", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            }
            else //ARMv4T
            {
                sprintf(str, "asr r%u, r%u, #%X", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            }
            break;
        }
        case 3: //ADD, SUB
        {
            if (BITS(c, 10, 1)) //ADD/SUB immediate
            {
                if (BITS(c, 9, 1)) //SUB immediate
                {
                    if (tv >= ARMv5TE)
                    {
                        IfThen_imm_3("sub", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                    else //ARMv4T
                    {
                        sprintf(str, "sub r%u, r%u, #%X", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                }
                else //ADD immediate
                {
                    if (tv >= ARMv5TE)
                    {
                        IfThen_imm_3("add", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                    else //ARMv4T
                    {
                        sprintf(str, "add r%u, r%u, #%X", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                }
            }
            else //ADD/SUB register
            {
                if (BITS(c, 9, 1)) //SUB register
                {
                    if (tv >= ARMv5TE)
                    {
                        IfThen_reg_3("sub", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                    else //ARMv4T
                    {
                        sprintf(str, "sub r%u, r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                }
                else //ADD register
                {
                    if (tv >= ARMv5TE)
                    {
                        IfThen_reg_3("add", str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                    else //ARMv4T
                    {
                        sprintf(str, "add r%u, r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3));
                    }
                }
            }
            break;
        }
        }
        break;
    }

    case 1: //0x2000 //MOV CMP, ADD, SUB
    {
        u8 index = BITS(c, 11, 2);
        if (index == 1) sprintf(str, "cmp r%u, #%X", BITS(c, 8, 3), BITS(c, 0, 8));
        else IfThen_imm_2(MovAddSubImmediate[index], str, it, cond, BITS(c, 8, 3), BITS(c, 0, 8));
        break;
    }

    case 2: //0x4000 //lots...
    {
        if (BITS(c, 10, 3) == 1)
        {
            switch (BITS(c, 8, 2))
            {
            case 0: //ADD register
            {
                if (tv >= ARMv5TE)
                {
                    u8 d = (BITS(c, 7, 1) << 3) | (BITS(c, 0, 3));
                    u8 m = BITS(c, 3, 4);
                    if (m == 13) sprintf(str, "add r%u, sp, r%u", d, d); //ADD sp + register v1
                    else sprintf(str, "add r%u, r%u", d, m); //ADD
                }
                break;
            }
            case 1: //CMP high register
            {
                if (tv >= ARMv5TE)
                {
                    sprintf(str, "cmp r%u, r%u", (BITS(c, 7, 1) << 3) | (BITS(c, 0, 3)), BITS(c, 3, 4));
                }
                break;
            }
            case 2: //MOV register
            {
                sprintf(str, "mov r%u, r%u", (BITS(c, 7, 1) << 3) | (BITS(c, 0, 3)), BITS(c, 3, 4));
                break;
            }
            case 3:
            {
                if (BITS(c, 7, 1)) sprintf(str, "blx r%u", BITS(c, 3, 4)); //BLX
                else sprintf(str, "bx r%u", BITS(c, 3, 4)); //BX
                break;
            }
            }
        }
        else if (!BITS(c, 10, 3))
        {
            u8 index = BITS(c, 6, 4);
            switch (index)
            {
            case 8:
            case 10:
            case 11:
            {
                sprintf(str, "%s r%u, r%u", DataProcessing_thumb[index], BITS(c, 0, 3), BITS(c, 3, 3));
                break;
            }
            default:
            {
                IfThen_reg_2(DataProcessing_thumb[index], str, it, cond, BITS(c, 0, 3), BITS(c, 3, 3));
            }
            }
        }
        else
        {
            if (BITS(c, 12, 1)) sprintf(str, "%s r%u, [r%u, r%u]", LoadStoreRegister[BITS(c, 9, 3)], BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 3)); //Load/store register offset  
            else sprintf(str, "ldr r%u, [pc, #%X]", BITS(c, 8, 3), 4 * BITS(c, 0, 8)); //LDR literal pool
        }
        break;
    }

    case 3: //0x6000 //STR, LDR, STRB, LDRB
    {
        switch (BITS(c, 11, 2))
        {
        case 0: //STR
        {
            sprintf(str, "str r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), 4 * BITS(c, 6, 5));
            break;
        }
        case 1: //LDR
        {
            sprintf(str, "ldr r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), 4 * BITS(c, 6, 5));
            break;
        }
        case 2: //STRB
        {
            sprintf(str, "strb r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            break;
        }
        case 3: //LDRB
        {
            sprintf(str, "ldrb r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), BITS(c, 6, 5));
            break;
        }
        }
        break;
    }

    case 4: //0x8000 //STR, LDR, STRH, LDRH
    {
        if (BITS(c, 12, 1))
        {
            if (tv >= ARMv5TE)
            {
                if (BITS(c, 11, 1)) sprintf(str, "ldr r%u, [sp, #%X]", BITS(c, 8, 3), 4 * BITS(c, 0, 8)); //LDR stack
                else sprintf(str, "str r%u, [sp, #%X]", BITS(c, 8, 3), 4 * BITS(c, 0, 8)); //STR stack
            }
        }
        else
        {
            if (BITS(c, 11, 1)) sprintf(str, "ldrh r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), 2 * BITS(c, 6, 5)); //LRDH immediate offset
            else sprintf(str, "strh r%u, [r%u, #%X]", BITS(c, 0, 3), BITS(c, 3, 3), 2 * BITS(c, 6, 5)); //STRH immediate offset
        }
        break;
    }

    case 5: //0xA000 //Misc and ADD to sp or pc
    {
        if (BITS(c, 12, 1)) //misc
        {
            switch (BITS(c, 8, 4))
            {
            case 0: //ADD/SUB sp
            {
                if (BITS(c, 7, 1)) sprintf(str, "sub sp, sp, #%X", 4 * BITS(c, 0, 7));  //SUB             
                else sprintf(str, "add sp, sp, #%X", 4 * BITS(c, 0, 7)); //ADD
                break;
            }
            //Compare and branch on (non-)zero
            case 1:
            case 3:
            case 9:
            case 11:
            {
                if (BITS(c, 11, 1)) sprintf(str, "cbnz r%u, #%X", BITS(c, 0, 3), 4 + 2 * ((BITS(c, 9, 1) << 5) | BITS(c, 3, 5))); //CBNZ           
                else sprintf(str, "cbz r%u, #%X", BITS(c, 0, 3), 4 + 2 * ((BITS(c, 9, 1) << 5) | BITS(c, 3, 5))); //CBZ               
                break;
            }
            case 2: //Sign/zero extend
            {
                if (tv >= ARMv6)
                {
                    sprintf(str, "%s r%u, r%u", SignZeroExtend[BITS(c, 6, 2)], BITS(c, 0, 3), BITS(c, 3, 3));
                }
                break;
            }
            //PUSH/POP
            case 4:
            case 5:
            case 12:
            case 13:
            {
                u8 reglist[STRING_LENGTH] = { 0 };
                u16 registers = BITS(c, 0, 9);
                if (BITS(c, 11, 1)) //POP
                {
                    if (FormatStringRegisterList_thumb(reglist, registers, "pc")) //if BitCount(registers) < 1 then UNPREDICTABLE
                    {
                        sprintf(str, "pop {%s}", reglist);
                    }
                }
                else //PUSH
                {
                    if (FormatStringRegisterList_thumb(reglist, registers, "lr")) //if BitCount(registers) < 1 then UNPREDICTABLE
                    {
                        sprintf(str, "push {%s}", reglist);
                    }
                }
                break;
            }
            case 6: //SETEND and CPS
            {
                if (tv >= ARMv6)
                {
                    if (c == 0xB650) sprintf(str, "setend le");
                    else if (c == 0xB658) sprintf(str, "setend be");
                    else if (BITS(c, 4, 8) == 100); //n/a
                    else if (BITS(c, 5, 7) == 51)
                    {
                        if (!BITS(c, 3, 1)) sprintf(str, "cps%s %s", CPS_effect[BITS(c, 4, 1)], CPS_flags[BITS(c, 0, 3)]); //CPS
                    }
                }
                break;
            }
            case 10: //Reverse byte
            {
                if (tv >= ARMv6)
                {
                    switch (BITS(c, 6, 2))
                    {
                    case 0: //REV
                    {
                        sprintf(str, "rev r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3));
                        break;
                    }
                    case 1: //REV16
                    {
                        sprintf(str, "rev16 r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3));
                        break;
                    }
                    case 2: //undefined
                    {
                        break;
                    }
                    case 3: //REVSH
                    {
                        sprintf(str, "revsh r%u, r%u", BITS(c, 0, 3), BITS(c, 3, 3));
                        break;
                    }
                    }
                }
                break;
            }
            case 14: //BKPT
            {
                sprintf(str, "bkpt #%X", BITS(c, 0, 8));
                break;
            }
            case 15: //IT and NOP-hints
            {
                u8 mask = BITS(c, 0, 4);
                if (mask) //IT
                {
                    u8 firstcond = BITS(c, 4, 4);
                    if (firstcond == 15); //n/a
                    else if (firstcond == 14 && CountBits(mask) != 1); //n/a
                    else
                    {
                        if (firstcond & 1) sprintf(str, "it%s %s", IT_xyz_1[mask], Conditions[firstcond]);
                        else sprintf(str, "it%s %s", IT_xyz_0[mask], Conditions[firstcond]);
                    }
                }
                else //NOP-compatible hints
                {
                    u8 hint = BITS(c, 4, 4);
                    switch (hint)
                    {
                    case 0: sprintf(str, "nop"); break;
                    case 1: sprintf(str, "yield"); break;
                    case 2: sprintf(str, "wfe"); break;
                    case 3: sprintf(str, "wfi"); break;
                    case 4: sprintf(str, "sev"); break;
                    default: sprintf(str, "hint #%X", hint);
                    }
                }
                break;
            }
            }
        }
        else //add to SP or PC
        {
            if (BITS(c, 11, 1)) sprintf(str, "add r%u, sp, #%X", BITS(c, 8, 3), 4 * BITS(c, 0, 8)); //ADD (SP plus immediate)
            else sprintf(str, "adr r%u, #%X", BITS(c, 8, 3), 4 * BITS(c, 0, 8)); //ADR (pc)
        }
        break;
    }

    case 6: //0xC000 //B, SWI, LDMIA, STMIA
    {
        if (BITS(c, 12, 1)) //Conditional branch, Undefined, System call
        {
            switch (BITS(c, 8, 4))
            {
            case 14: //UDF "Permanently undefined space", OS dependant
            {
                sprintf(str, "udf #%X", BITS(c, 0, 8));
                break;
            }
            case 15: //SWI
            {
                sprintf(str, "swi #%X", BITS(c, 0, 8));
                break;
            }
            default: //B conditional
            {
                sprintf(str, "b%s #%X", Conditions[BITS(c, 8, 4)], 4 + 2 * SIGNEX32_BITS(c, 0, 8));
            }
            }
        }
        else //Load/store multiple
        {
            u8 reglist[STRING_LENGTH] = { 0 };
            u16 registers = BITS(c, 0, 8);
            u8 rn = BITS(c, 8, 3);
            if (BITS(c, 11, 1)) //LDMIA (LDMFD)
            {
                if (FormatStringRegisterList_thumb(reglist, registers, ""))
                {
                    if (BITS(registers, rn, 1)) sprintf(str, "ldmia r%u, {%s}", rn, reglist); //no '!' because rn is also in registers
                    else sprintf(str, "ldmia r%u!, {%s}", rn, reglist);
                }
            }
            else //STMIA (STMEA)
            {
                if (FormatStringRegisterList_thumb(reglist, registers, ""))
                {
                    sprintf(str, "stmia r%u!, {%s}", rn, reglist);
                }
            }
        }
        break;
    }

    case 7: //0xE000 //B, then 32-bit instructions
    {
        switch (BITS(c, 11, 2))
        {
        case 0:
        {
            sprintf(str, "b #%X", 4 + 2 * SIGNEX32_BITS(c, 0, 11)); //11 bits to signed 32 bits
            break;
        }
        //case 1: break; //undefined on first pass
        case 2: //BL/BLX prefix
        {
            c = code >> 16; //get high 16 bits
            if (c >> 13 == 7)
            {
                switch (BITS(c, 11, 2))
                {
                case 1: //BLX suffix
                {
                    if (!BITS(c, 0, 1))
                    {
                        thumb_size++;
                        u32 ofs = ((BITS((code & 0xffff), 0, 10) << 10) | (BITS(c, 1, 10))) << 2;
                        sprintf(str, "blx #%X", 4 + SIGNEX32_VAL(ofs, 22));
                    }
                    break;
                }
                //case 2: break; //BL/BLX prefix
                case 3: //BL suffix
                {
                    thumb_size++;
                    u32 ofs = ((BITS((code & 0xffff), 0, 10) << 11) | (BITS(c, 0, 11))) << 1;
                    sprintf(str, "bl #%X", 4 + (int)SIGNEX32_VAL(ofs, 22));
                    break;
                }
                }
            }
            break;
        }
        }
        break;
    }
    }
    if (!str[0]) //in case nothing was written to it
    {
        debug_na_count++;
        sprintf(str, "n/a");
    }
    return thumb_size;
}

static void Disassemble_arm(u32 code, u8 str[STRING_LENGTH], ARMARCH av) {
    /**/
    //only supports ARMv5
    //todo: extra caution for UNPREDICTABLE instructions, need to remove them? or decode regardless?

    u32 c = code; //alias

    //Page 68 of 811 from the ARM Architecture reference manual june 2000 edition

    u8 cond = BITS(c, 28, 4);

    switch (BITS(c, 25, 3))
    {
    case 0: //todo
    {
        if (cond == NV) break; //undefined
        if (BITS(c, 4, 1))
        {
            if (BITS(c, 7, 1)) //Multiplies, extra load/stores: see fig 3-2
            {
                u8 oplo = BITS(c, 5, 2);
                if (!oplo)
                {
                    if (!BITS(c, 22, 3)) //Multiply (accumulate)
                    {
                        u8 rm = BITS(c, 0, 4);
                        u8 rs = BITS(c, 8, 4);
                        u8 rn = BITS(c, 12, 4);
                        u8 rd = BITS(c, 16, 4);
                        u8* s = BITS(c, 20, 1) ? "s" : "";
                        if (BITS(c, 21, 1)) //MLA
                        {
                            sprintf(str, "mla%s%s r%u, r%u, r%u, r%u", s, Conditions[cond], rd, rm, rs, rn);
                        }
                        else //MUL
                        {
                            if (rn) break; //Should-Be-Zero
                            sprintf(str, "mul%s%s r%u, r%u, r%u", s, Conditions[cond], rd, rm, rs);
                        }
                    }
                    else if (BITS(c, 23, 1)) //Multiply (accumulate) long
                    {
                        u8* s = BITS(c, 20, 1) ? "s" : "";
                        sprintf(str, "%s%s%s r%u, r%u, r%u, r%u", MultiplyLong[BITS(c, 21, 2)], s, Conditions[cond], BITS(c, 12, 4), BITS(c, 16, 4), BITS(c, 0, 4), BITS(c, 8, 4));
                    }
                    else //Swap/swap byte (SWP, SWPB)
                    {
                        if (BITS(c, 8, 4)) break; //Should-Be-Zero
                        u8* b = BITS(c, 22, 1) ? "b" : ""; //byte or no
                        sprintf(str, "swp%s%s r%u, r%u, [r%u]", b, Conditions[cond], BITS(c, 12, 4), BITS(c, 0, 4), BITS(c, 16, 4));
                    }
                }
                else
                {
                    if (!BITS(c, 22, 1) && BITS(c, 8, 4)) break; //Should-Be-Zero if register offset
                    if (oplo == 1) //Load/store halfword
                    {
                        u8* l = BITS(c, 20, 1) ? "ldrh" : "strh"; //load or store
                        FormatExtraLoadStore(c, str, cond, l);
                    }
                    else
                    {
                        if (BITS(c, 20, 1)) //Load signed halfword/byte
                        {
                            u8* h = BITS(c, 5, 1) ? "ldrsh" : "ldrsb"; //halfword/byte
                            FormatExtraLoadStore(c, str, cond, h);
                        }
                        else //Load/store two words
                        {
                            if (BITS(c, 12, 1)) break; //undefined if Rd is odd
                            u8* s = BITS(c, 5, 1) ? "strd" : "ldrd"; //store or load
                            FormatExtraLoadStore(c, str, cond, s);
                        }
                    }
                }
            }
            else //todo
            {
                if (BITS(c, 23, 2) == 2 && !BITS(c, 20, 1)) //Miscellanous instructions, see fig 3-3
                {
                    //caution: bit 7 can be 1, relocate this branch!
                    //Branch/exchange instruction set
                    //Count leading zeroes
                    //Branch and link/exchange instruction set
                    //Enhanced DSP add/sub
                    //Software breakpoint
                }
                else //Data processing register shift
                {
                    u8 rm = BITS(c, 0, 4);
                    u8 shift = BITS(c, 5, 2);
                    u8 rs = BITS(c, 8, 4);
                    u8 rd = BITS(c, 12, 4);
                    u8 rn = BITS(c, 16, 4);
                    u8 op = BITS(c, 21, 4);
                    u8* s = BITS(c, 20, 1) ? "s" : "";
                    switch (op)
                    {
                    case 8: //TST
                    case 9: //TEQ
                    case 10: //CMP
                    case 11: //CMN
                    {
                        sprintf(str, "%s%s r%u, r%u, %s r%u", DataProcessing_arm[op], Conditions[cond], rn, rm, Shifters[shift], rs);
                        break;
                    }
                    case 13: //MOV
                    case 15: //MVN
                    {
                        sprintf(str, "%s%s%s r%u, r%u, %s r%u", DataProcessing_arm[op], s, Conditions[cond], rd, rm, Shifters[shift], rs);
                        break;
                    }
                    default:
                    {
                        sprintf(str, "%s%s%s r%u, r%u, %s r%u", DataProcessing_arm[op], s, Conditions[cond], rd, rm, Shifters[shift], rs);
                    }
                    }
                }
            }
        }
        else
        {
            if (BITS(c, 23, 2) == 2 && !BITS(c, 20, 1)) //Miscellanous instructions, see fig 3-3
            {
                if (!BITS(c, 0, 12) && BITS(c, 16, 4) == 15) //Move status reg to reg (MRS)
                {
                    //note: if Rd == PC, UNPREDICTABLE
                    u8 sreg = BITS(c, 22, 1) ? 's' : 'c'; //SPSR (1) or CPSR (0)
                    sprintf(str, "mrs%s r%u, %cpsr", Conditions[cond], BITS(c, 12, 4), sreg);
                }
                else if (BITS(c, 12, 4) == 15 && !BITS(c, 4, 8) && BITS(c, 21, 1)) //Move reg to status reg (MSR register)
                {
                    u8 sreg = BITS(c, 22, 1) ? 's' : 'c'; //SPSR (1) or CPSR (0)
                    sprintf(str, "msr%s %cpsr_%s, r%u", Conditions[cond], sreg, MSR_cxsf[BITS(c, 16, 4)], BITS(c, 0, 4));
                }
                //else?
                //Enhanced DSP multiplies (SMUL, SMULW, SMLAW, SMLAL +B or T depending on <y>)
            }
            else //Data processing immediate shift
            {
                //todo: write a function for all 3 Data Processing switch-cases
                u8 op = BITS(c, 21, 4);
                u8 rm = BITS(c, 0, 4);
                u8 shift = BITS(c, 5, 2);
                u8 shift_imm = BITS(c, 7, 5);
                u8 sstr[STRING_LENGTH] = "rrx"; //default, overwrite if incorrect
                if ((shift == 1 || shift == 2) && !shift_imm) shift_imm = 32; //0~31 for LSL, 1~32 for LSR, ASR and ROR, always 0 for RRX
                 //todo: reformat the following test
                if (!(shift == 3 && !shift_imm)) sprintf(sstr, "%s #%u", Shifters[shift], shift_imm);
                u8 rd = BITS(c, 12, 4);
                u8 rn = BITS(c, 16, 4);
                u8* s = BITS(c, 20, 1) ? "s" : "";
                switch (op)
                {
                case 8: //TST
                case 9: //TEQ
                case 10: //CMP
                case 11: //CMN
                {
                    sprintf(str, "%s%s r%u, r%u, %s", DataProcessing_arm[op], Conditions[cond], rn, rm, sstr);
                    break;
                }
                case 13: //MOV
                case 15: //MVN
                {
                    sprintf(str, "%s%s%s r%u, r%u, %s", DataProcessing_arm[op], s, Conditions[cond], rd, rm, sstr);
                    break;
                }
                default:
                {
                    sprintf(str, "%s%s%s r%u, r%u, r%u, %s", DataProcessing_arm[op], s, Conditions[cond], rd, rn, rm, sstr);
                }
                }
            }
        }
        break;
    }
    case 1: //Data processing and MSR immediate
    {
        if (cond == NV) break; //undefined
        u32 imm = ROR(BITS(c, 0, 8), 2 * BITS(c, 8, 4));

        if (BITS(c, 12, 4) == 15 && !BITS(c, 20, 1)) //MSR immediate
        {
            u8 sreg = BITS(c, 22, 1) ? 's' : 'c'; //SPSR (1) or CPSR (0)
            sprintf(str, "msr%s %cpsr_%s, #%X", Conditions[cond], sreg, MSR_cxsf[BITS(c, 16, 4)], imm);
        }
        else //Data processing immediate
        {
            u8 op = BITS(c, 21, 4);
            u8 rd = BITS(c, 12, 4);
            u8 rn = BITS(c, 16, 4);
            u8* s = BITS(c, 20, 1) ? "s" : "";
            switch (op)
            {
            case 8: //TST
            case 9: //TEQ
            case 10: //CMP
            case 11: //CMN
                //always update the condition codes: <op>{<cond>} <Rn>, <shift>
            {
                sprintf(str, "%s%s r%u, #%X", DataProcessing_arm[op], Conditions[cond], rn, imm);
                break;
            }
            case 13: //MOV
            case 15: //MVN
                //only one source operand: <op>{<cond>}{S} <Rd>, <shift>
            {
                sprintf(str, "%s%s%s r%u, #%X", DataProcessing_arm[op], s, Conditions[cond], rd, imm);
                break;
            }

            default: //others: <op>{<cond>}{S} <Rd>, <Rn>, <shift>
            {
                sprintf(str, "%s%s%s r%u, r%u, #%X", DataProcessing_arm[op], s, Conditions[cond], rd, rn, imm);
            }
            }
        }
        break;
    }
    case 2: //Load/store immediate offset
    {
        if (cond == NV) break; //undefined
        //bits 25, 24, 23 and 21 decide the addressing mode
        u8 rd = BITS(c, 12, 4);
        u8 rn = BITS(c, 16, 4);
        u16 imm = BITS(c, 0, 12); //12 bits for LDR and LDRB (8 bits for LDRH and LDRSB)
        u8* sign = BITS(c, 23, 1) ? "+" : "-"; //sign of the immediate offset
        u8* ls = BITS(c, 20, 1) ? "ldr" : "str"; //load or store
        u8* b = BITS(c, 22, 1) ? "b" : ""; //byte or word
        if (BITS(c, 24, 1)) //offset or pre-indexed
        {
            u8* w = BITS(c, 21, 1) ? "!" : ""; //pre-indexed if 1
            sprintf(str, "%s%s%s r%u, [r%u, #%s%X]%s", ls, b, Conditions[cond], rd, rn, sign, imm, w);
        }
        else //post-indexed
        {
            u8* w = BITS(c, 21, 1) ? "t" : ""; //user mode
            sprintf(str, "%s%s%s%s r%u, [r%u], #%s%X", ls, b, w, Conditions[cond], rd, rn, sign, imm);
        }
        break;
    }
    case 3: //Load/store register offset
    {
        if (cond == NV) break; //undefined
        if (BITS(c, 4, 1)) break; //undefined
        u8 rm = BITS(c, 0, 4);
        u8 shift = BITS(c, 5, 2);
        u16 shift_imm = BITS(c, 7, 5);
        u8 rd = BITS(c, 12, 4);
        u8 rn = BITS(c, 16, 4);
        u8* sign = BITS(c, 23, 1) ? "" : "-"; //sign of the immediate offset, + implicit
        u8* ls = BITS(c, 20, 1) ? "ldr" : "str"; //load or store
        u8* b = BITS(c, 22, 1) ? "b" : ""; //byte or word
        u8 sstr[STRING_LENGTH] = "rrx"; //default, overwrite if incorrect
        if ((shift == 1 || shift == 2) && !shift_imm) shift_imm = 32; //0~31 for LSL, 1~32 for LSR, ASR and ROR, always 0 for RRX
         //todo: reformat the following test
        if (!(shift == 3 && !shift_imm)) sprintf(sstr, "%s #%u", Shifters[shift], shift_imm);
        //note: if rm==r15 or rn==r15 then UNPREDICTABLE
        //note: if rn==rm then UNPREDICTABLE
        if (BITS(c, 24, 1)) //offset or pre-indexed
        {
            u8* w = BITS(c, 21, 1) ? "!" : ""; //pre-indexed if 1
            sprintf(str, "%s%s%s r%u, [r%u, %sr%u, %s]%s", ls, b, Conditions[cond], rd, rn, sign, rm, sstr, w);
        }
        else //post-indexed
        {
            u8* w = BITS(c, 21, 1) ? "t" : ""; //user mode
            sprintf(str, "%s%s%s%s r%u, [r%u], %sr%u, %s", ls, b, w, Conditions[cond], rd, rn, sign, rm, sstr);
        }
        break;
    }
    case 4: //Load/store multiple
    {
        if (cond == NV) break; //undefined
        u8 reglist[STRING_LENGTH] = { 0 };
        FormatStringRegisterList_arm(reglist, BITS(c, 0, 16));
        u8 rn = BITS(c, 16, 4);
        u8* w = BITS(c, 21, 1) ? "!" : ""; //W bit
        u8* s = BITS(c, 22, 1) ? "^" : ""; //S bit
        u8 am = BITS(c, 23, 2); //PU bits
        if (BITS(c, 20, 1)) //LDM
        {
            sprintf(str, "ldm%s%s r%u%s, {%s}%s", Conditions[cond], AddressingModes[am], rn, w, reglist, s);
        }
        else //STM
        {
            sprintf(str, "stm%s%s r%u%s, {%s}%s", Conditions[cond], AddressingModes[am], rn, w, reglist, s);
        }
        break;
    }
    case 5: //Branch instructions
    {
        if (cond == NV) //BLX
        {
            sprintf(str, "blx #%X", 8 + 4 * SIGNEX32_BITS(c, 0, 24) + 2 * BITS(c, 24, 1));
        }
        else //Branch and branch with link
        {
            if (BITS(c, 24, 1)) //BL
            {
                sprintf(str, "bl #%X", 8 + 4 * SIGNEX32_BITS(c, 0, 24));
            }
            else //B
            {
                sprintf(str, "b #%X", 8 + 4 * SIGNEX32_BITS(c, 0, 24));
            }
        }
        break;
    }
    case 6: //todo
    {
        //if (cond == NV) break; //only unpredictable prior to ARMv5
        //Coprocessor load/store and double register transfers (MCRR, MRRC)
        //note: if PC is specified for Rn or Rd, UNPREDICTABLE
        u8* ls = BITS(c, 20, 1) ? "mrrc" : "mcrr";
        sprintf(str, "%s%s p%u, #%X, r%u, r%u, c%u", ls, Conditions[cond], BITS(c, 8, 4), BITS(c, 4, 4), BITS(c, 12, 4), BITS(c, 16, 4), BITS(c, 0, 4));
        break;
    }
    case 7: //todo
    {
        //if (cond == NV) break; //only unpredictable prior to ARMv5
        if (BITS(c, 24, 1)) //SWI
        {
            sprintf(str, "swi%s #%X", Conditions[cond], BITS(c, 0, 24));
        }
        //else
        //Coprocessor data processing
        //Coprocessor register transfer
        break;
    }
    //also: unconditionnal instructions if bits 28 to 31 are 1111
    }
    if (!str[0]) //in case nothing was written to it
    {
        debug_na_count++;
        sprintf(str, "n/a");
    }
}

static void SubstituteSubString(u8 dst[STRING_LENGTH], u32 index, const u8* sub, u32 size) {
    /* Insert sub string of length size (< STRING_LENGTH) at dst[index] */
    u8 tmp[STRING_LENGTH] = { 0 }; //zinit
    memcpy(&tmp, sub, size); //init
    memcpy(&tmp[size], &dst[index + size + 1], STRING_LENGTH - index - size - 1); //save
    memcpy(&dst[index], tmp, STRING_LENGTH - index - 1); //restore
}

static void CheckSpecialRegister(u8 str[STRING_LENGTH]) {
    /* Replace special registers by their common names */
    //(r12->ip), r13->sp, r14->lr, r15->pc
    for (u32 i = 0; i < STRING_LENGTH; i++)
    {
        if (str[i] == 'r')
        {
            switch (*(u16*)&str[i + 1])
            {
            case 0x3331: //"13"
            {
                SubstituteSubString(str, i, "sp", sizeof("sp") - 1);
                break;
            }
            case 0x3431: //"14"
            {
                SubstituteSubString(str, i, "lr", sizeof("lr") - 1);
                break;
            }
            case 0x3531: //"15"
            {
                SubstituteSubString(str, i, "pc", sizeof("pc") - 1);
                break;
            }
            }
        }
    }
}

static void Debug_DisassembleCode_arm(u32 c) {
    /* Debug: disassemble a single ARM instruction from the ARMv5TE architecture */
    u8 s[STRING_LENGTH] = { 0 };
    Disassemble_arm(c, s, ARMv5TE);
    printf("%08X -> %s\n", c, s);
}

static void Debug_DumpAllInstructions(void) {
    /* Debug: call only when you want to dump the complete THUMB instruction set to a file */

    FILE* fp = fopen("ARMv5TE_THUMB_instruction_set.txt", "w+");

    for (u32 code = 0; code <= 0xffff; code++)
    {
        u8 str[STRING_LENGTH] = { 0 };
        Disassemble_thumb(code, str, 0, "", ARMv5TE);
        CheckSpecialRegister(str);
        fprintf(fp, "%04X %s\n", code, str);
    }

    fclose(fp);

    printf("N/A instructions remaining: %u (%u%% done)\n", debug_na_count, 100 - 100 * debug_na_count / 65536);
}

static int GetFileSize_mine(FILE* fp) {
    /* Return the size of an opened file */
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);
    rewind(fp);
    return size;
}

static int DisassembleFile(FILE* in, FILE* out, FILERANGE* range) {
    /* Disassemble from a binary file, print to another file */
    int size = GetFileSize_mine(in);
    if (range->start > size || range->end > size) return 0;
    if (range->end && (range->end < size))
    {
        size = range->end;
    }
    fprintf(out, "Disassembly of %u bytes:\n\n", range->end - range->start);

    fseek(in, range->start, SEEK_SET);

    for (int i = range->start; i < (range->start + size) / 2; i++)
    {
        u8 str[STRING_LENGTH] = { 0 };
        u32 code = 0;
        fread(&code, 4, 1, in); //prefetch 32 bits
        if (Disassemble_thumb(code, str, 0, "", ARMv5TE)) //32-bit
        {
            CheckSpecialRegister(str);
            fprintf(out, "%08X: %08X %s\n", range->start + (i - range->start) * 2, code, str);
            i++;
        }
        else //16-bit
        {
            fseek(in, -2, SEEK_CUR); //go back 2 bytes
            CheckSpecialRegister(str);
            fprintf(out, "%08X: %04X     %s\n", range->start + (i - range->start) * 2, code & 0xffff, str);
        }
    }

    fprintf(out, "\n%u unknown instructions.\n", debug_na_count);
    return 1; //success
}

static int IsValidPath(u8* path) {
    /* Check if length of path/filename is */
    //todo: check if you can also open it
    //todo: check for "." in the name, need file extension
    u32 hasDotAndEom = 0;
    if (!path || !path[0]) return 0; //needs to be at least one char
    for (u32 i = 1; i < PATH_LENGTH; i++)
    {
        if (path[i] == '.') hasDotAndEom = 1;
        if (!path[i])
        {
            hasDotAndEom++;
            break; //found EOM
        }
    }
    if (hasDotAndEom == 2) return 1;
    return 0;
}

static int IfValidRangeSet(FILERANGE* range, u8* r) {
    /* Check to see if the range string yields a valid FILERANGE */
    //Acceptable formats:
    //"%x-%x" //start to end
    //"--%x" //unspecified start (default to beginning of file, 0) to end
    //"%x--" //start to unspecified end (default to end of file)

    u32 start = 0;
    u32 end = 0;

    if (!r || !r[0]) return 0; //needs to be at least one char

    /* Init buffer (memcpy with two limit conditions) */
    u8 str[RANGE_LENGTH] = { 0 };
    u32 i = 0;
    while (r[i] && (i < RANGE_LENGTH - 1)) //must be null terminated
    {
        str[i] = r[i];
        i++;
    }

    if (*(u16*)str == 0x2d2d) //detect double dash
    {
        end = strtol(&str[2], NULL, 16);
    }
    else
    {
        for (u32 i = 0; i < RANGE_LENGTH; i++)
        {
            if (str[i] == '-') //first dash
            {
                start = strtol(str, NULL, 16);
                end = strtol(&str[i + 1], NULL, 16);
                break;
            }
        }
    }

    if (end && (start > end))
    {
        printf("WARNING: If END is specified and non-zero, it cannot be greater than START. The whole file will be disassembled.\n");
        return 0; //start can't be greater than end if end is non-zero
    }

    /* Success, set */
    range->start = start;
    range->end = end;
    return 1;
}

/* ENTRY POINT */

#define DEBUG //comment out to disable debug ifdefs

int main(int argc, char* argv[]) {

#ifdef DEBUG
    //Debug_DumpAllInstructions();
    Debug_DisassembleCode_arm(0xfc5d5f09);
    //u32 i = 0;
    //while (++i)
    //{
    //    Debug_DisassembleCode_arm(i);
    //}
    //printf("%u n/a instructions, %.2f%% complete.\n", debug_na_count, 100 - (100 * (float)debug_na_count / (float)0x100000000));

#else

    u8* filename_in = argv[1];
    u8* filename_out = NULL;
    FILE* file_in = NULL;
    FILE* file_out = NULL;
    FILERANGE filerange = { 0 };

    if (IsValidPath(filename_in)) //file in
    {
        file_in = fopen(filename_in, "rb");
        if (file_in == NULL)
        {
            printf("ERROR: The file \"%s\" doesn't exist. Aborting.\n", filename_in);
            return 0;
        }

        if (IsValidPath(argv[2])) //file out
        {
            filename_out = argv[2];
            file_out = fopen(filename_out, "w+");
            if (file_out == NULL) return 0; //couldn't create file for some reason

            IfValidRangeSet(&filerange, argv[3]);
            printf("Starting disassembly of \"%s\".\n", filename_in);
            if (DisassembleFile(file_in, file_out, &filerange))
            {
                printf("Successfully disassembled \"%s\" to \"%s\".\n", filename_in, filename_out);
            }
            else
            {
                printf("ERROR: DisassembleFile failed.\n");
            }
            fclose(file_in);
            fclose(file_out);
        }
        else //stdout
        {
            IfValidRangeSet(&filerange, argv[2]);
            printf("Starting disassembly of \"%s\".\n", filename_in);
            if (DisassembleFile(file_in, stdout, &filerange))
            {
                printf("Successfully disassembled \"%s\".\n", filename_in);
            }
            else
            {
                printf("ERROR: DisassembleFile failed.\n");
            }
            fclose(file_in);
        }
        return 0;
    }

    printf("Nothing was done\n");
#endif // DEBUG

    return 0;
}
