#include <string.h>
#include <time.h>
#include "cdcode.h"
#include "plcdef.h"
#include "binheader.h"
#include <windows.h>
#include <stdint.h>
extern char work_mlc_ram[plc_comp_max];
//char label_table[100][14];
char label_table[1000][14];
//unsigned int jmp_address[100];
unsigned int jmp_address[1000];
//unsigned int label_address[100];
unsigned int label_address[1000];
int  jmp_index;
//modify by ted for label使用太多超過100個
//int  label_index[100];
int  label_index[1000];
//
int  label_total;
int  error_line[mes_err_max];  /* save error line position */
char temp_reg_stack;
struct regpar *parpt;  // point register parameter load struct
char C_label[6];
//add by ted for Level 2,3 read I'(I2048~I4095) ps. copy from I0~I2047
int iLevel = 0;
//
//20250119
typedef struct {
	int part1; // rotate / 2
	uint32_t part2; // encode
} Result;



Result rotate_and_return(uint32_t encode)
{
	int rotate;

	for (rotate = 0; rotate < 32; rotate += 2)
	{

		if (!(encode & ~0xffU)) //確認是否只有低 8 位有數值，高 24 位必須為 0 ~0xffU = 0b11111111_11111111_11111111_00000000

		{

			Result result = { rotate / 2, encode };
			return result;
		}

		encode = (encode << 2) | (encode >> 30); //向左旋轉 2 位元
	}


	Result result = { 0, 0 };
	return result;
}

bool is_legal_arm_immediate(uint32_t num) {
	for (int shift = 0; shift <= 30; shift += 2) { // 偶數位移 0, 2, 4, ..., 30
												   // 循環右移 num 偶數位

		uint32_t rotated = ((num >> shift) | (num << (32 - shift))) & 0xFFFFFFFF; //將 32 位整數 num 向右旋轉 shift 位，並保證結果始終是 32 位。 

																				  // 檢查是否符合 0x000000XX 格式
		if ((rotated & 0xFFFFFF00) == 0) {
			return true;
		}
	}
	return false;
}

uint32_t  mov_encode_constant(uint32_t constant, uint32_t reg) {


	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t I = 1 << 25;           // I = 1 表示立即數
	uint32_t opcode = 0b1101 << 21; // MOV 指令的操作碼 (4位)
	uint32_t S = 0 << 20;           // S = 0 不設定狀態位元
	uint32_t Rd = (reg & 0b1111) << 12;     // Rd = 0 (R0 暫存器)


	Result result = rotate_and_return(constant);
	// printf("part1: 0x%X, part2: 0x%02X\n", result.part1, result.part2);
	uint32_t instruction = cond | I | opcode | S | Rd | (result.part1 << 8) | result.part2;

	return instruction;

}

uint32_t generate_and_imm(int rd, int rn, int imm12) { 
    uint32_t machine_code;
    
    // 立即數可用 8 位元直接表示
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
    uint32_t I = 1 << 25;           // I = 1 表示立即數
    
    uint32_t S = 0 << 20;           // S = 0 不設定狀態位元
    uint32_t Rn = (rn & 0b1111) << 16; // Rd = 目標暫存器
    uint32_t Rd = (rd & 0b1111) << 12; // Rd = 目標暫存器
    uint32_t imm = imm12;  // 立即數 (8位)

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {

        Result result = rotate_and_return(imm12);
        //printf("part1: 0x%X, part2: 0x%02X\n", result.part1, result.part2);
        return  cond | I | S | Rn |Rd |(result.part1 << 8) | result.part2;
    }
    else {
        return cond | I | S | Rn | Rd | imm;}

    

}

// 產生 ARMv7 MUL 指令的機器碼
uint32_t generate_mul_reg_reg(int rd, int rn, int rm, int setflag) {
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (bits 31:28)
    uint32_t op   = 0b000000 << 21; // MUL 指令 (bits 27:21 固定為 000000)
    uint32_t S    = (setflag & 1) << 20;  // S = 1 設定狀態標誌 (bit 20)

    uint32_t Rd   = (rd & 0b1111) << 16;  // 目標暫存器 (bits 19:16)
    uint32_t Rm   = (rm & 0b1111) << 8;  // 乘數 2 (bits 15:12) (原本沒有位移)
    uint32_t MUL  = 0b1001 << 4;          // MUL 指令固定 (bits 11:8)
    uint32_t Rn   = (rn & 0b1111);        // 乘數 1 (bits 3:0)

    uint32_t machine_code = cond | op | S | Rd | Rn | MUL | Rm;

    return machine_code;
}

// 產生 ARMv7 MUL 指令的機器碼
uint32_t generate_sdiv_reg_reg(int rd, int rn, int rm) {
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (bits 31:28)
    uint32_t op   = 0b0111000 << 21; // MUL 指令 (bits 27:21 固定為 000000)
    uint32_t S    = ( 0b1) << 20;  // S = 1 設定狀態標誌 (bit 20)

    uint32_t Rd   = (rd & 0b1111) << 16;  // 目標暫存器 (bits 19:16)
    uint32_t set_code  = 0b1111 << 12;  
    uint32_t Rm   = (rm & 0b1111) << 8;  // 乘數 2 (bits 15:12) (原本沒有位移)
    uint32_t MUL  = 0b0001 << 4;          // SDIV 指令固定 (bits 11:8)
    uint32_t Rn   = (rn & 0b1111);        // 乘數 1 (bits 3:0)

    uint32_t machine_code = cond | op | S | Rd | set_code | Rn | MUL | Rm;

    return machine_code;
}

uint32_t generate_mov_reg_reg(int rd, int rm, int setflag) {   // 5 4 5
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4 bits)
    uint32_t op   = 0b1101 << 21;   // mov 指令 (bits [24:21])
    uint32_t S    = (setflag & 1) << 20;  // S=1 設定狀態標誌 (bit 20)

    uint32_t Rd   = (rd & 0b1111) << 12;  // Rd = 目標暫存器
    uint32_t Rm   = (rm & 0b1111);  // 修正 Rm 範圍，確保只取 4 bits

    uint32_t machine_code = cond | op | S | Rd | Rm;

    // 在 function 內部顯示機器碼
    //printf("Generated Machine Code: 0x%08X\n", machine_code);

    return machine_code;
}

uint32_t generate_and_reg(int rd, int rn, int rm, int setflag) {   // 5 4 5
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4 bits)
    uint32_t op   = 0b0000 << 21;   // AND 指令 (bits [24:21])
    uint32_t S    = (setflag & 1) << 20;  // S=1 設定狀態標誌 (bit 20)
    uint32_t Rn   = (rn & 0b1111) << 16;  // Rn = 第一個來源暫存器
    uint32_t Rd   = (rd & 0b1111) << 12;  // Rd = 目標暫存器
    uint32_t Rm   = (rm & 0b1111);        // Rm = 第二個來源暫存器

    uint32_t machine_code = cond | op | S | Rn | Rd | Rm;

    // 在 function 內部顯示機器碼
    //printf("Generated Machine Code: 0x%08X\n", machine_code);

    return machine_code;
}

uint32_t generate_or_imm(int rd, int rn, uint32_t imm, int setflag) {
    // 指令格式: ORR{S}<c> <Rd>, <Rn>, #<imm>
    // 編碼規則: cond(4) | 0b0011_0(5) | S(1) | Rn(4) | Rd(4) | imm12(12)
    // imm12 的合法格式: 4-bit 旋轉值 (rotate) + 8-bit 立即數 (imm8)
    // 需確保 imm 是合法立即數（例如 0x000000FF 或 0xF000000F 等可旋轉表示的數值）

    uint32_t cond = 0b1110 << 28;    // 無條件執行 (AL)
    uint32_t op   = 0b0011100 << 21; // ORR 操作碼 (0b0011100, 佔 bits 24-20)
    uint32_t S    = (setflag & 1) << 20; // 設定狀態標誌 (bit 20)
    uint32_t Rn   = (rn & 0b1111) << 16; // Rn 來源暫存器
    uint32_t Rd   = (rd & 0b1111) << 12; // Rd 目標暫存器
    uint32_t imm12 = imm & 0xFFF;       // 提取低 12 位（需由調用者確保 imm 合法性）

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {

        Result result = rotate_and_return(imm12);
        //printf("part1: 0x%X, part2: 0x%02X\n", result.part1, result.part2);
        return  cond | op | S | Rn |Rd |(result.part1 << 8) | result.part2;
    }
    else {
        return cond | op | S | Rn | Rd | imm;}
}



uint32_t generate_or_reg(int rd, int rn, int rm, int setflag) {   // 5 4 5
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4 bits)
    uint32_t op   = 0b1100 << 21;   // AND 指令 (bits [24:21])
    uint32_t S    = (setflag & 1) << 20;  // S=1 設定狀態標誌 (bit 20)
    uint32_t Rn   = (rn & 0b1111) << 16;  // Rn = 第一個來源暫存器
    uint32_t Rd   = (rd & 0b1111) << 12;  // Rd = 目標暫存器
    uint32_t Rm   = (rm & 0b1111);        // Rm = 第二個來源暫存器

    uint32_t machine_code = cond | op | S | Rn | Rd | Rm;

    // 在 function 內部顯示機器碼
    //printf("Generated Machine Code: 0x%08X\n", machine_code);

    return machine_code;
}

uint32_t generate_eor_imm(int rd, int rn, uint32_t imm, int setflag) {
    // 指令格式: ORR{S}<c> <Rd>, <Rn>, #<imm>
    // 編碼規則: cond(4) | 0b0011_0(5) | S(1) | Rn(4) | Rd(4) | imm12(12)
    // imm12 的合法格式: 4-bit 旋轉值 (rotate) + 8-bit 立即數 (imm8)
    // 需確保 imm 是合法立即數（例如 0x000000FF 或 0xF000000F 等可旋轉表示的數值）

    uint32_t cond = 0b1110 << 28;    // 無條件執行 (AL)
    uint32_t op   = 0b0010001 << 21; // ORR 操作碼 (0b0011100, 佔 bits 24-20)
    uint32_t S    = (setflag & 1) << 20; // 設定狀態標誌 (bit 20)
    uint32_t Rn   = (rn & 0b1111) << 16; // Rn 來源暫存器
    uint32_t Rd   = (rd & 0b1111) << 12; // Rd 目標暫存器
    uint32_t imm12 = imm & 0xFFF;       // 提取低 12 位（需由調用者確保 imm 合法性）

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {

        Result result = rotate_and_return(imm12);
        //printf("part1: 0x%X, part2: 0x%02X\n", result.part1, result.part2);
        return  cond | op | S | Rn |Rd |(result.part1 << 8) | result.part2;
    }
    else {
        return cond | op | S | Rn | Rd | imm;}
}

uint32_t generate_eor_reg(int rd, int rn, int rm, int setflag) {   // 5 4 5
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4 bits)
    uint32_t op   = 0b0000001 << 21;   // AND 指令 (bits [24:21])
    uint32_t S    = (setflag & 1) << 20;  // S=1 設定狀態標誌 (bit 20)
    uint32_t Rn   = (rn & 0b1111) << 16;  // Rn = 第一個來源暫存器
    uint32_t Rd   = (rd & 0b1111) << 12;  // Rd = 目標暫存器
    uint32_t Rm   = (rm & 0b1111);        // Rm = 第二個來源暫存器

    uint32_t machine_code = cond | op | S | Rn | Rd | Rm;

    // 在 function 內部顯示機器碼
    //printf("Generated Machine Code: 0x%08X\n", machine_code);

    return machine_code;
}


uint32_t add_reg_reg(int rd, int rn, int rm, int set_flags) { //  sub r1 r1 r0 , SUB & SUBS 差在 int set_flags
    uint32_t machine_code;
    
    // Build the instruction
    // [31:28] - condition (AL = 0xE) 1110 0000 010S 0001 0001 0000 0000 0000
    // [27:26] - 0b00 (Data processing)
    // [25]    - 0 (immediate operand flag)
    // [24]    - 0 (set flags)
    // [23:20] - opcode (SUB = 0x2)
    // [19:16] - Rn (first operand register)
    // [15:12] - Rd (destination register)
    // [11:0]  - operand2 (second operand)
    
    machine_code = (0b1110 << 28) |    // Condition
                  (0 << 27) |           // Data processing
                  (0 << 26) |
                  (0 << 25) |           // Not immediate
                  (set_flags << 20) |    // S bit
                  (0b0100 << 21) |   // Opcode
                  (rn << 16) |           // First operand register
                  (rd << 12) |           // Destination register
                  rm;              // Second operand
    
    return machine_code;
}
uint32_t generate_add_imm(int rd, int rn, int imm12) { 
    uint32_t machine_code = 0 ;
    
    // 立即數可用 8 位元直接表示
    uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
    uint32_t I = 0b1 << 25;           // I = 1 表示立即數
    uint32_t opcode = 0b0100 << 21;           // I = 1 表示立即數
    
    uint32_t S = 0 << 20;           // S = 0 不設定狀態位元
    uint32_t Rn = (rn & 0b1111) << 16; // Rd = 目標暫存器
    uint32_t Rd = (rd & 0b1111) << 12; // Rd = 目標暫存器
    uint32_t imm = imm12;  // 立即數 (8位)

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {

        Result result = rotate_and_return(imm12);
        //printf("part1: 0x%X, part2: 0x%02X\n", result.part1, result.part2);

        machine_code =   cond | I | opcode | S | Rn |Rd |(result.part1 << 8) | result.part2;
    }
    else {
        
        machine_code = cond | I | opcode | S | Rn | Rd | imm;}

    printf("Generated Machine Code: 0x%08X\n", machine_code);

    return machine_code;

    

}
uint32_t generate_rsb_imm(int rd, int rn, int imm12, int set_flags) { // sub r1, r1, imm
    uint32_t machine_code;
    
    // 先建立基礎的 machine_code
    machine_code = (0b1110 << 28) |   // Condition AL (Always)
                   (0b00 << 26)  |    // Data processing
                   (1 << 25)     |    // I (immediate operand flag)
                   (0b0011 << 21) |   // Opcode (SUB)
                   (set_flags << 20) |    // S bit
                   (rn << 16)     |   // Rn (first operand register)
                   (rd << 12);        // Rd (destination register)

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {
        Result result = rotate_and_return(imm12);
        
        // 使用 rotate_and_return 計算後的值
        machine_code |= (result.part1 << 8) | result.part2;
    } else {
        // 直接使用 12-bit 的立即數
        machine_code |= (imm12 & 0xFFF);
    }

    return machine_code;
}

uint32_t generate_sub_imm(int rd, int rn, int imm12, int set_flags) { // sub r1, r1, imm
    uint32_t machine_code;
    
    // 先建立基礎的 machine_code
    machine_code = (0b1110 << 28) |   // Condition AL (Always)
                   (0b00 << 26)  |    // Data processing
                   (1 << 25)     |    // I (immediate operand flag)
                   (0b0010 << 21) |   // Opcode (SUB)
                   (set_flags << 20) |    // S bit
                   (rn << 16)     |   // Rn (first operand register)
                   (rd << 12);        // Rd (destination register)

    if (is_legal_arm_immediate(imm12) && imm12 > 255) {
        Result result = rotate_and_return(imm12);
        
        // 使用 rotate_and_return 計算後的值
        machine_code |= (result.part1 << 8) | result.part2;
    } else {
        // 直接使用 12-bit 的立即數
        machine_code |= (imm12 & 0xFFF);
    }

    return machine_code;
}


uint32_t sub_reg_reg(int rd, int rn, int rm, int set_flags) { //  sub r1 r1 r0 , SUB & SUBS 差在 int set_flags
    uint32_t machine_code;
    
    // Build the instruction
    // [31:28] - condition (AL = 0xE) 1110 0000 010S 0001 0001 0000 0000 0000
    // [27:26] - 0b00 (Data processing)
    // [25]    - 0 (immediate operand flag)
    // [24]    - 0 (set flags)
    // [23:20] - opcode (SUB = 0x2)
    // [19:16] - Rn (first operand register)
    // [15:12] - Rd (destination register)
    // [11:0]  - operand2 (second operand)
    
    machine_code = (0b1110 << 28) |    // Condition
                  (0 << 27) |           // Data processing
                  (0 << 26) |
                  (0 << 25) |           // Not immediate
                  (set_flags << 20) |    // S bit
                  (0b0010 << 21) |   // Opcode
                  (rn << 16) |           // First operand register
                  (rd << 12) |           // Destination register
                  rm;              // Second operand
    
    return machine_code;
}



// 生成立即數左移指令: Rd = Rm LSL #imm
uint32_t generate_lsl_imm(uint32_t rd, uint32_t rm, uint32_t imm, uint32_t setflags) {
    uint32_t cond = 0b1110 << 28;        // 無條件執行 (4位)
    uint32_t op = 0b00 << 26;            // 數據處理指令
    uint32_t I = 0b0 << 25;              // 不是立即數操作數
    uint32_t opcode = 0b1101 << 21;      // MOV 操作碼
    uint32_t S = (setflags & 0b1) << 20; // 是否設置標誌位
    uint32_t Rn = 0b0000 << 16;          // MOV 指令不使用 Rn
    uint32_t Rd = (rd & 0xF) << 12;      // 目標寄存器
    uint32_t imm5 = (imm & 0b11111) << 7; // 位移量 (5 位)
    uint32_t shift_type = 0b000 << 4;     // LSL 的位移類型
    uint32_t Rm = rm & 0xF;              // 源寄存器

    // 返回完整的 32 位指令
    return cond | op | I | opcode | S | Rn | Rd | imm5 | shift_type | Rm;
}

// 生成寄存器控制的左移指令: Rd = Rn LSL Rm
uint32_t generate_lsl_reg_reg(uint32_t rd, uint32_t rn, uint32_t rm, uint32_t setflags) {
    uint32_t cond = 0b1110 << 28;        // 無條件執行 (4位)
    uint32_t op = 0b00 << 26;            // 數據處理指令
    uint32_t I = 0b0 << 25;              // 不是立即數操作數
    uint32_t opcode = 0b1101 << 21;      // MOV 操作碼
    uint32_t S = (setflags & 0b1) << 20; // 是否設置標誌位
    uint32_t Rn = 0b0000 << 16;          // MOV 指令不使用 Rn（對於寄存器移位）
    uint32_t Rd = (rd & 0xF) << 12;      // 目標寄存器
    uint32_t Rs = (rm & 0xF) << 8;       // 包含位移量的寄存器
    uint32_t shift_type = 0b01 << 4;     // 寄存器控制的 LSL 
    uint32_t Rm = rn & 0xF;              // 要移位的寄存器

    // 返回完整的 32 位指令
    return cond | op | I | opcode | S | Rn | Rd | Rs | shift_type | Rm;
}

// 生成立即數左移指令: Rd = Rm LSL #imm
uint32_t generate_lsr_imm(uint32_t rd, uint32_t rm, uint32_t imm, uint32_t setflags) {
    uint32_t cond = 0b1110 << 28;        // 無條件執行 (4位)
    uint32_t op = 0b00 << 26;            // 數據處理指令
    uint32_t I = 0b0 << 25;              // 不是立即數操作數
    uint32_t opcode = 0b1101 << 21;      // MOV 操作碼
    uint32_t S = (setflags & 0b1) << 20; // 是否設置標誌位
    uint32_t Rn = 0b0000 << 16;          // MOV 指令不使用 Rn
    uint32_t Rd = (rd & 0xF) << 12;      // 目標寄存器
    uint32_t imm5 = (imm & 0b11111) << 7; // 位移量 (5 位)
    uint32_t shift_type = 0b010 << 4;     // LSL 的位移類型
    uint32_t Rm = rm & 0xF;              // 源寄存器

    // 返回完整的 32 位指令
    return cond | op | I | opcode | S | Rn | Rd | imm5 | shift_type | Rm;
}

// 生成寄存器控制的左移指令: Rd = Rn LSL Rm
uint32_t generate_lsr_reg_reg(uint32_t rd, uint32_t rn, uint32_t rm, uint32_t setflags) {
    uint32_t cond = 0b1110 << 28;        // 無條件執行 (4位)
    uint32_t op = 0b00 << 26;            // 數據處理指令
    uint32_t I = 0b0 << 25;              // 不是立即數操作數
    uint32_t opcode = 0b1101 << 21;      // MOV 操作碼
    uint32_t S = (setflags & 0b1) << 20; // 是否設置標誌位
    uint32_t Rn = 0b0000 << 16;          // MOV 指令不使用 Rn（對於寄存器移位）
    uint32_t Rd = (rd & 0xF) << 12;      // 目標寄存器
    uint32_t Rs = (rm & 0xF) << 8;       // 包含位移量的寄存器
    uint32_t shift_type = 0b11 << 4;     // 寄存器控制的 LSL 
    uint32_t Rm = rn & 0xF;              // 要移位的寄存器

    // 返回完整的 32 位指令
    return cond | op | I | opcode | S | Rn | Rd | Rs | shift_type | Rm;
}







// 函數: 產生 MOV R0, #value 的 ARM32 機械碼
uint32_t generate_mov_reg_imm(uint32_t value, int saved_reg) {
	if (value <= 0xFF) {
		// 立即數可用 8 位元直接表示
		uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
		uint32_t I = 1 << 25;           // I = 1 表示立即數
		uint32_t opcode = 0b1101 << 21; // MOV 指令的操作碼 (4位)
		uint32_t S = 0 << 20;           // S = 0 不設定狀態位元
		uint32_t Rd = (saved_reg & 0b1111) << 12; // Rd = 目標暫存器
		uint32_t imm12 = value;  // 立即數 (8位)

		return cond | I | opcode | S | Rd | imm12;
	}
	else if (value <= 0xFFFF) {
		// 分離高位與低位
		uint32_t imm4 = (value >> 12) & 0xF;   // 高 4 位 (value[15:12])
		uint32_t imm12 = value & 0xFFF;        // 低 12 位 (value[11:0])

											   // MOV 指令格式
		uint32_t cond = 0b1110 << 28;          // 無條件執行 (Condition = 1110)
		uint32_t I = 1 << 25;                  // I = 1，表示立即數
		uint32_t opcode = 0b1000 << 21;        // MOV 指令的操作碼
		uint32_t S = 0 << 20;                  // S = 0，不設定狀態位元
		uint32_t Rd = (saved_reg & 0b1111) << 12; // 目標暫存器

												  // 正確位置的組合 MOVW 格式
		return cond | I | opcode | S | (imm4 << 16) | Rd | imm12;
	}
	else {
		return 0;
	}
}


uint32_t generate_ldr_reg(uint32_t output_reg, uint32_t input_reg, uint32_t offset) { // LDR R1, [R0] e5901000 

	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b010 << 25;           // LDR (immediate, ARM) 的 代碼
	uint32_t P = 0b1 << 24; // P 是否啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23; // U 位址增量方向：1 表示加（向上），0 表示減（向下）。
	uint32_t B = 0b0 << 22; // B 是否以位元組（Byte）為單位載入。1 表示位元組，0 表示字（Word）。 此為讀取Reg 所以採取0
	uint32_t W = 0b0 << 21; // W 是否啟用寫回（Write-back）模式。
	uint32_t L = 0b1 << 20; // L 	1 表示載入（LDR），0 表示儲存（STR）。
	uint32_t Rn = (input_reg & 0xF) << 16;           // 基址暫存器（Base Register）。 R0 = 0
	uint32_t Rt = (output_reg & 0xF) << 12;           // 目標暫存器（Destination Register）。 R1 = 1
	uint32_t imm12 = offset & 0xFFF; // imm12 = offset  => 偏移量



	return cond | type | P | U | B | W | L | Rn | Rt | imm12;
}

uint32_t generate_ldrb_reg(uint32_t output_reg, uint32_t input_reg, uint32_t offset) { // LDR R1, [R0] e5901000 

	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b010 << 25;           // LDR (immediate, ARM) 的 代碼
	uint32_t P = 0b1 << 24; // P 是否啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23; // U 位址增量方向：1 表示加（向上），0 表示減（向下）。
	uint32_t B = 0b1 << 22; // B 是否以位元組（Byte）為單位載入。1 表示位元組，0 表示字（Word）。 此為讀取Reg 所以採取0
	uint32_t W = 0b0 << 21; // W 是否啟用寫回（Write-back）模式。
	uint32_t L = 0b1 << 20; // L 	1 表示載入（LDR），0 表示儲存（STR）。
	uint32_t Rn = (input_reg & 0xF) << 16;           // 基址暫存器（Base Register）。 R0 = 0
	uint32_t Rt = (output_reg & 0xF) << 12;           // 目標暫存器（Destination Register）。 R1 = 1
	uint32_t imm12 = offset & 0xFFF; // imm12 = offset  => 偏移量



	return cond | type | P | U | B | W | L | Rn | Rt | imm12;
}


uint32_t generate_ldr_reg_regoffset(uint32_t output_reg, uint32_t input_reg, uint32_t reg_offset, uint32_t offset) {
	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b011 << 25;   // LDR (register offset, ARM) 的代碼
	uint32_t P = 0b1 << 24;        // 啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23;        // 位址增量方向：1 表示加（向上）
	uint32_t B = 0b0 << 22;        // 是否以位元組為單位載入：0 表示字（Word）
	uint32_t W = 0b0 << 21;        // 不啟用寫回（Write-back）
	uint32_t L = 0b1 << 20;        // 1 表示載入（LDR）
	uint32_t Rn = (input_reg & 0xF) << 16;  // 基址暫存器
	uint32_t Rt = (output_reg & 0xF) << 12; // 目標暫存器

											// 處理位移類型和位移量
	uint32_t shift_type = 0b00 << 5;      // LSL (Logical Shift Left)
	uint32_t imm5 = (offset & 0b11111) << 7; // 位移量 (5 位元)
	uint32_t Rm = reg_offset & 0xF;      // 偏移暫存器

										 // 返回完整的 32 位指令
	return cond | type | P | U | B | W | L | Rn | Rt | imm5 | shift_type | Rm;
}

uint32_t generate_ldrb_reg_regoffset(uint32_t output_reg, uint32_t input_reg, uint32_t reg_offset, uint32_t offset) {
	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b011 << 25;   // LDR (register offset, ARM) 的代碼
	uint32_t P = 0b1 << 24;        // 啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23;        // 位址增量方向：1 表示加（向上）
	uint32_t B = 0b1 << 22;        // 是否以位元組為單位載入：0 表示字（Word）
	uint32_t W = 0b0 << 21;        // 不啟用寫回（Write-back）
	uint32_t L = 0b1 << 20;        // 1 表示載入（LDR）
	uint32_t Rn = (input_reg & 0xF) << 16;  // 基址暫存器
	uint32_t Rt = (output_reg & 0xF) << 12; // 目標暫存器

											// 處理位移類型和位移量
	uint32_t shift_type = 0b00 << 5;      // LSL (Logical Shift Left)
	uint32_t imm5 = (offset & 0b11111) << 7; // 位移量 (5 位元)
	uint32_t Rm = reg_offset & 0xF;      // 偏移暫存器

										 // 返回完整的 32 位指令
	return cond | type | P | U | B | W | L | Rn | Rt | imm5 | shift_type | Rm;
}


uint32_t generate_str_reg(uint32_t output_reg, uint32_t input_reg, uint32_t offset) { // LDR R1, [R0] e5901000 

	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b010 << 25;           // LDR (immediate, ARM) 的 代碼
	uint32_t P = 0b1 << 24; // P 是否啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23; // U 位址增量方向：1 表示加（向上），0 表示減（向下）。
	uint32_t B = 0b0 << 22; // B 是否以位元組（Byte）為單位載入。1 表示位元組，0 表示字（Word）。 此為讀取Reg 所以採取0
	uint32_t W = 0b0 << 21; // W 是否啟用寫回（Write-back）模式。
	uint32_t L = 0b0 << 20; // L 	1 表示載入（LDR），0 表示儲存（STR）。
	uint32_t Rn = (input_reg & 0xF) << 16;           // 基址暫存器（Base Register）。 R0 = 0
	uint32_t Rt = (output_reg & 0xF) << 12;           // 目標暫存器（Destination Register）。 R1 = 1
	uint32_t imm12 = offset & 0xFFF; // imm12 = offset  => 偏移量



	return cond | type | P | U | B | W | L | Rn | Rt | imm12;
}

uint32_t generate_strb_reg(uint32_t output_reg, uint32_t input_reg, uint32_t offset) { // LDR R1, [R0] e5901000 

	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b010 << 25;           // LDR (immediate, ARM) 的 代碼
	uint32_t P = 0b1 << 24; // P 是否啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23; // U 位址增量方向：1 表示加（向上），0 表示減（向下）。
	uint32_t B = 0b1 << 22; // B 是否以位元組（Byte）為單位載入。1 表示位元組，0 表示字（Word）。 此為讀取Reg 所以採取0
	uint32_t W = 0b0 << 21; // W 是否啟用寫回（Write-back）模式。
	uint32_t L = 0b0 << 20; // L 	1 表示載入（LDR），0 表示儲存（STR）。
	uint32_t Rn = (input_reg & 0xF) << 16;           // 基址暫存器（Base Register）。 R0 = 0
	uint32_t Rt = (output_reg & 0xF) << 12;           // 目標暫存器（Destination Register）。 R1 = 1
	uint32_t imm12 = offset & 0xFFF; // imm12 = offset  => 偏移量



	return cond | type | P | U | B | W | L | Rn | Rt | imm12;
}

uint32_t generate_branch(int offset, uint8_t condition) { // 0xE  // Always   // Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
    uint32_t machine_code; 
    
    // Convert byte offset to word offset (divide by 4)
    offset = offset >> 2;
    
    // Adjust for pipeline (PC + 8)
    offset -= 2;
    
    // Extract 24-bit signed offset
    offset &= 0x00FFFFFF;
    
    // Build the instruction
    // [31:28] - condition
    // [27:24] - 0b1010 (branch instruction)
    // [23:0]  - offset
    machine_code = (condition << 28) | (0xA << 24) | (offset & 0x00FFFFFF);
    // Print machine code inside the function
    // printf("Generated Machine Code branch : 0x%08X\n", machine_code);
    return machine_code;
}



uint32_t generate_str_reg_regoffset(uint32_t output_reg, uint32_t input_reg, uint32_t reg_offset, uint32_t offset) {
	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b011 << 25;   // LDR (register offset, ARM) 的代碼
	uint32_t P = 0b1 << 24;        // 啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23;        // 位址增量方向：1 表示加（向上）
	uint32_t B = 0b0 << 22;        // 是否以位元組為單位載入：0 表示字（Word）
	uint32_t W = 0b0 << 21;        // 不啟用寫回（Write-back）
	uint32_t L = 0b0 << 20;        // 0 表示寫入（STR）
	uint32_t Rn = (input_reg & 0xF) << 16;  // 基址暫存器
	uint32_t Rt = (output_reg & 0xF) << 12; // 目標暫存器

											// 處理位移類型和位移量
	uint32_t shift_type = 0b00 << 5;      // LSL (Logical Shift Left)
	uint32_t imm5 = (offset & 0b11111) << 7; // 位移量 (5 位元)
	uint32_t Rm = reg_offset & 0xF;      // 偏移暫存器

										 // 返回完整的 32 位指令
	return cond | type | P | U | B | W | L | Rn | Rt | imm5 | shift_type | Rm;
}

uint32_t generate_strb_reg_regoffset(uint32_t output_reg, uint32_t input_reg, uint32_t reg_offset, uint32_t offset) {
	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
	uint32_t type = 0b011 << 25;   // LDR (register offset, ARM) 的代碼
	uint32_t P = 0b1 << 24;        // 啟用前置位址模式（Pre-indexing）
	uint32_t U = 0b1 << 23;        // 位址增量方向：1 表示加（向上）
	uint32_t B = 0b1 << 22;        // 是否以位元組為單位載入：0 表示字（Word）
	uint32_t W = 0b0 << 21;        // 不啟用寫回（Write-back）
	uint32_t L = 0b0 << 20;        // 0 表示寫入（STR）
	uint32_t Rn = (input_reg & 0xF) << 16;  // 基址暫存器
	uint32_t Rt = (output_reg & 0xF) << 12; // 目標暫存器

											// 處理位移類型和位移量
	uint32_t shift_type = 0b00 << 5;      // LSL (Logical Shift Left)
	uint32_t imm5 = (offset & 0b11111) << 7; // 位移量 (5 位元)
	uint32_t Rm = reg_offset & 0xF;      // 偏移暫存器

										 // 返回完整的 32 位指令
	return cond | type | P | U | B | W | L | Rn | Rt | imm5 | shift_type | Rm;
}

// uint32_t generate_strh_reg_regoffset(uint32_t output_reg, uint32_t input_reg, uint32_t reg_offset) {
//     uint32_t machine_code; 
// 	uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
// 	uint32_t type = 0b000 << 25;   // 
// 	uint32_t P = 0b1 << 24;        // 啟用前置位址模式（Pre-indexing）
// 	uint32_t U = 0b1 << 23;        // 位址增量方向：1 表示加（向上）
// 	uint32_t B = 0b0 << 22;        // 是否以位元組為單位載入：0 表示字（Word）
// 	uint32_t W = 0b0 << 21;        // 不啟用寫回（Write-back）
// 	uint32_t L = 0b0 << 20;        // 0 表示寫入（STR）
// 	uint32_t Rn = (input_reg & 0xF) << 16;  // 基址暫存器
// 	uint32_t Rt = (output_reg & 0xF) << 12; // 目標暫存器

// 											// 處理位移類型和位移量
// 	uint32_t shift_type = 0b1011 << 4;      // LSL (Logical Shift Left)

// 	uint32_t Rm = reg_offset & 0xF;      // 偏移暫存器

// 										 // 返回完整的 32 位指令
// 	machine_code =  cond | type | P | U | B | W | L | Rn | Rt  | shift_type | Rm;
//     printf("Generated Machine Code branch : 0x%08X\n", machine_code);
//     return machine_code;
// }


uint32_t generate_mvn_reg(int rd, int rm) { //NOT e1e01001  mvn r1,r1   = not r1 存入 r1
    uint32_t machine_code;
    
    // Build the instruction
    // Similar to SUB but with MVN opcode
    machine_code = (0b1110 << 28) |    // Condition
                  (0 << 27) |           // Data processing
                  (0 << 26) |
                  (0 << 25) |           // Not immediate
                  (0 << 20) |    // S bit
                  (0b1111<< 21) |   // Opcode
                  (0 << 16) |           // No Rn for MVN
                  (rd & 0xF) << 12 |
                  (rm & 0xF);

    
    return machine_code;
}



uint32_t generate_movt_reg_imm(uint32_t value, int saved_reg) {
    uint32_t high_value = (value >> 16) & 0xFFFF;  // 取得高 16 位元

    if (high_value <= 0xFFFF) { // 確保值在 16 位範圍內
        uint32_t cond = 0b1110 << 28;   // 無條件執行 (4位)
        uint32_t fixed = 0b00110100 << 20;    // 固定代碼 [27:20]
        uint32_t imm4 = (high_value >> 12) & 0xF;   // 取高 16 位中的高 4 位
        uint32_t Rd = (saved_reg & 0b1111) << 12;   // Rd = 暫存器編號 [15:12]
        uint32_t imm12 = high_value & 0xFFF;        // 取高 16 位中的低 12 位
        
        return cond | fixed | (imm4 << 16) | Rd | imm12;
    }
    return 0; // 如果數值超過範圍，返回 0
}


uint32_t encode_beq(int32_t target_offset) {
    // BEQ 指令的基本操作碼 (condition = 0000 for BEQ)
    uint32_t opcode = 0x0A000000;
    
    // 考慮 PC+8 的影響
    // 實際偏移量 = 目標偏移量 - 8
    int32_t actual_offset = target_offset - 8;
    
    // 指令偏移量必須是4的倍數
    if (actual_offset % 4 != 0) {
        printf("Warning: Offset must be multiple of 4\n");
        return 0;
    }
    
    // 計算相對偏移量 (除以4是因為ARM指令都是4位元組對齊)
    int32_t rel_offset = actual_offset / 4;
    
    // 檢查偏移量範圍
    if (rel_offset > 0x7FFFFF || rel_offset < -0x800000) {
        printf("Error: Offset too large for BEQ instruction\n");
        return 0;
    }
    
    // 取得偏移量的低24位
    uint32_t imm24 = rel_offset & 0xFFFFFF;
    
    // 組合最終的指令
    uint32_t instruction = opcode | imm24;

    return instruction;
}
uint32_t generate_cmp(uint32_t reg, uint32_t imm) {
    uint32_t cond = 0b1110 << 28;      // 無條件執行 (4位)
    uint32_t type = 0b00110101 << 20;  // CMP 指令的 op-code

    uint32_t Rn = (reg & 0xF) << 16;   // 比較的暫存器

    uint32_t machine_code = cond | type | Rn;  // 初始化指令碼

    uint32_t imm12 = imm & 0xFFF;  // 12-bit 立即數

    if (is_legal_arm_immediate(imm)) {
        Result result = rotate_and_return(imm);
        machine_code |= (result.part1 << 8) | result.part2;
    } else {
        machine_code |= imm12;
    }

    return machine_code;
}

uint32_t generate_cmp_reg(uint32_t rn, uint32_t rm) {
    uint32_t cond = 0b1110 << 28;   // ???蟡??行 (4位)
    uint32_t type = 0b00010101 << 20;   // LDR (register offset, ARM) 的代?

    uint32_t Rn = (rn & 0xF) << 16;  // 基址寄存器

    uint32_t Rm = (rm & 0xF); // 位移量 (12 位元)   // 使用 rm 而不是 Rm

    // 完整的 32 位指令
    uint32_t instruction = cond | type | Rn | Rm;

    // 打印完整指令
    // printf("cmp instruction: 0x%08X\n", instruction);

    // 返回完整的 32 位指令
    return instruction;
}

uint32_t mov_imm(uint32_t Index1, int reg_output) {
    uint32_t low_machine_code;
    uint16_t low_value = Index1 & 0xFFFF;         // 取得低 16 位

    // 處理低 16 位
    if (is_legal_arm_immediate(low_value) && low_value > 255) {
        low_machine_code = mov_encode_constant(low_value, reg_output);
    } else if (low_value <= 0xFF) {
        low_machine_code = generate_mov_reg_imm(low_value, reg_output);
    } else if (low_value <= 0xFFFF) {
        low_machine_code = generate_mov_reg_imm(low_value, reg_output);
    } else {
        printf("Invalid constant (%d) after fixup\n", low_value);
        return 0; // 無效情況設置為 0
    }

    return low_machine_code;
}

void write_to_array(uint32_t machine_code) {
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++; // 指向下一個位元組
    }
}



/*
  This is to record if there is such case of accessing user register area (address above 1,000,1000) during code generation.
  */
char isAccessingUsrRegArea;

#define HEADER_SIZE 1024

#define USR_REG_AREA_START  1000000


/*
  This routine is to attach a header area to plc generated binary.
  */



void GenHeader()
{
    char *code_ptr;
    code_ptr=(char *)binHeader;
    int codeNum=*code_ptr++;
    /* The number of bytes to fill by 0x00 in header area. */
    int fillNum=HEADER_SIZE-codeNum;
    while (codeNum > 0)
    {
        *pc_counter++=*code_ptr++;
        codeNum--;
    }
    while(fillNum >0)
    {
        *pc_counter++=0x00;
        fillNum--;
    }
    /* Write the identification into header. */
    BINHEADERCFG *binHeaderCfgPtr=(BINHEADERCFG *)work_mlc_ram;
    unsigned char headerIDBuf[]={'L','N','C',' ','l','c','o','d',0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA};
    memcpy(binHeaderCfgPtr->headerID,headerIDBuf,16);
    char compilerTypeStr[8]="lmlc";
    memcpy(binHeaderCfgPtr->compilerTypeStr,compilerTypeStr,8);
#ifdef QAPP_VERSION
    char compilerVerStr[16]=QAPP_VERSION;
    memcpy(binHeaderCfgPtr->compilerVersionStr,compilerVerStr,16);
#endif
    time_t rawTime;
    struct tm *timeInfo;
    time(&rawTime);
    timeInfo = localtime(&rawTime);
    binHeaderCfgPtr->year=1900+timeInfo->tm_year;
    binHeaderCfgPtr->mon=1+timeInfo->tm_mon;
    binHeaderCfgPtr->day=timeInfo->tm_mday;
    binHeaderCfgPtr->hour=timeInfo->tm_hour;
    binHeaderCfgPtr->min=timeInfo->tm_min;
    binHeaderCfgPtr->sec=timeInfo->tm_sec;
}

/*
  This is to attach the codes for functionality checking if neccessary.
  For example, there is case of accessing user register area (address above 1,000,000).
  */
void AttachHeadFuncConditionSeg()
{
    char *code_ptr;
    code_ptr=(char *)headerCondChk;
    int codeInsOffset=(int)(*code_ptr++);
    int codeNum=(int)(*code_ptr++);
    char *write_ptr=&work_mlc_ram[codeInsOffset];
    while(codeNum >0)
    {
        *write_ptr++=*code_ptr++;
        codeNum--;
    }
    BINHEADERCFG *binHeaderCfgPtr=(BINHEADERCFG *)work_mlc_ram;
    /* Write the user reg access type supported by this compiler kernel for checking from the loader side. */
    binHeaderCfgPtr->usrRegAccessTypeSupByComp=USRREGACCESSTYPE;

}

void PrintAddrIO( long arg )
{
    if( 0 <= arg && arg <= 0xFFF )
        fprintf(file_txt,"I %d\n", arg);
    if( 0x1000 <= arg && arg <= 0x1FFF )
        fprintf(file_txt,"O %d\n", arg-0x1000);
    if( 0x2000 <= arg && arg <= 0x2FFF )
        fprintf(file_txt,"C %d\n", arg-0x2000);
    if( 0x3000 <= arg && arg <= 0x3FFF )
        fprintf(file_txt,"S %d\n", arg-0x3000);
    if( 0x4000 <= arg && arg <= 0x4FFF )
        fprintf(file_txt,"A %d\n", arg-0x4000);
#ifdef M_BIT
    if( 0xA00 <= arg && arg <= 0xBFF )
        fprintf(file_txt,"M %d\n", arg-0xA00);
#endif
}

/*****************************************************************/
void get_por()   /* get  POR machine code */
{
    char *code_ptr;

    code_ptr=(char *)POR;
#ifdef VerText
    fprintf(file_txt,"POR\n");
#endif
    fprintf(plc_run_cpp,"    POR();\n");
    // get_code_0(code_ptr);

    // mov     cl,al

    uint32_t   mov_cl_al_machine_code =  generate_mov_reg_reg(4,5,0);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_cl_al_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }

    // POP    eax
    uint32_t pop_r9_machine_code = 0xE49D9004 ;
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (pop_r9_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    //or      al,cl

    uint32_t or_al_cl_machine_code = generate_or_reg(5,5,4,0);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( or_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


}
/***********************************************************************/
void get_pldtr( int t )   /* get  PLDTR machine code */
{
    char a,b,*code_ptr;
    NUM_TYPE  *temp_ptr;
    int s_a0_stus_s_ofs;  /* A5 : status store bit */
    s_a0_stus_s_ofs = (int)((long)s_mlc_stus_str - (long)MLC_Data);

    if(end_flag < 1) t+=255;  // 850508
    code_ptr=(char*)PLDTR;
#ifdef VerText
    fprintf(file_txt,"PLDTR   TR %d\n", t);
#endif
    fprintf(plc_run_cpp,"    PLDTR(Data+");
    a=*code_ptr++;
    b=*code_ptr++;

    while (a>0)
    {
        *pc_counter++=*code_ptr++;
        a--;
    }

    temp_ptr = (NUM_TYPE *)pc_counter;
    *temp_ptr++ = t + s_a0_stus_s_ofs;
    fprintf(plc_run_cpp,"%d);\n",t + s_a0_stus_s_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while (b>0)
    {
        *pc_counter++=*code_ptr++;
        b--;
    }
}
/***********************************************************************/
void get_ldtr( int t )   /* get  LDTR machine code */
{
    char buffer[256]; // 分配足夠的debug緩存
    char a,b,*code_ptr;
    NUM_TYPE *temp_ptr;
    int s_a0_stus_s_ofs;  /* A5 : status store bit */
    s_a0_stus_s_ofs = (int)((long)s_mlc_stus_str - (long)MLC_Data);


    if(end_flag < 1) t+=255;  // 850508

    code_ptr=(char *)LDTR;
#ifdef VerText
    fprintf(file_txt,"LDTR    TR %d\n", t);
#endif
    fprintf(plc_run_cpp,"    LDTR(Data+");
    a=*code_ptr++;
    b=*code_ptr++;

    // while (a>0)
    // {
    //     *pc_counter++=*code_ptr++;
    //     a--;
    // }

    // temp_ptr = (NUM_TYPE *)pc_counter;
    // *temp_ptr++ = t + s_a0_stus_s_ofs;
    // fprintf(plc_run_cpp,"%d);\n",t + s_a0_stus_s_ofs);
    // pc_counter+=NUM_SIZE;
    // code_ptr+=NUM_SIZE;

    // while (b>0)
    // {
    //     *pc_counter++=*code_ptr++;
    //     b--;
    // }

    sprintf(buffer, "%s 0x%lx ", "20250119 type=LDTR *Reg=", t + s_a0_stus_s_ofs); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

    uint32_t esi_cc_index_low_machine_code = mov_imm(t + s_a0_stus_s_ofs,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (esi_cc_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		// movt r7 , esi+cc 移動esi+cc之高16位元
	uint32_t esi_cc_index_high_machine_code = generate_movt_reg_imm(t + s_a0_stus_s_ofs, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (esi_cc_index_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	  
    
    	// LDR r5, [r6, r7]  @ Load Register value
    uint32_t ldrb_esi_cc_index_machine_code = generate_ldrb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( ldrb_esi_cc_index_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	




}
/***********************************************************************/
void get_outr( int t )   /* get OUTR machine code */
{
    char buffer[256]; // 分配足夠的debug緩存
    char a,b,*code_ptr;
    NUM_TYPE *temp_ptr;
    long s_a0_stus_s_ofs;  /* A5 : status store bit */
    s_a0_stus_s_ofs = (int)((long)s_mlc_stus_str - (long)MLC_Data);
    temp_reg_stack = (char)t;


    if(end_flag < 1) t+=255;  // 850508
    //     code_ptr=(char *)OUTR;
#ifdef VerText
    fprintf(file_txt,"OUTR    TR %d\n",t);
#endif
    fprintf(plc_run_cpp,"    OUTR(Data+");
    a=*code_ptr++;
    b=*code_ptr++;

    // while (a>0)
    // {
    //     *pc_counter++=*code_ptr++;
    //     a--;
    // }

    // temp_ptr = (NUM_TYPE *)pc_counter;
    *temp_ptr++=t + s_a0_stus_s_ofs;
    fprintf(plc_run_cpp,"%d);\n",t + s_a0_stus_s_ofs);
    // pc_counter+=NUM_SIZE;
    // code_ptr+=NUM_SIZE;

    // mov     [ebx+esi+cc],al
    
        //mov r7 , ebx+esi+cc 移動esi+cc之低16位元

    uint32_t esi_cc_index_low_machine_code = mov_imm(t + s_a0_stus_s_ofs,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (esi_cc_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt r7 , esi+cc 移動esi+cc之高16位元
	uint32_t esi_cc_index_high_machine_code = generate_movt_reg_imm(t + s_a0_stus_s_ofs, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (esi_cc_index_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    
    	// LDR r5, [r6, r7]  @ Load Register value
    uint32_t strb_esi_cc_index_machine_code = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (strb_esi_cc_index_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    // while (b>0)
    // {
    //     *pc_counter++=*code_ptr++;
    //     b--;
    // }
    sprintf(buffer, "%s 0x%lx", "20250119 func=OUTR t + s_a0_stus_s_ofs=", t + s_a0_stus_s_ofs); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


}


/*******************************************************************/


int get_ld(struct contact *cont_ptr){
    //char a,b,c,*code_ptr;
    long *Reg;
    NUM_TYPE *temp_ptr;
    uint32_t low_machine_code, high_machine_code, machine_code2;
    char buffer[256]; // 分配足夠的debug緩存
    char a,b,c,*code_ptr;
    long s_a0_cnt_s_ofs;
    long s_a0_tmr_s_ofs;
    long s_a0_reg_ofs;
    char Bit;
    long a1,a2,a3,a4;
 
    a1=(long)s_mlc_cnt_stus;
    a2=(long)s_mlc_tmr_stus;
    a4=(long)s_mlc_reg;
    a3=(long)MLC_Data;
    s_a0_cnt_s_ofs=(long)((long)a1-(long)a3);
    s_a0_tmr_s_ofs=(long)((long)a2-(long)a3);
    s_a0_reg_ofs=(long)((long)a4-(long)a3);
 

    if (cont_ptr->data.code==0x0D)    /* ---- code */
    return(ERROR);
 
    uint32_t reg_t;

    if ( cont_ptr->data.code == 1 || cont_ptr->data.code == 3 )
    {
       //20241119 jeremy
 
       
       Reg =(long*)cont_ptr->arg;
       ////add by ted for Level 2,3 read I'(I2048~I4095) ps. copy from I0~I2047
       //if (iLevel>0 & *Reg < 0x10000) //非第一個level而且是I點
       //  *temp_ptr++=(*Reg + 0x800);                       // -| |-  & -|/|- //
       //else
       ////
       reg_t=*Reg;                       // -| |-  & -|/|- //

    }
    else if ( cont_ptr->data.code == 9 || cont_ptr->data.code == 0x0A)
    {
       Reg =(long*)cont_ptr->arg;
       //*temp_ptr++=(*Reg*4)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
       reg_t=(*Reg*2)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
                                                /* timer status bit buffer */
                                 
    }
    else if ( cont_ptr->data.code == 0x0B || cont_ptr->data.code == 0x0C)
    {
       Reg =(long*)cont_ptr->arg;
       //*temp_ptr++=(*Reg*4)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
       reg_t=(*Reg*2)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
                                              /* counter status bit buffer */
                                    
    }
    else if (cont_ptr->data.code==0x16 || cont_ptr->data.code==0x17)
    {
       Reg =(long*)cont_ptr->arg;
       /* Adjust for user register area. This would modified the source in the ram.*/
       if(USR_REG_AREA_START <= *Reg)
       {
           *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
           isAccessingUsrRegArea=1;
       }
       reg_t=(*Reg*4)+s_a0_reg_ofs;/* -|R|-  & -|R/|- */

    }    

    
    sprintf(buffer, "%s 0x%lx %s 0x%x", "20250119 func=get_ld type=LD *Reg=", *Reg," reg_t=", reg_t); // 格式化字符串




 if (cont_ptr->data.code==0x16)
 {
//          code_ptr=(char*)LDR;
// #ifdef VerText
//        fprintf(file_txt,"LDR     ");
// #endif
//        fprintf(plc_run_cpp,"    LDR(*(long*)(Data+");
 }
 else if (cont_ptr->data.code==0x17)
 {
//          code_ptr=(char *)LDRNOT;
// #ifdef VerText
//        fprintf(file_txt,"LDRNOT  ");
// #endif
//        fprintf(plc_run_cpp,"    LDRNOT(*(long*)(Data+");
 }
 else if (cont_ptr->data.code==1 || cont_ptr->data.code==9 || //LD
     cont_ptr->data.code==0x0B )      /*  -| |- , -|T|- , -|C|- */
    {
        //code_ptr=(char *)LD;//machine code address
        Reg =(long*)cont_ptr->arg;//memory/registerget address
        //uint32_t offset = 3000; // 十進制的3000
        //uint32_t data_A = 0x4000;
        //uint32_t Index1 = data_A + offset; //A3000
        //20250119 arm code : Mov, MovT

        OutputDebugStringA(buffer); // 輸出到 DebugView
        low_machine_code = mov_imm(reg_t,  7);
               //movt r7 , A3000 移動A3000之高16位元
        //存 code
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        high_machine_code = generate_movt_reg_imm(reg_t, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=get_ld type=LD low_machine_code=", low_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        sprintf(buffer, "%s 0x%lx", "20250119 func=get_ld type=LD  high_machine_code",  high_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        machine_code2 = generate_ldrb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_code2 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        sprintf(buffer, "%s 0x%x", "20250119 func=get_ld type=LD machine_code2=", machine_code2); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        // and r5, r5 , #0xff  //取A3000的位址 因ldr一次讀取4個byte

    }
 else
    {  //LDNOT
        //code_ptr=(char *)LD;//machine code address
        Reg =(long*)cont_ptr->arg;//memory/registerget address
        //uint32_t offset = 3000; // 十進制的3000
        //uint32_t data_A = 0x4000;
        //uint32_t Index1 = data_A + offset; //A3000
        //20250119 arm code : Mov, MovT




        OutputDebugStringA(buffer); // 輸出到 DebugView
        low_machine_code = mov_imm(reg_t,  7);
               //movt r7 , A3000 移動A3000之高16位元
        //存 code
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        high_machine_code = generate_movt_reg_imm(reg_t, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=get_ld type=LD low_machine_code=", low_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        sprintf(buffer, "%s 0x%lx", "20250119 func=get_ld type=LD  high_machine_code",  high_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        machine_code2 = generate_ldrb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_code2 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        sprintf(buffer, "%s 0x%x", "20250119 func=get_ld type=LD machine_code2=", machine_code2); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        // and r5, r5 , #0xff  //取A3000的位址 因ldr一次讀取4個byte
        uint32_t mvn_al_machine_code = generate_mvn_reg(5,5);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mvn_al_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        uint32_t al_0xff_machine_code = generate_and_imm(5,5,0x01);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_0xff_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    }




    return 0;
}

/***********************************************************************/
int get_ld_x86( struct contact *cont_ptr )        /* get LD or LDNOT machine code */
{
   char a,b,c,*code_ptr;
   NUM_TYPE *temp_ptr;
   long s_a0_cnt_s_ofs;
   long s_a0_tmr_s_ofs;
   long s_a0_reg_ofs;
   long *Reg;
   char Bit;
   long a1,a2,a3,a4;

   a1=(long)s_mlc_cnt_stus;
   a2=(long)s_mlc_tmr_stus;
   a4=(long)s_mlc_reg;
   a3=(long)MLC_Data;
   s_a0_cnt_s_ofs=(long)((long)a1-(long)a3);
   s_a0_tmr_s_ofs=(long)((long)a2-(long)a3);
   s_a0_reg_ofs=(long)((long)a4-(long)a3);

   if (cont_ptr->data.code==0x0D)    /* ---- code */
      return(ERROR);
   
   if (cont_ptr->data.code==0x16)
   {
   	    code_ptr=(char*)LDR;
#ifdef VerText
         fprintf(file_txt,"LDR     ");
#endif
         fprintf(plc_run_cpp,"    LDR(*(long*)(Data+");
   }
   else if (cont_ptr->data.code==0x17)
   {
   	    code_ptr=(char *)LDRNOT;
#ifdef VerText
         fprintf(file_txt,"LDRNOT  ");
#endif
         fprintf(plc_run_cpp,"    LDRNOT(*(long*)(Data+");
   }
   else if (cont_ptr->data.code==1 || cont_ptr->data.code==9 ||
       cont_ptr->data.code==0x0B )      /*  -| |- , -|T|- , -|C|- */
      {
         code_ptr=(char *)LD;
#ifdef VerText
         fprintf(file_txt,"LD      ");
#endif
         fprintf(plc_run_cpp,"    LD(Data+");
      }
   else
      {
         code_ptr=(char *)LDNOT;
#ifdef VerText
         fprintf(file_txt,"LDNOT   ");
#endif
         fprintf(plc_run_cpp,"    LDNOT(Data+");
      }

#ifdef VerText
// fprintf(file_txt,"I %d\n", cont_ptr->arg1);
   Reg = (long*)cont_ptr->arg;
   PrintAddrIO(*Reg);
#endif

   a=*code_ptr++;
   b=*code_ptr++;
   if (cont_ptr->data.code==0x16 || cont_ptr->data.code==0x17)
   {
       c=*code_ptr++;
   }
   
   while (a>0)
   {
      *pc_counter++=*code_ptr++;
      a--;
   }

   temp_ptr = (NUM_TYPE *)pc_counter;

   if ( cont_ptr->data.code == 1 || cont_ptr->data.code == 3 )
   {
      //20241119 jeremy

      
      Reg =(long*)cont_ptr->arg;
      ////add by ted for Level 2,3 read I'(I2048~I4095) ps. copy from I0~I2047
	  //if (iLevel>0 & *Reg < 0x10000) //非第一個level而且是I點
      //  *temp_ptr++=(*Reg + 0x800);                       // -| |-  & -|/|- //
	  //else
	  ////
        *temp_ptr++=*Reg;                       // -| |-  & -|/|- //
	    fprintf(plc_run_cpp,"%d);\n",*Reg);
   }
   else if ( cont_ptr->data.code == 9 || cont_ptr->data.code == 0x0A)
   {
      Reg =(long*)cont_ptr->arg;
      //*temp_ptr++=(*Reg*4)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
      *temp_ptr++=(*Reg*2)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
                                               /* timer status bit buffer */
      fprintf(plc_run_cpp,"%d);\n",(*Reg*2)+s_a0_tmr_s_ofs+1);                                         
   }
   else if ( cont_ptr->data.code == 0x0B || cont_ptr->data.code == 0x0C)
   {
      Reg =(long*)cont_ptr->arg;
      //*temp_ptr++=(*Reg*4)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
      *temp_ptr++=(*Reg*2)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
                                             /* counter status bit buffer */
      fprintf(plc_run_cpp,"%d);\n",(*Reg*2)+s_a0_cnt_s_ofs+1);                                       
   }
   else if (cont_ptr->data.code==0x16 || cont_ptr->data.code==0x17)
   {
      Reg =(long*)cont_ptr->arg;
      /* Adjust for user register area. This would modified the source in the ram.*/
      if(USR_REG_AREA_START <= *Reg)
      {
          *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
          isAccessingUsrRegArea=1;
      }
      *temp_ptr++=(*Reg*4)+s_a0_reg_ofs;/* -|R|-  & -|R/|- */
      fprintf(plc_run_cpp,"%d),",(*Reg*4)+s_a0_reg_ofs);
   }
   else    *temp_ptr++=0;  /* for test ... */

   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while (b>0)
   {
      *pc_counter++=*code_ptr++;
      b--;
   }
   
   if (cont_ptr->data.code==0x16 || cont_ptr->data.code==0x17)
   {
   	  Bit = (char)(cont_ptr->arg[5]);
      *pc_counter++=Bit;
      fprintf(plc_run_cpp,"%d);\n",Bit);
      code_ptr++;
      while (c>0)
      {
          *pc_counter++=*code_ptr++;
          c--;
      }
   }

   return 0;
}
/*******************************************************************/
int get_and_out( struct contact *cont_ptr )   /* get AND or OUT machine code */
{
    char buffer[256]; // 分配足夠的debug緩存
    long s_a0_cnt_s_ofs ;
    long s_a0_tmr_s_ofs ;
    long s_a0_one_shot ;
    long *Reg;
    char Bit;
    char x,y,z,i,j,k;
    char a,b,c,*code_ptr;
    NUM_TYPE *temp_ptr;
    long int d,f;
    long int a1,a2,a3,a4;

    a1=(long)s_mlc_cnt_stus;
    a2=(long)s_mlc_tmr_stus;
    a3=(long)MLC_Data;
    a4=(long)s_mlc_one_shot;

    s_a0_cnt_s_ofs=(long)((long)a1-(long)a3);
    s_a0_tmr_s_ofs=(long)((long)a2-(long)a3);
    s_a0_one_shot=(long)((long)a4-(long)a3);

    d=(long)cont_ptr;
    if (cont_ptr->data.code==0x0D)
        return(ERROR);                 /* ---- code */
    f=(long)pc_edit_adr;
    d=d+8-f;


    sprintf(buffer, "get_and_out"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

    sprintf(buffer, " cont_ptr->data.code=%d" , cont_ptr->data.code);
    OutputDebugStringA(buffer);



    if ( d%80==0 ) {     /* last element output coil */
        if (!( (cont_ptr->data.code>=0x05 && cont_ptr->data.code<=0x08) || //(^),(v)
               (cont_ptr->data.code>=0x0E && cont_ptr->data.code<=0x15) || //( ),(/),(L),(UL),(RBitL),(RBitUL)
               (cont_ptr->data.code>=0x18 && cont_ptr->data.code<=0x19)    //(R),(R/)
             )
           )
            return (ERROR);              /* not output coil */

        if ((cont_ptr->data.code>=0x05)&&(cont_ptr->data.code<=0x08))
        {
            switch ( cont_ptr->data.code )
            {
                case 5:
                case 6:
                    code_ptr=(char *)OFF_ON;     /* -(^)- : 5H */
#ifdef VerText
                    fprintf(file_txt,"OFF_ON  ");
#endif

                    sprintf(buffer, "off_on"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView

                    fprintf(plc_run_cpp,"    OFF_ON(Data+");
                    break;
                case 7:
                case 8:
                    code_ptr=(char *)ON_OFF;     /* -(v)- : 7H */
#ifdef VerText
                    fprintf(file_txt,"ON_OFF  ");
#endif

                    sprintf(buffer, "on_off"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView

                    fprintf(plc_run_cpp,"    ON_OFF(Data+");
                    break;
            }
#ifdef VerText
            fprintf(file_txt,"O %d\n", one_shot_no);
#endif
            a=*code_ptr++;
            b=*code_ptr++;
            c=*code_ptr++;
            sprintf(buffer, "on_off a=%d b=%d c=%d", a, b, c);
            OutputDebugStringA(buffer);
            sprintf(buffer, "on_off cont_ptr->data.code=%d" , cont_ptr->data.code);
            OutputDebugStringA(buffer);
            

            // while ( a>0 ) {
            //     *pc_counter++=*code_ptr++;
            //     a--;
            // }

            // temp_ptr = (NUM_TYPE *)pc_counter;
            // *temp_ptr++ = one_shot_no + s_a0_one_shot;
            // fprintf(plc_run_cpp,"%d,Data+",one_shot_no + s_a0_one_shot);
            // pc_counter+=NUM_SIZE;
            // code_ptr+=NUM_SIZE;

            uint32_t Index1_low_machine_code = mov_imm(one_shot_no + s_a0_one_shot,  7);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( Index1_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }                    
            uint32_t Index1_high_machine_code = generate_movt_reg_imm(one_shot_no + s_a0_one_shot, 7);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( Index1_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }        
            uint32_t ldrb_cc_index_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( ldrb_cc_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }        
            uint32_t strb_cc_index_machine_code = generate_strb_reg_regoffset(5, 6, 7, 0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( strb_cc_index_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }        

            // while ( b>0 ){
            //     *pc_counter++=*code_ptr++;
            //     b--;
            // }

            // temp_ptr = (NUM_TYPE *)pc_counter;
            // *temp_ptr++ = one_shot_no + s_a0_one_shot;
            // pc_counter+=NUM_SIZE;
            // code_ptr+=NUM_SIZE;
            if (cont_ptr->data.code == 5 || cont_ptr->data.code == 6) { // off_on
                uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
                // 這裡應該填入實際的程式碼
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( mvn_cl_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }        


            } 
            else if (cont_ptr->data.code == 7 || cont_ptr->data.code == 8) { //on_off
                uint32_t mvn_al_machine_code= generate_mvn_reg(5,5); 
                // 這裡應該填入實際的程式碼
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( mvn_al_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }        
                        


            }
            uint32_t al_8bit = generate_and_imm(5,5,0x01);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_8bit>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }                    
            uint32_t cl_8bit = generate_and_imm(4,4,0x01);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( cl_8bit>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }        
            uint32_t al_and_cl = generate_and_reg(5,4,5,0) ;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_and_cl>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }        
                        

            // while ( c>0 ){
            //     *pc_counter++=*code_ptr++;
            //     c--;
            // }

            // temp_ptr = (NUM_TYPE *)pc_counter;
            Reg = (long*)(cont_ptr->arg);
            *temp_ptr++ = *Reg;
            // fprintf(plc_run_cpp,"%d);\n",*Reg);
            // pc_counter+=NUM_SIZE;
            // code_ptr+=NUM_SIZE;

        uint32_t mov_dddd_low = mov_imm(*Reg, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( mov_dddd_low>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }        

        uint32_t mov_dddd_high = generate_movt_reg_imm(*Reg, 7); // 設置高16位
            sprintf(buffer, "*Reg=%d" ,*Reg);
            OutputDebugStringA(buffer);        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( mov_dddd_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        uint32_t str_al_dddd_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( str_al_dddd_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


            one_shot_no++;
        }
        else if ((cont_ptr->data.code>=0x0E)&&(cont_ptr->data.code<=0x15))
        {
            switch( cont_ptr->data.code )
            {
                case 0x0E:
                case 0x0F:
                {  //OUTF


            Reg = (long*)cont_ptr->arg;
            /* Adjust for user register area. This would modified the source in the ram.*/
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }

            // mov     [ebx+dddd],al

            sprintf(buffer, "%s 0x%lx ", "OUTR address  = ", *Reg); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView


            uint32_t eax_index_low_machine_code = mov_imm(*Reg,  7);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }



            uint32_t eax_index_high_machine_code = generate_movt_reg_imm(*Reg, 7);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }



                // [ebx+dddd],al
                uint32_t str_al_index_machine_code = generate_strb_reg_regoffset(5, 6, 7, 0);

                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_al_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }





                }
                case 0x10:
                case 0x11:
                {


            
                    uint32_t   mov_cl_al_machine_code =  generate_mov_reg_reg(4,5,0);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (mov_cl_al_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
        
        
                    uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
                Reg = (long*)cont_ptr->arg;
                /* Adjust for user register area. This would modified the source in the ram.*/
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
    
        
        
        
        // mov     [ebx+dddd],al
        
                    sprintf(buffer, "%s 0x%lx ", "OUTR address  = ", *Reg); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView
        
        
                    uint32_t eax_index_low_machine_code = mov_imm(*Reg,  7);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
        
        
        
                    uint32_t eax_index_high_machine_code = generate_movt_reg_imm(*Reg, 7);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
        
        
        
                        // [ebx+dddd],al
                        uint32_t str_al_index_machine_code = generate_strb_reg_regoffset(4, 6, 7, 0);
        
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (str_al_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
        

                    break;}
                case 0x12:
                    code_ptr=(char *)LATCH;   /* -(L)- : 12H */
#ifdef VerText
                    fprintf(file_txt,"LATCH   ");
#endif
                    fprintf(plc_run_cpp,"    LATCH(Data+");

                    sprintf(buffer, "latch"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView



                    break;
                case 0x13:            /* -(RBitL)- : 13H */
                    code_ptr=(char *)RBITLATCH;
#ifdef VerText
                    fprintf(file_txt,"RBITLATCH    ");
#endif
                    break;

                case 0x14:
                    code_ptr=(char *)UNLATCH; /* -(UL)- : 14H */
#ifdef VerText
                    fprintf(file_txt,"UNLATCH ");
#endif


                    sprintf(buffer, "unlatch"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView

                    fprintf(plc_run_cpp,"    UNLATCH(Data+");
                    break;
                case 0x15:
                    code_ptr=(char *)RBITUNLATCH; /* -(RBitUL)- : 15H */
#ifdef VerText
                    fprintf(file_txt,"UNLATCH ");
#endif
                    break;
            }
            /* RBit latch/unlatch. */
            if((0x13==cont_ptr->data.code) || (0x15==cont_ptr->data.code))
            {
                Reg = (long*)(cont_ptr->arg);
                /* Adjust for user register area. This would modified the source in the ram.*/
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                Bit = (char)(cont_ptr->arg[5]);
#ifdef VerText
               fprintf(file_txt,"R %d Bit %d\n", Reg,(int)Bit);
#endif

                a=*code_ptr++;
                b=*code_ptr++;
                c=*code_ptr++;
                while(a>0)
                {
                    *pc_counter++=*code_ptr++;
                    a--;
                }
                /* Set bit. */
                *pc_counter++=Bit;
                code_ptr+=1;
                while(b>0)
                {
                    *pc_counter++=*code_ptr++;
                    b--;
                }
                temp_ptr = (NUM_TYPE *)pc_counter;
                *temp_ptr++=(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data);
                pc_counter+=NUM_SIZE;
                code_ptr+=NUM_SIZE;
                while(c>0)
                {
                    *pc_counter++=*code_ptr++;
                    c--;
                }
                temp_ptr = (NUM_TYPE *)pc_counter;
                *temp_ptr++=(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data);
                pc_counter+=NUM_SIZE;
                code_ptr+=NUM_SIZE;









            }//RBit latch/unlatch
            else
            {


            Reg = (long*)cont_ptr->arg;


            sprintf(buffer, "latch/unlatch  cont_ptr->data.code=%d" , *Reg);
            OutputDebugStringA(buffer);
            if (cont_ptr->data.code == 0x12) { // latch
                //  LD[]  ebx+dddd      讀取 [EBX+dddd] 的 8 位元數據到 R7
                    //mov r7 , A3000 移動A3000之低16位元
            sprintf(buffer, "latch");
            OutputDebugStringA(buffer);

                uint32_t dddd_index_low_machine_code = mov_imm(*Reg,  7);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( dddd_index_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }                       
                    //movt r7 , A3000 移動A3000之高16位元
                uint32_t dddd_index_high_machine_code = generate_movt_reg_imm(*Reg, 7);
                         for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( dddd_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }                      
                
                    // LDR r5, [r6, r7]  @ Load Register value
                uint32_t ldrb_dddd_index_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( ldrb_dddd_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }       

                    // OR 運算：R5 = R5| R7 (AL = AL | [EBX+dddd])
                uint32_t dddd_or_al = generate_or_reg(5,5,7,0);
                    // 存回結果到記憶體 [EBX+dddd]
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( dddd_or_al>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }       


                uint32_t mov_dddd_low = mov_imm(*Reg, 7); // 設置低16位
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (  mov_dddd_low>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }                       

                uint32_t mov_dddd_high = generate_movt_reg_imm(*Reg , 7); // 設置高16位
                         for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( mov_dddd_high>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }                      

                uint32_t al_8bit = generate_and_imm(5,5,0x01);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( al_8bit>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }       

                uint32_t str_al_dddd_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( str_al_dddd_1>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }        


            }

            else if (cont_ptr->data.code == 0x14) { //unlatch

            sprintf(buffer, "unlatch");
            OutputDebugStringA(buffer);
                //cmp     al,0
                    // CMP R5, #0  比對al 跟 0 
                uint32_t machine_cmp = generate_cmp(5, 0);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( machine_cmp>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }                        
                
                    // BEQ instruction #跳轉指令
                uint32_t machine_beq = encode_beq(6*4 );
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( machine_beq>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }                   

                    // not al
                uint32_t mvn_al_machine_code= generate_mvn_reg(5,5);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( mvn_al_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }                   

                uint32_t al_8bit = generate_and_imm(5,5,0x01);
                 for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (al_8bit >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }                  
                
                    // MOV [ebx+dddd],al

                uint32_t mov_dddd_low = mov_imm(*Reg, 7); // 設置低16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( mov_dddd_low>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }   

                uint32_t mov_dddd_high = generate_movt_reg_imm(*Reg, 7); // 設置高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( mov_dddd_high>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }   

                uint32_t str_al_dddd_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (  str_al_dddd_1>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }   



 
                        


            }





            }//else RBit latch/unlatch
        }
        else //(R),(R/)
        {
            switch( cont_ptr->data.code )
            {
                case 0x18: {
                    code_ptr=(char *)OUTRF;      /* -(R)- */




                    sprintf(buffer, "%s ", "OUTRF"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView
                
                
                    // cmp     al,0
                    uint32_t machine_cmp = generate_cmp(5, 0);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                    }
                
                    uint32_t machine_beq = encode_beq(12*4);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    // mov     eax,1
                    uint32_t eax_1_low_machine_code_1 = mov_imm(1,  9);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_1_low_machine_code_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t eax_1_high_machine_code_1 = generate_movt_reg_imm(0, 9);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_1_high_machine_code_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    Bit = (char)(cont_ptr->arg[5]); 
                

                    
                    sprintf(buffer,   "  type = Bit %d ", Bit);
                    OutputDebugStringA(buffer); // 輸出到 DebugView

                    // mov     cl,0xff
                
                    uint32_t cl_0xff_low_machine_code = mov_imm(Bit,  4);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (cl_0xff_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t cl_0xff_high_machine_code = generate_movt_reg_imm(Bit, 4);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (cl_0xff_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // shl     eax,cl = lsl r9,r9,r4
                    uint32_t lsl_eax_cl_machine_code = generate_lsl_reg_reg(9,9,4,0);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (lsl_eax_cl_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                
                    // mov     ecx,[ebx+dddd]
                
                    Reg = (long*)(cont_ptr->arg);
                    /* Adjust for user register area. This would modified the source in the ram.*/
                    if(USR_REG_AREA_START <= *Reg)
                    {
                        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                        isAccessingUsrRegArea=1;
                    }
                
                    /* get source */ //ecx
                    uint32_t dddd_index_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t dddd_index_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_dddd_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_dddd_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // or      eax,ecx
                    uint32_t or_eax_ecx_machine_code =  generate_or_reg(9,9,10,0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( or_eax_ecx_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    // [ebx+dddd],eax
                    uint32_t str_eax_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (str_eax_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    // jmp     OUTRF_end
                    uint32_t jmp_end = generate_branch(23*4, 0xe); // Always (0xE)
                    //jmp  addW2   
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (jmp_end  >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }	
                
                
                
                    //  OUTRF_OFF:
                
                    // mov     eax,[ebx+dddd]
                    /* get source */ //eax
                    uint32_t eax_index_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // mov     cl,0xFF
                        Bit = (char)(cont_ptr->arg[5]);
                        // mov     cl,0xff
                
                        uint32_t cl_low_machine_code = mov_imm(Bit,  4);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t cl_high_machine_code = generate_movt_reg_imm(Bit, 4);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                        // shr     eax,cl = lsr r9,r9,r4
                        uint32_t lsr_machine_code = generate_lsr_reg_reg(9,9,4,0);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (lsr_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                        // and     eax,1
                        uint32_t and_eax_1_machine_code = generate_and_imm(9, 9, 0x1);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (and_eax_1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                        // cmp     eax,0
                        uint32_t machine_cmp_eax_0 = generate_cmp(9, 0);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (machine_cmp_eax_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                        }
                
                    // je      OUTRF_0  // beq = 0x0
                    uint32_t branch_OUTRF_0_machine_code = generate_branch(14 * 4, 0x0) ;
                    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (branch_OUTRF_0_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }  
                
                
                        // mov     eax,1
                
                        uint32_t eax_1_low_machine_code = mov_imm(1,  9);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (eax_1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t eax_1_high_machine_code = generate_movt_reg_imm(0, 9);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (eax_1_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                
                
                        // mov     cl,0xFF
                        Bit = (char)(cont_ptr->arg[5]);
                        // mov     cl,0xff
                
                        uint32_t cl_bit_low_machine_code = mov_imm(Bit,  4);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = ( cl_bit_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t cl_bit_high_machine_code = generate_movt_reg_imm(Bit, 4);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_bit_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                
                    // shl     eax,cl = lsl r9,r9,r4
                    uint32_t lsl_eax_cl_machine_code_2 = generate_lsl_reg_reg(9,9,4,0);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (lsl_eax_cl_machine_code_2 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    
                
                    // not     eax
                    uint32_t not_eax_machine_code = generate_mvn_reg(9, 9);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (not_eax_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                
                    // mov     ecx,[ebx+dddd]
                    uint32_t ecx_dddd_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ecx_dddd_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t ecx_dddd_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ecx_dddd_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_ecx_dddd_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_ecx_dddd_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // and     eax,ecx
                    // and     eax,ecx
                    uint32_t and_eax_ecx_machine_code = generate_and_reg(9,9,10,0);

                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (and_eax_ecx_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }

                
                
                    // mov     [ebx+dddd],eax
                    uint32_t dddd_eax_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_eax_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t dddd_eax_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_eax_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t str_dddd_eax_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (str_dddd_eax_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                //  OUTRF_end:


                    break;
                }
                case 0x19:{
            
  

                    sprintf(buffer, "%s ", "OUTRNOT"); // 格式化字符串
                    OutputDebugStringA(buffer); // 輸出到 DebugView
                
                    Bit = (char)(cont_ptr->arg[5]); 
                
                    sprintf(buffer,   "  type = Bit %d ", Bit);
                    OutputDebugStringA(buffer); // 輸出到 DebugView
                
                    // mov     ecx,[ebx+dddd]
                
                    Reg = (long*)(cont_ptr->arg);
                    /* Adjust for user register area. This would modified the source in the ram.*/
                    if(USR_REG_AREA_START <= *Reg)
                    {
                        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                        isAccessingUsrRegArea=1;
                    }
                
                
                       // cmp     al,0
                       uint32_t machine_cmp = generate_cmp(5, 0);
                       for (size_t i = 0; i < sizeof(uint32_t); i++) {
                           *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                           pc_counter++;                            // 指向下一個位元組
                       }
                   
                
                       // je      OUTRNOT_OFF
                       uint32_t machine_beq = encode_beq(24*4);
                       for (size_t i = 0; i < sizeof(uint32_t); i++) {
                           *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                           pc_counter++;                            // 指向下一個位元組
                       }
                   
                   
                
                
                
                   // mov     eax,[ebx+dddd]
                    /* get source */ //eax
                    uint32_t eax_index_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // mov     cl,0xFF
                        Bit = (char)(cont_ptr->arg[5]);
                        // mov     cl,0xff
                
                        uint32_t cl_low_machine_code = mov_imm(Bit,  4);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t cl_high_machine_code = generate_movt_reg_imm(Bit, 4);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                        // shr     eax,cl = lsr r9,r9,r4
                        uint32_t lsr_machine_code = generate_lsr_reg_reg(9,9,4,0);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (lsr_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                        // and     eax,1
                        uint32_t and_eax_1_machine_code = generate_and_imm(9, 9, 0x1);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (and_eax_1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                        // cmp     eax,00
                        uint32_t machine_cmp_eax_0 = generate_cmp(9, 0);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (machine_cmp_eax_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                        }
                
                    // je     OUTRNOT_0  // beq = 0x0
                    uint32_t branch_OUTRF_0_machine_code = generate_branch(25 * 4, 0x0) ;
                    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (branch_OUTRF_0_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }  
                
                
                        // mov     eax,1
                
                        uint32_t eax_1_low_machine_code = mov_imm(1,  9);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (eax_1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t eax_1_high_machine_code = generate_movt_reg_imm(0, 9);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (eax_1_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                
                
                        // mov     cl,0xFF
                        Bit = (char)(cont_ptr->arg[5]);
                        // mov     cl,0xff
                
                        uint32_t cl_bit_low_machine_code = mov_imm(Bit,  4);
                        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = ( cl_bit_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                    
                    
                    
                        uint32_t cl_bit_high_machine_code = generate_movt_reg_imm(Bit, 4);
                        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (cl_bit_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }
                
                    // shl     eax,cl = lsl r9,r9,r4
                    uint32_t lsl_eax_cl_machine_code_2 = generate_lsl_reg_reg(9,9,4,0);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (lsl_eax_cl_machine_code_2 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    
                
                    // not     eax
                    uint32_t not_eax_machine_code = generate_mvn_reg(9, 9);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (not_eax_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                
                    // mov     ecx,[ebx+dddd]
                    uint32_t ecx_dddd_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ecx_dddd_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t ecx_dddd_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ecx_dddd_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_ecx_dddd_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_ecx_dddd_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // and     eax,ecx
                    uint32_t and_eax_ecx_machine_code = generate_and_reg(9,9,10,0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (and_eax_ecx_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    // mov     [ebx+dddd],eax
                    uint32_t dddd_eax_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_eax_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t dddd_eax_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_eax_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t str_dddd_eax_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (str_dddd_eax_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                    
                
                    // jmp     OUTRF_end
                    uint32_t jmp_end = generate_branch(11*4, 0xe); // Always (0xE)
                    //jmp  addW2   
                        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                                *pc_counter = (jmp_end  >> (i * 8)) & 0xFF; // 提取每個位元組
                                pc_counter++;                            // 指向下一個位元組
                        }	
                
                
                
                // OUTRNOT_OFF :
                
                
                
                    // mov     eax,1
                    uint32_t eax_1_low_machine_code_1 = mov_imm(1,  9);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_1_low_machine_code_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t eax_1_high_machine_code_1 = generate_movt_reg_imm(0, 9);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (eax_1_high_machine_code_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    // mov     cl,0xff
                
                    uint32_t cl_0xff_low_machine_code = mov_imm(Bit,  4);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (cl_0xff_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t cl_0xff_high_machine_code = generate_movt_reg_imm(Bit, 4);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (cl_0xff_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // shl     eax,cl = lsl r9,r9,r4
                    uint32_t lsl_eax_cl_machine_code = generate_lsl_reg_reg(9,9,4,0);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (lsl_eax_cl_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                
                    // mov     ecx,[ebx+dddd]
                
                
                
                    /* get source */ //ecx
                    uint32_t dddd_index_low_machine_code = mov_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data),  7);
                    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    uint32_t dddd_index_high_machine_code = generate_movt_reg_imm((*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data), 7);
                    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (dddd_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                
                    uint32_t ldr_dddd_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (ldr_dddd_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                
                    // or      eax,ecx
                    uint32_t or_eax_ecx_machine_code =  generate_or_reg(9,9,10,0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = ( or_eax_ecx_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                    }
                
                
                
                    // [ebx+dddd],eax
                    uint32_t str_eax_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
                
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (str_eax_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }
                


                    
                    break;
                }
            }
        }
    }
    else
    {
        switch ( cont_ptr->data.code )
        {
            case 0x01:                /* -| |- */
            case 0x02:
            case 0x09:                /* -|T|- */
            case 0x0B:                /* -|C|- */
                code_ptr=(char *)ANDF;
#ifdef VerText
                fprintf(file_txt,"ANDF    ");
#endif
                fprintf(plc_run_cpp,"    ANDF(Data+");
                break;
            case 0x16:                /* -|R|- */
                code_ptr=(char *)ANDRF;
#ifdef VerText
                fprintf(file_txt,"ANDRF   ");
#endif
                fprintf(plc_run_cpp,"    ANDRF(*(long*)(Data+");
                break;
            case 0x03:                /* -|/|- */
            case 0x04:
            case 0x0A:                /* -|T/|- */
            case 0x0C:                /* -|C/|- */
                code_ptr=(char *)ANDNOT;
#ifdef VerText
                fprintf(file_txt,"ANDNOT  ");
#endif
                fprintf(plc_run_cpp,"    ANDNOT(Data+");
                break;
            case 0x17:                /* -|R/|- */
                code_ptr=(char *)ANDRNOT;
#ifdef VerText
                fprintf(file_txt,"ANDRNOT ");
#endif
                fprintf(plc_run_cpp,"    ANDRNOT(*(long*)(Data+");
                break;
        }
#ifdef VerText
//    fprintf(file_txt,"I %d\n", cont_ptr->arg1);
        Reg = (long*)(cont_ptr->arg);
        PrintAddrIO(*Reg);
#endif

        a=*code_ptr++;
        b=*code_ptr++;
        if ((cont_ptr->data.code>=0x16)&&(cont_ptr->data.code<=0x17))
        	c=*code_ptr++;
        	

        while ( a>0 ){
            *pc_counter++=*code_ptr++;
            a--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;

        if (cont_ptr->data.code==0x16||cont_ptr->data.code==0x17)/* -|R|-  & -|R/|- */
        {
            Reg = (long*)(cont_ptr->arg);
            /* Adjust for user register area. This would modified the source in the ram.*/
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr++=(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data);
            fprintf(plc_run_cpp,"%d),",(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data));
        }
        else if ((cont_ptr->data.code>=0x01)&&(cont_ptr->data.code<=0x08))  /* -| |-  & -|/|- */
        {
            Reg = (long*)(cont_ptr->arg);
            /* 2014/08/17 iLevel is not used anymore. */
            #if 0
            //add by ted for Level 2,3 read I'(I2048~I4095) ps. copy from I0~I2047
	        if (iLevel>0)
	            *temp_ptr++=(*Reg + 0x800);                       /* -| |-  & -|/|- */
	        else
	        //
            *temp_ptr++=*Reg;                                     /* -| |-  & -|/|- */
            #endif
            *temp_ptr++=*Reg;
            fprintf(plc_run_cpp,"%d);\n",*Reg);
        }
        else
        {
            if ( cont_ptr->data.code==9||cont_ptr->data.code==0x0A )
            {
                Reg = (long*)(cont_ptr->arg);
                *temp_ptr++=(*Reg*2)+s_a0_tmr_s_ofs+1; /* -|T|-  & -|T/| */
                                                     /* timer status bit buffer */
                fprintf(plc_run_cpp,"%d);\n",(*Reg*2)+s_a0_tmr_s_ofs+1);
            }
            else
            {
                if ( cont_ptr->data.code==0x0B || cont_ptr->data.code==0x0C )
                {
                    Reg = (long*)(cont_ptr->arg);
                    *temp_ptr++=(*Reg*2)+s_a0_cnt_s_ofs+1; /* -|C|-  & -|C/| */
                                                   /* counter status bit buffer */
                    fprintf(plc_run_cpp,"%d);\n",(*Reg*2)+s_a0_cnt_s_ofs+1);
                }
            }
        }
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while ( b>0 ){
            *pc_counter++=*code_ptr++;
             b--;
        }
        if ((cont_ptr->data.code==0x16)||(cont_ptr->data.code==0x17))
        {
        	Bit = (char)(cont_ptr->arg[5]);
            *pc_counter++=Bit;
            fprintf(plc_run_cpp,"%d);\n",Bit);
            code_ptr++;
        	while ( c>0 ){
                *pc_counter++=*code_ptr++;
                c--;
            }
        }
    }
    return 1;
}

/*********************************************************************/
int get_pld( struct contact *cont_ptr )   /* get  PLD machine code */
{

    // 2025 
    char buffer[256]; // 分配足夠的debug緩存

   char a,b,c,*code_ptr;
   NUM_TYPE *temp_ptr;
   int s_a0_cnt_s_ofs;
   int s_a0_tmr_s_ofs;
   long *Reg;
   char Bit;
   long int a1,a2,a3;

   a1=(long)s_mlc_cnt_stus;
   a2=(long)s_mlc_tmr_stus;
   a3=(long)MLC_Data;

   s_a0_cnt_s_ofs=(int)((long)a1-(long)a3);
   s_a0_tmr_s_ofs=(int)((long)a2-(long)a3);

    sprintf(buffer, "%s 0x%lx", "20250119 func=PLD cont_ptr->data.code=", cont_ptr->data.code); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

   if ((cont_ptr->data.code>=1)&&(cont_ptr->data.code<=8))
   {
      Reg = (long*)cont_ptr->arg;

        uint32_t push_r9_machine_code = 0xE52D9004 ;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (push_r9_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }      

      uint32_t dddd_index_low_machine_code = mov_imm(*Reg,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dddd_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }      
      uint32_t dddd_index_high_machine_code = generate_movt_reg_imm(*Reg, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dddd_index_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }     
      uint32_t ldrb_dddd_index_machine_code = generate_ldrb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldrb_dddd_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        sprintf(buffer, "%s 0x%lx", "20250119 func=PLD *Reg=", *Reg); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


   }
   else if (cont_ptr->data.code==9||cont_ptr->data.code==0x0A)
   {
      Reg = (long*)cont_ptr->arg;
      //*temp_ptr++=(*Reg*4)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
      *temp_ptr++=(*Reg*2)+s_a0_tmr_s_ofs+1;  /* -|T|-  & -|T/| */
                                               /* timer status bit buffer */
      fprintf(plc_run_cpp,"%d);\n",(*Reg*2)+s_a0_tmr_s_ofs+1);
   }
   else if (cont_ptr->data.code==0x0B||cont_ptr->data.code==0x0C)
   {
      Reg = (long*)cont_ptr->arg;
      //*temp_ptr++=4*(*Reg)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
      *temp_ptr++=2*(*Reg)+s_a0_cnt_s_ofs+1;  /* -|C|-  & -|C/| */
                                             /* counter status bit buffer */
      fprintf(plc_run_cpp,"%d);\n",2*(*Reg)+s_a0_cnt_s_ofs+1);
   }
   else if (cont_ptr->data.code==0x16||cont_ptr->data.code==0x17)
   {
   	  Reg = (long*)cont_ptr->arg;
      /* Adjust for user register area. This would modified the source in the ram.*/
      if(USR_REG_AREA_START <= *Reg)
      {
          *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
          isAccessingUsrRegArea=1;
      }
      *temp_ptr++=(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data); /* -|R|-  & -|R/|- */
      fprintf(plc_run_cpp,"%d),",(*Reg*4)+(long)((long)s_mlc_reg-(long)MLC_Data));
   }
//    pc_counter+=NUM_SIZE;
//    code_ptr+=NUM_SIZE;

//    while (b>0)
//    {
//       *pc_counter++=*code_ptr++;
//       b--;
//    }
   
   if (cont_ptr->data.code==0x16||cont_ptr->data.code==0x17)
   {
      Bit = (char)(cont_ptr->arg[5]);
      *pc_counter++=Bit;
      fprintf(plc_run_cpp,"%d);\n",Bit);
      code_ptr++;
      while ( c>0 )
      {
          *pc_counter++=*code_ptr++;
          c--;
      }	
   }
   
   return 1;
}

/*==========================================================
 *  Function Name : get_jump()
 *  Description   : get JMP and JSR element routine
 *  Input         :
 *     code_ptr   :
 *     funct_ptr  :
 *     mach_start_ptr :
 *
 *  Return        : none
 *  Calling       : match()
 *==========================================================*/
void get_jump( char *code_ptr, struct funct *funct_ptr, char *mach_start_ptr ) /* get machine code */
// mach_start_ptr : L88 machine code start address
{
    struct symbol *symbol_ptr;
    NUM_TYPE *temp_ptr;
    char a1,a2,a3,a4;
    int i,match_flag=0;
    int line,b,c;

    b=(int)funct_ptr;
    c=(int)pc_edit_adr;
    line = ((b-c)/linebytes)+1;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */
    a3=*code_ptr++;  /* machine word number */
    a4=*code_ptr++;  /* machine word number */

/*================ get machine code ==================*/
    if(a1>0)
    {
        while(a1>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a1--;
        }
    }
/*================ add stack pointer =================*/
    if(a2>0)
    {
        temp_ptr = (NUM_TYPE *)pc_counter;
        *temp_ptr = temp_reg_stack;
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

/*================ get machine code ==================*/
        while(a2>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a2--;
        }
    }
/*================ label addressing ==================*/
    if(a3>0)
    {
        symbol_ptr=(struct symbol *)funct_ptr;
        jmp_index++;

        for(i=1;i<=label_total && match_flag==0;i++)
            match_flag=match(symbol_ptr->symb,label_table[i]);
        i--;
       /*==== if LAB name in label_table,     ====*/
       /*==== save the order in label_index   ====*/

        if(match_flag==1) /* LAB name in label_table */
        {
            fprintf(plc_run_cpp,"    if(al) goto Label_%d;//%s;\n",i,label_table[i]);
            //fprintf(plc_run_cpp,"    if(al) goto %s;\n",label_table[i]);
            label_index[jmp_index]=i;
            error_line[i]=line;
        }

        /*==== if LAB name NOT in label_table, ====*/
        /*==== add new LAB name in label_table ====*/

        else
        {
            int srcLen=strlen(symbol_ptr->symb);
            label_total++;
            for (i=0;i<14;i++)
            {
                /* Protection for unreasonable char. */
                char setChar=0;
                if((32 > symbol_ptr->symb[i]) || (126 < symbol_ptr->symb[i]))
                {
                    setChar=symbol_ptr->symb[i];
                    if(13 == i)
                    {
                        setChar=symbol_ptr->symb[i] & 0x7F;
                        /* A special error case. */
                        if(14 > srcLen)
                        {
                            setChar=0;
                        }
                    }
                    if((32 > setChar) || (126 < setChar))
                    {
                        setChar=0;
                    }
                }
                else
                {
                    setChar=symbol_ptr->symb[i];
                }

                //printf("[cdcode]@get_jump Before set label_table label_table[%d][%d]=%c(0x%02X) symb[%d]=%c(0x%02x)\n",label_total,i,label_table[label_total][i],label_table[label_total][i],i,symbol_ptr->symb[i],symbol_ptr->symb[i]);
                label_table[label_total][i]=setChar;
                //printf("[cdcode]@get_jump After set label_table label_table[%d][%d]=%c(0x%02X) symb[%d]=%c(0x%02x)\n",label_total,i,label_table[label_total][i],label_table[label_total][i],i,symbol_ptr->symb[i],symbol_ptr->symb[i]);
            }
            fprintf(plc_run_cpp,"    if(al) goto Label_%d;//%s;\n",label_total,label_table[label_total]);
            //fprintf(plc_run_cpp,"    if(al) goto %s;\n",label_table[label_total]);
            label_index[jmp_index]=label_total;
            error_line[label_total]=line;
        }

        jmp_address[jmp_index]=pc_counter-mach_start_ptr;
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

/*================ get machine code ==================*/
        while(a3>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a3--;
        }
    }
/*================ minus stack pointer ===============*/
    if(a4>0)
    {
        temp_ptr = (NUM_TYPE *)pc_counter;
        *temp_ptr = temp_reg_stack;
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

/*================ get machine code ==================*/
        while(a4>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a4--;
        }
    }
}

/*==========================================================
 *  Function Name : get_label()
 *  Description   : get LAB element routine
 *  Input         :
 *     code_ptr   :
 *     funct_ptr  :
 *     mach_start_ptr :
 *
 *  Return        : none
 *  Calling       : match()
 *==========================================================*/
void get_label( char *code_ptr, struct funct *funct_ptr, char *mach_start_ptr ) /* get machine code */
/*   mach_start_ptr : L88 machine code start address */
{
    struct symbol *symbol_ptr;
    int i,match_flag;
    //int j; //for debug
    symbol_ptr=(struct symbol *)funct_ptr;

    /*==== if LAB name not in label_table, ====*/
    /*==== add new LAB name in label_table ====*/

    match_flag=0;
    for(i=1;i<=label_total && match_flag==0;i++)
        match_flag=match(symbol_ptr->symb,label_table[i]);
                       /* if FOUND, match_flag=1 */

    if(match_flag==0) /* LAB name not in label_table */
    {
        int srcLen=strlen(symbol_ptr->symb);
        label_total++;
        for (i=0;i<14;i++)
        {
            /* Protection for unreasonable char. */
            char setChar=0;
            if((32 > symbol_ptr->symb[i]) || (126 < symbol_ptr->symb[i]))
            {
                setChar=symbol_ptr->symb[i];
                if(13 == i)
                {
                    setChar=symbol_ptr->symb[i] & 0x7F;
                    /* A special error case. */
                    if(14 > srcLen)
                    {
                        setChar=0;
                    }
                }
                if((32 > setChar) || (126 < setChar))
                {
                    setChar=0;
                }
            }
            else
            {
                setChar=symbol_ptr->symb[i];
            }
            //printf("[cdcode]@get_label Before set label_table label_table[%d][%d]=%c(0x%02X) symb[%d]=%c(0x%02x)\n",label_total,i,label_table[label_total][i],label_table[label_total][i],i,symbol_ptr->symb[i],symbol_ptr->symb[i]);
            label_table[label_total][i]=setChar;
            //printf("[cdcode]@get_label After set label_table label_table[%d][%d]=%c(0x%02X) symb[%d]=%c(0x%02x)\n",label_total,i,label_table[label_total][i],label_table[label_total][i],i,symbol_ptr->symb[i],symbol_ptr->symb[i]);
        }
    }

    /*==== find the index for current LAB, then save address ====*/

    match_flag=0;
    for(i=1;i<=label_total && match_flag==0;i++)
        match_flag=match(symbol_ptr->symb,label_table[i]);
                       /* if FOUND, match_flag=1 */
    i--;
#if 0
    for(j=0;j<label_total;j++)
    {
        printf("[code_cpp]label %j=%s\n",label_table[j]);
    }
    printf("[code.cpp]@get_label i=%d symb=%s\n pc_counter=0x%04x mach_start_ptr=0x%04x\n",i,symbol_ptr->symb,pc_counter,mach_start_ptr);
#endif
    if(match_flag==1) /* LAB name in label_table */
        label_address[i]=pc_counter-mach_start_ptr;
    if(match_flag==1)
    	  fprintf(plc_run_cpp,"Label_%d: //%s:\n",i,label_table[i]);
          //fprintf(plc_run_cpp,"%s:\n",label_table[i]);
}

/* create mlc parameter table */
struct regpar *par_tab( struct regpar *tar_addr, unsigned char code, long no, long arg1, long arg2, long offset, char C_label[14] )
{
    int i;
    switch(code){
        case PC_EOF : tar_addr->code=PC_EOF;
            return(tar_addr);
        case 'S':
            tar_addr->code='S';  /* load C Subroutine ID */
            tar_addr->MLC_offset=offset;  // C_code_offset, type : long
            for (i=0;i<14;i++)
                tar_addr->label[i]=C_label[i];
            break;
        case 'T':
            tar_addr->code='T';  /* load timer register ID */
            break;
        case 'C':
            tar_addr->code='C';  /* load counter register ID */
            tar_addr->arg2=arg2;   /* load register paramter */
            break;
        case 'R':
            tar_addr->code='R';  /* load genernal register ID */
            break;
        case 'N':
            tar_addr->code='N';  /* load genernal register ID */
            tar_addr->arg2=arg2;   // mlc_code_offset, type : int
                                   // should change to long type later
            break;
    }
    tar_addr->no=no;       /* load register number */
    tar_addr->arg1=arg1;   /* load register paramter */
    tar_addr++;
    tar_addr->code=PC_EOF;  /* end of file */
    return(tar_addr);
}


/*****************************************************************/
int get_fun_code( struct funct *funct_ptr, char *mach_start_ptr )
{
    char *code_ptr;    //,a;
    int mlc_code_offset;
    int C_code_offset;

    char buffer[256]; // 分配足夠的debug緩存
    sprintf(buffer, "get_code"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

/* struct symbol *sym_ptr;    */
//   char name[8];
//   char word[8];
   /* sruct node *tree(),*ext_glb_tree();  */

    if ((funct_ptr->data.code>=0x20)&&(funct_ptr->data.code<=0x05D) ||
        (funct_ptr->data.code>=0x80)&&(funct_ptr->data.code<=0x0AB)
       )
    {
        switch(funct_ptr->data.code)
        {
                sprintf(buffer, "fun1"); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            case 0x21:
                code_ptr=(char *)ADDW;
                fprintf(plc_run_cpp,"    ADDW(");
                sprintf(buffer, "ADDW"); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x33:
                code_ptr=(char *)SUBW;
                fprintf(plc_run_cpp,"    SUBW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x45:
                code_ptr=(char *)MULW;
                fprintf(plc_run_cpp,"    MULW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x56:
                code_ptr=(char *)MODW;
                fprintf(plc_run_cpp,"    MODW(");
                get_arith_r(code_ptr,funct_ptr);
                break;

            case 0x57:
                code_ptr=(char *)DIVW;
                fprintf(plc_run_cpp,"    DIVW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x81:
                code_ptr=(char *)ANDW;
                fprintf(plc_run_cpp,"    ANDW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x93:
                code_ptr=(char *)ORW;
                fprintf(plc_run_cpp,"    ORW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
            case 0x0A5:
                code_ptr=(char *)XORW;
                fprintf(plc_run_cpp,"    XORW(");
                get_arith_r(code_ptr,funct_ptr);
                break;
    /**** 82.07.22 ****/

            case 0x27:
                code_ptr=(char *)ADDC;
                // fprintf(plc_run_cpp,"    ADDC(");
                sprintf(buffer, "ADDC"); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
                get_arith_i(code_ptr,funct_ptr ); 

                break;
            case 0x39:
                code_ptr=(char *)SUBC;
                fprintf(plc_run_cpp,"    SUBC(");
                get_arith_i(code_ptr,funct_ptr );
                break;
            case 0x4b:
                code_ptr=(char *)MULC;
                fprintf(plc_run_cpp,"    MULC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
            case 0x5c:
                code_ptr=(char *)MODC;
                fprintf(plc_run_cpp,"    MODC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
            case 0x5d:
                code_ptr=(char *)DIVC;
                fprintf(plc_run_cpp,"    DIVC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
            case 0x87:
                code_ptr=(char *)ANDC;
                fprintf(plc_run_cpp,"    ANDC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
            case 0x99:
                code_ptr=(char *)ORC;
                fprintf(plc_run_cpp,"    ORC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
            case 0x0Ab:
                code_ptr=(char *)XORC;
                fprintf(plc_run_cpp,"    XORC(");
                get_arith_i(code_ptr,funct_ptr);
                break;
        // TOOL
            //case 0x0A0:  // Read Back Register to Register
            //    code_ptr=XMVR;
            //    get_xmv_r(code_ptr,funct_ptr);
            //    break;
            case 0x0A2:  // Tool Search
                code_ptr=(char *)SCH;
                fprintf(plc_run_cpp,"    SCH(");
                get_sch(code_ptr,funct_ptr);
                break;
            case 0x0A3:  // Tool Rotation
                code_ptr=(char *)ROT;
                fprintf(plc_run_cpp,"    ROT(");
                get_rot(code_ptr,funct_ptr);
                break;
            case 0x09A:  // Shllw
                code_ptr=(char *)SHLLW;
                fprintf(plc_run_cpp,"    SHLLW(");
                get_shl_r(code_ptr,funct_ptr);
                break;
            case 0x09B:  // Shllc
                code_ptr=(char *)SHLLC;
                fprintf(plc_run_cpp,"    SHLLC(");
                get_shl_i(code_ptr,funct_ptr);
                break;
            case 0x09C:  // Shrlw
                code_ptr=(char *)SHRLW;
                fprintf(plc_run_cpp,"    SHRLW(");
                get_shl_r(code_ptr,funct_ptr);
                break;
            case 0x09D:  // Shrlc
                code_ptr=(char *)SHRLC;
                fprintf(plc_run_cpp,"    SHRLC(");
                get_shl_i(code_ptr,funct_ptr);
                break;
        }
    }
    else if((funct_ptr->data.code>=0x5E)&&(funct_ptr->data.code<0x80))
    {
        switch(funct_ptr->data.code)
        {

                sprintf(buffer, "fun2"); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            case 0x5E:
                code_ptr=(char *)CMPWhi;
                fprintf(plc_run_cpp,"    CMPWhi(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x5F:
                code_ptr=(char *)CMPWhieq;
                fprintf(plc_run_cpp,"    CMPWhieq(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x62:
                code_ptr=(char *)CMPWls;
                fprintf(plc_run_cpp,"    CMPWls(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x63:
                code_ptr=(char *)CMPWlseq;
                fprintf(plc_run_cpp,"    CMPWlseq(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x66:
                code_ptr=(char *)CMPWeq;
                fprintf(plc_run_cpp,"    CMPWeq(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x67:
                code_ptr=(char *)CMPWneq;
                fprintf(plc_run_cpp,"    CMPWneq(");
                get_cmp_r(code_ptr,funct_ptr);
                break;
            case 0x6A:
                code_ptr=(char *)CMPChi;
                fprintf(plc_run_cpp,"    CMPChi(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x6B:
                code_ptr=(char *)CMPChieq;
                fprintf(plc_run_cpp,"    CMPChieq(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x6C:
                code_ptr=(char *)CMPCls;
                fprintf(plc_run_cpp,"    CMPCls(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x6D:
                code_ptr=(char *)CMPClseq;
                fprintf(plc_run_cpp,"    CMPClseq(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x6E:
                code_ptr=(char *)CMPCeq;
                fprintf(plc_run_cpp,"    CMPCeq(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x6F:
                code_ptr=(char *)CMPCneq;
                fprintf(plc_run_cpp,"    CMPCneq(");
                get_cmp_i(code_ptr,funct_ptr);
                break;
            case 0x70:
                code_ptr=(char *)MOVC;
                fprintf(plc_run_cpp,"    MOVC(");
                get_mov_i(code_ptr,funct_ptr);
                break;
            case 0x71:
                code_ptr=(char *)MULRINI;
                fprintf(plc_run_cpp,"    MULRINI(");
                get_mulrini(code_ptr,funct_ptr);
                break;
            case 0x72:
                code_ptr=(char *)MOVW;
                fprintf(plc_run_cpp,"    MOVW(");
                get_mov_r(code_ptr,funct_ptr);
                break;
            case 0x73:
                code_ptr=(char *)MULRCPY;
                fprintf(plc_run_cpp,"    MULRCPY(");
                get_mulrcpy(code_ptr,funct_ptr);
                break;
            case 0x74:  // Move immediate to Register Point
                code_ptr=(char *)XMOVC;
                fprintf(plc_run_cpp,"    XMOVC(");
                get_xmov_i(code_ptr,funct_ptr);
                break;
            case 0x76:  // Move Register to Register Point
                code_ptr=(char *)XMOVW;
                fprintf(plc_run_cpp,"    XMOVW(");
                get_xmov_r(code_ptr,funct_ptr);
                break;
            case 0x77:
                code_ptr=(char *)IORMAP;
                fprintf(plc_run_cpp,"    IRMAP(");
                get_irmap(code_ptr,funct_ptr);
                break;
            case 0x78:
                code_ptr=(char *)IORMAP;
                fprintf(plc_run_cpp,"    ORMAP(");
                get_ormap(code_ptr,funct_ptr);
                break;
            case 0x79:
                code_ptr=(char *)MOVRTRP;
                fprintf(plc_run_cpp,"    MOVRTRP(");
                get_movrp(code_ptr,funct_ptr);
                break;
            case 0x7A:
                code_ptr=(char *)MOVRPTR;
                fprintf(plc_run_cpp,"    MOVRPTR(");
                get_movrp(code_ptr,funct_ptr);
                break;
            case 0x7B:
                code_ptr=(char *)IORMAPN;
                fprintf(plc_run_cpp,"    IRMAPN(");
                get_irmapN(code_ptr,funct_ptr);
                break;
            case 0x7C:
                code_ptr=(char *)IORMAPN;
                fprintf(plc_run_cpp,"    ORMAPN(");
                get_ormapN(code_ptr,funct_ptr);
                break;
        }

    }
    else if((funct_ptr->data.code>=0x0CD)&&(funct_ptr->data.code<=0x0D3))
    {
        switch(funct_ptr->data.code)
        {
            case 0x0CD:
                code_ptr=(char *)RET;
                break;
            case 0x0CF:
                code_ptr=(char *)END;
                end_flag++;  /*  fast pc and normal pc END :code */
/*       mlc_normal_addr=pc_counter+1;  */   /* END code : 1 words */
                break;
            case 0x0D0:
                code_ptr=(char *)RTS;
                break;
        }
        get_code_0(code_ptr);

        if(funct_ptr->data.code == 0x0CF)
        {
            mlc_code_offset = pc_counter - mach_start_ptr;
//          parpt=(struct regpar *)par_tab(parpt,'N',0,end_flag,0,mlc_code_offset,0);
            parpt=(struct regpar *)par_tab(parpt,'N',0,end_flag,mlc_code_offset,0,0);
            //add by ted for Level 2,3 read I'(I2048~I4095) ps. copy from I0~I2047
			iLevel++;
			//
        }
    }
/*******************************************************************/

    /* get timer and counter machine code */
    else if(((funct_ptr->data.code>=0x0C1)&&(funct_ptr->data.code<=0x0C9))||
            ((funct_ptr->data.code >=0x0E0)&&(funct_ptr->data.code<=0x0EB)) ||
            ((funct_ptr->data.code >=0x0B0)&&(funct_ptr->data.code<=0x0B7))
           )
    {
        switch(funct_ptr->data.code)
        {
            case 0x0C1:    /* use immediate data */
            case 0x0C2:    /* use register data */
                code_ptr=(char *)TIMER0;
                get_timer0(code_ptr,funct_ptr);
                break;
            case 0x0C3:    /* use immediate data */
            case 0x0E2:    /* use register data */
                code_ptr=(char *)TIMER1;
                get_timer1(code_ptr,funct_ptr);
                break;
            case 0x0C4:    /* use immediate data */
            case 0x0E3:    /* use register data */
                code_ptr=(char *)TIMER2;
                get_timer2(code_ptr,funct_ptr);
                break;
            case 0x0C5:    /* use immediate data */
            case 0x0E4:    /* use register data */
                code_ptr=(char *)TIMER3;
                get_timer3(code_ptr,funct_ptr);
                break;
            case 0x0C6:    /* use immediate data */
            case 0x0E5:    /* use register data */
                code_ptr=(char *)UCNT;
                get_ucnt(code_ptr,funct_ptr);
                counter_no++;
                break;
            case 0x0C7:    /* use immediate data */
            case 0x0E6:    /* use register data */
                code_ptr=(char *)DCNT;
                get_dcnt(code_ptr,funct_ptr);
                counter_no++;
                break;
            case 0x0C9:
                code_ptr=(char *)RESET;
                get_reset(code_ptr,funct_ptr);
                break;
            case 0x0E0:    /* use immediate data */
            case 0x0E9:    /* use register data */
                code_ptr=(char *)R_UCNT;
                get_r_ucnt(code_ptr,funct_ptr);
                counter_no++;
                break;
            case 0x0E1:    /* use immediate data */
            case 0x0EA:    /* use register data */
                code_ptr=(char *)R_DCNT;
                get_r_dcnt(code_ptr,funct_ptr);
                counter_no++;
                break;
            // 850226 get counter value
            case 0x0EB:
                code_ptr=(char *)CNTVAL;
                get_cntval(code_ptr,funct_ptr);
                break;
            //RTimers
            case 0x0B0:
                code_ptr=(char *)RTIMER1MS_REG;
                Get_RTimer1MS_Reg(code_ptr,funct_ptr);

                break;
            case 0x0B1:
                code_ptr=(char *)RTIMER1MS_IMM;
                Get_RTimer1MS_Imm(code_ptr,funct_ptr);
                break;
            case 0x0B2:
                code_ptr=(char *)RTIMER10MS_REG;
                Get_RTimer10MS_Reg(code_ptr,funct_ptr);
                break;
            case 0x0B3:
                code_ptr=(char *)RTIMER10MS_IMM;
                Get_RTimer10MS_Imm(code_ptr,funct_ptr);
                break;
            case 0x0B4:
                code_ptr=(char *)RTIMER100MS_REG;
                Get_RTimer100MS_Reg(code_ptr,funct_ptr);
                break;
            case 0x0B5:
                code_ptr=(char *)RTIMER100MS_IMM;
                Get_RTimer100MS_Imm(code_ptr,funct_ptr);
                break;
            case 0x0B6:
                code_ptr=(char *)RTIMER1S_REG;
                Get_RTimer1S_Reg(code_ptr,funct_ptr);
                break;
            case 0x0B7:
                code_ptr=(char *)RTIMER1S_IMM;
                Get_RTimer1S_Imm(code_ptr,funct_ptr);
                break;

        }
    }
    else if((funct_ptr->data.code>=0x0CA)&&(funct_ptr->data.code<=0x0CC))
    {
        switch(funct_ptr->data.code)
        {
            case 0x0CA:
                code_ptr=(char *)JMP;
                get_jump(code_ptr,funct_ptr,mach_start_ptr);
                break;
            case 0x0CB:
                code_ptr=(char *)JSR;
                get_jump(code_ptr,funct_ptr,mach_start_ptr);
                break;
            case 0x0CC:
                code_ptr=(char *)CSR;
                C_code_offset=get_call(code_ptr,funct_ptr,mach_start_ptr);
                parpt=(struct regpar *)par_tab(parpt,'S',0,0,0,C_code_offset,C_label);
                break;
        }
    }

    else if((funct_ptr->data.code>=0x0D4)&&(funct_ptr->data.code<=0x0D8))
    {
        switch(funct_ptr->data.code)
        {
            case 0x0D4:  // 851019  MSG element   by C.C. Liu
                code_ptr=(char *)MSGCode;
                Set_Msg(code_ptr,funct_ptr);
                break;
            case 0x0D5:
                code_ptr=new char[14];
                get_label(code_ptr,funct_ptr,mach_start_ptr);
                break;
        }
    }
    else
    {
        /* do nothing. */
    }
    return 1;
}
/*==========================================================
 *  Function Name : get_call()
 *  Description   : get CSR element routine
 *  Input         :
 *     code_ptr   :
 *     funct_ptr  :
 *     mach_start_ptr :
 *
 *  Return        : none
 *  Calling       : none
 *==========================================================*/
int get_call( char *code_ptr, struct funct *funct_ptr, char *mach_start_ptr ) /* get machine code */
// *mach_start_ptr : L88 machine code start address
{
    struct symbol *symbol_ptr;
    char a1,a2;
    int i;
    int C_code_offset;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */

/*================ get machine code ==================*/
    if(a1>0)
    {
        while(a1>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a1--;
        }
    }
/*================ label addressing ==================*/
    if(a2>0)
    {
        symbol_ptr=(struct symbol *)funct_ptr;
        for (i=0;i<14;i++)
            C_label[i]=symbol_ptr->symb[i];
        C_label[13]&=0x7F;
        C_code_offset = BaseOFFSET(pc_counter, mach_start_ptr);
        pc_counter+=4;
        code_ptr+=4;

/*================ get machine code ==================*/
        while(a2>0)
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a2--;
        }
    }
    return( C_code_offset );
}


/*==========================================================*/
int match( char *symb, char *table )
{
    int i;
    int srcLen=strlen(symb);
    //for ( i=0; i<14 && *symb++==*table++; i++ );
    for ( i=0; i<14; i++ )
    {
        /* Protection for unreasonable char. */
        char compChar=0;
        if((32 > symb[i]) || (126 < symb[i]))
        {
            compChar=symb[i];
            if(13 == i)
            {
                compChar=symb[i] & 0x7F;
                /* A special error case. */
                if(14 > srcLen)
                {
                    compChar=0;
                }
            }
            if((32 > compChar) || (126 < compChar))
            {
                compChar=0;
            }
        }
        else
        {
            compChar=symb[i];
        }

        if(compChar != *(table+i))
        {
            //printf("[cdcode]@match Not Matched. i=%d symb+i=%c(%02x) table+i=%c(%02x)\n",i,*(symb+i),*(symb+i),*(table+i),*(table+i));
            break;
        }
        //printf("[cdcode]@match i=%d symb+i=%c(%02x) table+i=%c(%02x)\n",i,*(symb+i),*(symb+i),*(table+i),*(table+i));
    }
    if ( i!=14 ) 
        return(0); /* string not match */
    return(1);
}
/*******************************************************************/

void get_code_0( char *code_ptr ) /* get machine code without argument */
{
    char a;
    a=*code_ptr++;  /* machine word number */
    while( a>0 ){
        *pc_counter++=*code_ptr++;  /* get machine code */
        a--;
    }




}
/*******************************************************************/
/*
        Initialize multi registers with one intial value
*/
static void get_mulrini(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3,code_seg4;
        /* register area address */
        unsigned int localRegAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int *regIndex;
        unsigned int *iniNum;
        unsigned int *iniVal;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        code_seg4=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is start register for initialization */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"%d,",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        temp_ptr = (NUM_TYPE *)pc_counter;
        /* Be careful about this part is different from most of other function blocks */
        /* The second argument is number of registers to initialize.*/
        iniNum = (unsigned int*)(funct_ptr->arg+4);
        *temp_ptr=*iniNum;
        fprintf(plc_run_cpp,"%d,",*iniNum);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }
        temp_ptr = (NUM_TYPE *)pc_counter;
        /* Be careful about this part is different from most of other function blocks */
        /* The third argument is initialization value.*/
        iniVal = (unsigned int*)(funct_ptr->arg+8);
        *temp_ptr=*iniVal;
        fprintf(plc_run_cpp,"%d);\n",*iniVal);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg4 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg4--;
        }

}

/*******************************************************************/
/*
        Copy multi registers from source to destination address
*/
static void get_mulrcpy(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3,code_seg4;
        /* register area address */
        unsigned int localRegAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int *regIndex;
        unsigned int *cpyNum;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        code_seg4=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"%d,",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* Be careful about this part is different from most of other function blocks */
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+4);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"%d,",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }
        temp_ptr = (NUM_TYPE *)pc_counter;
        /* Be careful about this part is different from most of other function blocks */
        /* The third argument is copy number.*/
        cpyNum = (unsigned int*)(funct_ptr->arg+8);
        *temp_ptr=*cpyNum;
        fprintf(plc_run_cpp,"%d);\n",*cpyNum);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg4 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg4--;
        }

}


/*******************************************************************/
/*
        MAP 32 continuous I registor to one R register
*/
static void get_irmap(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3;
        /* register area address */
        unsigned int localRegAddr;
        unsigned int localIAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int localIOffset;
        unsigned int *regIndex;
        localIAddr = (unsigned int)s_mlc_i_bit;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        localIOffset = localIAddr-localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source I register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        *temp_ptr = (*regIndex*1) + localIOffset;/* get start register address*/
        fprintf(plc_run_cpp,"%d,",(*regIndex*1) + localIOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register address*/
        fprintf(plc_run_cpp,"%d);\n",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }

}

/*******************************************************************/
/*
        MAP N continuous I registor to one R register
*/
static void get_irmapN(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3,code_seg4;
        /* register area address */
        unsigned int localRegAddr;
        unsigned int localIAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int localIOffset;
        unsigned int *regIndex;
        localIAddr = (unsigned int)s_mlc_i_bit;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        localIOffset = localIAddr-localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        code_seg4=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source I register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        *temp_ptr = (*regIndex*1) + localIOffset;/* get start register address*/
        fprintf(plc_run_cpp,"%d,",(*regIndex*1) + localIOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register address*/
        fprintf(plc_run_cpp,"%d,",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }
        /* The third argument is the number to map, 1~32 */
        *pc_counter=*((char *)(funct_ptr->arg+10))-1;
        fprintf(plc_run_cpp,"%d)\n",*pc_counter);
        pc_counter++;
        code_ptr++;
        while(code_seg4 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg4--;
        }

}


/*******************************************************************/
/*
        MAP 32 continuous O registor to one R register
*/
static void get_ormap(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3;
        /* register area address */
        unsigned int localRegAddr;
        unsigned int localOAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int localOOffset;
        unsigned int *regIndex;
        localOAddr = (unsigned int)s_mlc_o_bit;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        localOOffset = localOAddr-localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source O register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        *temp_ptr = (*regIndex*1) + localOOffset;/* get start register */
        fprintf(plc_run_cpp,"%d,",(*regIndex*1) + localOOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"%d);\n",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }

}


/*******************************************************************/
/*
        MAP N continuous O registor to one R register
*/
static void get_ormapN(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3,code_seg4;
        /* register area address */
        unsigned int localRegAddr;
        unsigned int localOAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int localOOffset;
        unsigned int *regIndex;
        localOAddr = (unsigned int)s_mlc_o_bit;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        localOOffset = localOAddr-localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        code_seg4=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source O register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        *temp_ptr = (*regIndex*1) + localOOffset;/* get start register */
        fprintf(plc_run_cpp,"%d,",(*regIndex*1) + localOOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"%d);\n",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }
        /* The third argument is the number to map, 1~32 */
        *pc_counter=*((char *)(funct_ptr->arg+10))-1;
        fprintf(plc_run_cpp,"%d)\n",*pc_counter);
        pc_counter++;
        code_ptr++;
        while(code_seg4 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg4--;
        }
}

/*******************************************************************/
/*
        Copy register to register pointer or register pointer to register
*/
static void get_movrp(char *code_ptr, struct funct *funct_ptr)
{
        NUM_TYPE *temp_ptr;
        /* fixed code segment size */
        int code_seg1,code_seg2,code_seg3,code_seg4;
        /* register area address */
        unsigned int localRegAddr;
        /* IOCSA data area */
        unsigned int localDataAddr;
        /* register area offset */
        unsigned int localRegOffset;
        unsigned int *regIndex;
        localRegAddr = (unsigned int)s_mlc_reg;
        localDataAddr = (unsigned int )MLC_Data;
        localRegOffset = localRegAddr - localDataAddr;
        code_seg1=*code_ptr++;
        code_seg2=*code_ptr++;
        code_seg3=*code_ptr++;
        code_seg4=*code_ptr++;
        while(code_seg1 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg1--;
        }
        /* The first argument is source register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg2 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg2--;
        }
        /* The second argument is destination register starting address */
        temp_ptr = (NUM_TYPE *)pc_counter;
        regIndex = (unsigned int*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *regIndex)
        {
            *regIndex=*regIndex-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*regIndex*4) + localRegOffset;/* get start register */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*regIndex*4) + localRegOffset);
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;

        while(code_seg3 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg3--;
        }
        /* write register area offset */
        temp_ptr = (NUM_TYPE *)pc_counter;
        *temp_ptr=localRegOffset;
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
        while(code_seg4 >0 )
        {
            *pc_counter++=*code_ptr++;
            code_seg4--;
        }

}

/*******************************************************************/
static void get_shl_r( char *code_ptr, struct funct *funct_ptr )   /* get shllw/shrlw machine - two argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */

    uint32_t machine_cmp = generate_cmp(5, 0);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }

    if (funct_ptr->data.code == 0x09A ){  // SHLLW

        sprintf(buffer, "%s ", "SHLLW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


        uint32_t machine_beq = encode_beq(13*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
    




        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

    
    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    


    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
        uint32_t get_low_16bit =    generate_movt_reg_imm(0, 10) ;
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (get_low_16bit>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    

    
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
        uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(4, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
    
            uint32_t destination_index_shll_value_machine_code = generate_lsl_reg_reg(4,4,10,0) ; 
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_shll_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            sprintf(buffer, "%s 0x%lx", "20250119 func=shll destination_index_shll_value_machine_code=", destination_index_shll_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
            uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(4, 6, 7, 0);
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
    
    
    
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
    
    
    }
    else   if (funct_ptr->data.code == 0x09C ){  // SHRLW


        
            sprintf(buffer, "%s ", "SHRLW"); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView


            uint32_t machine_beq = encode_beq(13*4);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
            }





            Reg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *Reg)
            {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
            }


            /* get source */
            uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }



            uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }


            uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }


            uint32_t get_low_16bit =    generate_movt_reg_imm(0, 10) ;

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (get_low_16bit>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
            }



            Reg = (long*)(funct_ptr->arg+6);
            if(USR_REG_AREA_START <= *Reg)
            {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
            }


            uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }



            uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }

            sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView

            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(4, 6, 7, 0);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }


            uint32_t destination_index_SHRL_value_machine_code = generate_lsr_reg_reg(4,4,10,0) ; 

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }

            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView

            uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(4, 6, 7, 0);
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  



            uint32_t al_low_machine_code = mov_imm(0xff,  5);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
            uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value



            }




}



/*********************************************************************/
/*******************************************************************/
static void get_shl_r_x86( char *code_ptr, struct funct *funct_ptr )   /* get shllw/shrlw machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
}



/*********************************************************************/
static void get_shl_i( char *code_ptr, struct funct *funct_ptr )   /* get shllc/shrlc machine - two argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    unsigned char *chartemp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg,*Vaule;
    int s_a0_reg_ofs;
    unsigned char shift_val;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */


    uint32_t machine_cmp = generate_cmp(5, 0);
    //存 code
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
}
    if (funct_ptr->data.code == 0x09B ){  // SHLLC

    
        uint32_t machine_beq = encode_beq(8*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }

    
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
        
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
        uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(4, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
    
            Vaule = (long*)(funct_ptr->arg);
            shift_val=(unsigned char)*Vaule;
    
            uint32_t destination_index_shll_value_machine_code = generate_lsl_imm(4,4,shift_val,0) ; 
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_shll_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            sprintf(buffer, "%s 0x%lx", "20250119 func=shll destination_index_shll_value_machine_code=", destination_index_shll_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
            uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(4, 6, 7, 0);
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
    
    
    
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
    
    
    }

    else if (funct_ptr->data.code == 0x09D ){  // SHLLC



            
        uint32_t machine_beq = encode_beq(8*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }

    
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
        
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
        uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(4, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
    
            Vaule = (long*)(funct_ptr->arg);
            shift_val=(unsigned char)*Vaule;
    
            uint32_t destination_index_SHRL_value_machine_code = generate_lsr_imm(4,4,shift_val,0) ; 
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
            uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(4, 6, 7, 0);
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
    
    
    
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
    }




}



/*******************************************************************/
/*********************************************************************/
static void get_shl_i_x86( char *code_ptr, struct funct *funct_ptr )   /* get shllc/shrlc machine - two argument */
{
    NUM_TYPE *temp_ptr;
    unsigned char *chartemp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg,*Vaule;
    int s_a0_reg_ofs;
    unsigned char shift_val;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    chartemp_ptr = (unsigned char *)pc_counter;
    Vaule = (long*)(funct_ptr->arg);
    shift_val=(unsigned char)*Vaule;
    *chartemp_ptr = shift_val;  /* get immediate data */
    fprintf(plc_run_cpp,"%d);\n",*Vaule);
    pc_counter+=1;
    code_ptr+=1;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
}



/*******************************************************************/
static void get_arith_r( char *code_ptr, struct funct *funct_ptr )   /* get machine - three argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */
    a4=*code_ptr++;  /* macine word number */

    // 2025 armv7

    uint32_t machine_cmp = generate_cmp(5, 0);
    //存 code
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
}

    if (funct_ptr->data.code == 0x21){ //addw

        sprintf(buffer, "%s ", "addw"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(16*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s ", "addw2"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);

            sprintf(buffer, "%s ", "addw3"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

                sprintf(buffer, "%s ", "addw1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        sprintf(buffer, "%s 0x%d", "20250119 func=addw destination_index_add_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=addw destination_index_add_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    uint32_t destination_index_add_value_machine_code = add_reg_reg(9,9,10,0) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_add_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=addw destination_index_add_value_machine_code=", destination_index_add_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView





    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value

    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  addW2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	

    al_low_machine_code = mov_imm(0x0,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value


    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

        sprintf(buffer, "%s 0x%d", "20250119 func=addw destination_index_add_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  
    }
    else if (funct_ptr->data.code == 0x33){ // subw

        sprintf(buffer, "%s ", "subw"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(16*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s ", "subw2"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);

            sprintf(buffer, "%s ", "subw3"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

                sprintf(buffer, "%s ", "subw1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        sprintf(buffer, "%s 0x%d", "20250119 func=subw destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=subw destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    uint32_t destination_index_sub_value_machine_code = sub_reg_reg(9,9,10,0) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_sub_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=subw destination_index_sub_value_machine_code=", destination_index_sub_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView





    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value

    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  SUBW2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	

    al_low_machine_code = mov_imm(0x0,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value


    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

        sprintf(buffer, "%s 0x%d", "20250119 func=subw destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  

    }

    else if (funct_ptr->data.code == 0x45){ //MULW


        sprintf(buffer, "%s ", "20250119 func=mulw "); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


        uint32_t machine_beq = encode_beq(17*4);
        // write_code_to_memory(machine_beq, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    

    
        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        // *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */

        sprintf(buffer, "%s 0x%lx", "20250119 func=mulw source = ", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
        uint32_t source_index_low_machine_code = mov_imm( (*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t source_index_high_machine_code = generate_movt_reg_imm( (*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }            
    


    
        // temp_ptr = (NUM_TYPE *)pc_counter;
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    

        sprintf(buffer, "%s 0x%lx", "20250119 func=mulw destination = ", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    




        uint32_t mul_eax_ecx = generate_mul_reg_reg(9,9,10,0) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mul_eax_ecx >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        uint32_t mov_ecx_eax = generate_mov_reg_reg(10,9,0); 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_ecx_eax >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        

        uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
        uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
        //jmp  SUBW2   
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
    
        al_low_machine_code = mov_imm(0x0,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
        temp_ptr = (NUM_TYPE *)pc_counter;
        Reg = (long*)(funct_ptr->arg+6);
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
        sprintf(buffer, "%s 0x%lx", "20250119 func=mulw destination = ", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
        destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
        destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  
    
        
    
    

    }


    else if (funct_ptr->data.code == 0x57){ //DIVW



        

        sprintf(buffer, "%s ", "20250119 func=divw "); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

 




// 2. 若 AL == 0，則跳至 DIVCend
uint32_t beq_DIVCend = generate_branch(34 * 4, 0x0);
write_to_array(beq_DIVCend);

// 3. 清空 EDX (R10)
uint32_t mov_r10_0 = mov_imm(0, 10); // MOV r10, #0
write_to_array(mov_r10_0);


/* get source */
Reg = (long*)(funct_ptr->arg);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}




// 5. 設定 divisor (ecx -> r10)
uint32_t destination_index_low_value_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
write_to_array(destination_index_low_value_machine_code);

uint32_t destination_index_high_value_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
write_to_array(destination_index_high_value_machine_code);

uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
write_to_array(ldr_source_index_machine_code);





/* get destination */
Reg = (long*)(funct_ptr->arg+6);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}


uint32_t destination_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
write_to_array(destination_low_machine_code);



uint32_t destination_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
write_to_array(destination_high_machine_code);


// 4. 讀取 dividend (eax -> r9)
uint32_t ldr_r9_dest = generate_ldr_reg_regoffset(9, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r9_dest);







// 6. 若 divisor == 0，則跳至 DIVCend
uint32_t cmp_r10_0 = generate_cmp(10, 0); // CMP r10, #0
write_to_array(cmp_r10_0);

uint32_t beq_DIVCend2 = generate_branch(25 * 4, 0x0);
write_to_array(beq_DIVCend2);




// 2. 確保被除數 (r9) 為正數
uint32_t cmp_r9_0 = generate_cmp(9, 0);
write_to_array(cmp_r9_0);

// 若 `r9` < 0，則跳轉到 `make_r9_pos`
uint32_t bge_r9_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r9_ok);

uint32_t rsb_r9 = generate_rsb_imm(9, 9, 0, 0); // RSB r9, r9, #0
write_to_array(rsb_r9);

// 3. 確保除數 (r10) 為正數

write_to_array(cmp_r10_0);

// 若 `r10` < 0，則跳轉到 `make_r10_pos`
uint32_t bge_r10_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r10_ok);

uint32_t rsb_r10 = generate_rsb_imm(10, 10, 0, 0); // RSB r10, r10, #0
write_to_array(rsb_r10);

// 4. 初始化 quotient (r8 = 0)
uint32_t mov_r8_0 = mov_imm(0, 8); // MOV r8, #0
write_to_array(mov_r8_0);

// 5. 進入迴圈

uint32_t cmp_r9_r10 = generate_cmp_reg(9, 10); // CMP r9, r10
write_to_array(cmp_r9_r10);

uint32_t blt_DIVCend3 = generate_branch(4 * 4, 0xb); // BLT DIVCend
write_to_array(blt_DIVCend3);

uint32_t sub_r9_r10 = sub_reg_reg(9, 9, 10, 0); // SUB r9, r9, r10
write_to_array(sub_r9_r10);

uint32_t add_r8_1 = generate_add_imm(8, 8, 1); // ADD r8, r8, #1
write_to_array(add_r8_1);

uint32_t b_loop_start = generate_branch(-4 * 4, 0xe); // B loop_start
write_to_array(b_loop_start);



write_to_array(destination_low_machine_code);

write_to_array(destination_high_machine_code);

uint32_t ldr_r4_dest = generate_ldr_reg_regoffset(4, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r4_dest);

// 1. 用 XOR 判斷被除數 (r9) 和 除數 (r10) 是否異號，結果存入 r4
uint32_t eor_r4 = generate_eor_reg(4, 4, 10, 0); // EOR r4, r9, r10
write_to_array(eor_r4);


uint32_t lsr_32bit_r9 = generate_lsr_imm(4, 4, 31, 0); // EOR r4, r9, r10
write_to_array(lsr_32bit_r9);

// 6. 若原本被除數和除數異號，則將 quotient 轉為負數
uint32_t cmp_r4_0 = generate_cmp(4, 0);
write_to_array(cmp_r4_0);

uint32_t beq_neg_quotient = generate_branch(2 * 4, 0x0);
write_to_array(beq_neg_quotient);

uint32_t rsb_r8 = generate_rsb_imm(8, 8, 0, 0); // RSB r8, r8, #0
write_to_array(rsb_r8);



write_to_array(destination_low_machine_code);




write_to_array(destination_high_machine_code);

// 7. 儲存結果到記憶體
uint32_t str_r8_dest = generate_str_reg_regoffset(8, 6, 7, 0); // STR r8, [r6, r7]
write_to_array(str_r8_dest);



// 15. 設定 AL = 0xFF
uint32_t mov_r5_FF = mov_imm(0xFF, 5); // MOV r5, #0xFF
write_to_array(mov_r5_FF);


    

  
    }


    else if (funct_ptr->data.code == 0x81){ //andw


        sprintf(buffer, "%s ", "andw"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(16*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s ", "andw2"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);

            sprintf(buffer, "%s ", "andw3"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

                sprintf(buffer, "%s ", "andw1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        sprintf(buffer, "%s 0x%d", "20250119 func=andw destination_index_and_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=andw destination_index_and_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    uint32_t destination_index_and_value_machine_code =  generate_and_reg(9,9,10,0) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_and_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=andw destination_index_and_value_machine_code=", destination_index_and_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView





    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value

    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  andW2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	

    al_low_machine_code = mov_imm(0x0,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value


    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

        sprintf(buffer, "%s 0x%d", "20250119 func=andw destination_index_and_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  
  
    

  
    }


    else if (funct_ptr->data.code == 0x93){ //orw
        sprintf(buffer, "%s ", "orw"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(16*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s ", "orw2"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);

            sprintf(buffer, "%s ", "orw3"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

                sprintf(buffer, "%s ", "orw1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        sprintf(buffer, "%s 0x%d", "20250119 func=orw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=orw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    uint32_t destination_index_or_value_machine_code =  generate_or_reg(9,9,10,0) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_or_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=orw destination_index_or_value_machine_code=", destination_index_or_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView





    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value

    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  orW2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	

    al_low_machine_code = mov_imm(0x0,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value


    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

        sprintf(buffer, "%s 0x%d", "20250119 func=orw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  
  
    


    }
    else if (funct_ptr->data.code == 0x0A5){ //xorw
        sprintf(buffer, "%s ", "eorw"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    
    uint32_t machine_beq = encode_beq(16*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
        sprintf(buffer, "%s ", "eorw2"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    temp_ptr = (NUM_TYPE *)pc_counter;
    
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;/* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    
            sprintf(buffer, "%s ", "eorw3"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    /* get source */
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
                sprintf(buffer, "%s ", "eorw1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
        sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    
    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
        sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
    
    uint32_t destination_index_eor_value_machine_code =  generate_eor_reg(9,9,10,0) ; 
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_eor_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
        sprintf(buffer, "%s 0x%lx", "20250119 func=eeorw destination_index_eor_value_machine_code=", destination_index_eor_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    
    
    
    
    uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value
    
    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  eeorw2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
    
    al_low_machine_code = mov_imm(0x0,  5);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    al_high_machine_code = generate_movt_reg_imm(0, 5);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value
    
    
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
        sprintf(buffer, "%s 0x%d", "20250119 func=eeorw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  
    
    
    
    
    }
    
    
    else if (funct_ptr->data.code == 0x56){ //modw








// 2. 若 AL == 0，則跳至 DIVCend
uint32_t beq_DIVCend = generate_branch(34 * 4, 0x0);
write_to_array(beq_DIVCend);

// 3. 清空 EDX (R10)
uint32_t mov_r10_0 = mov_imm(0, 10); // MOV r10, #0
write_to_array(mov_r10_0);


/* get source */
Reg = (long*)(funct_ptr->arg);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}




// 5. 設定 divisor (ecx -> r10)
uint32_t destination_index_low_value_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
write_to_array(destination_index_low_value_machine_code);

uint32_t destination_index_high_value_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
write_to_array(destination_index_high_value_machine_code);

uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
write_to_array(ldr_source_index_machine_code);





/* get destination */
Reg = (long*)(funct_ptr->arg+6);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}


uint32_t destination_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
write_to_array(destination_low_machine_code);



uint32_t destination_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
write_to_array(destination_high_machine_code);


// 4. 讀取 dividend (eax -> r9)
uint32_t ldr_r9_dest = generate_ldr_reg_regoffset(9, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r9_dest);







// 6. 若 divisor == 0，則跳至 DIVCend
uint32_t cmp_r10_0 = generate_cmp(10, 0); // CMP r10, #0
write_to_array(cmp_r10_0);

uint32_t beq_DIVCend2 = generate_branch(25 * 4, 0x0);
write_to_array(beq_DIVCend2);




// 2. 確保被除數 (r9) 為正數
uint32_t cmp_r9_0 = generate_cmp(9, 0);
write_to_array(cmp_r9_0);

// 若 `r9` < 0，則跳轉到 `make_r9_pos`
uint32_t bge_r9_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r9_ok);

uint32_t rsb_r9 = generate_rsb_imm(9, 9, 0, 0); // RSB r9, r9, #0
write_to_array(rsb_r9);

// 3. 確保除數 (r10) 為正數

write_to_array(cmp_r10_0);

// 若 `r10` < 0，則跳轉到 `make_r10_pos`
uint32_t bge_r10_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r10_ok);

uint32_t rsb_r10 = generate_rsb_imm(10, 10, 0, 0); // RSB r10, r10, #0
write_to_array(rsb_r10);

// 4. 初始化 quotient (r8 = 0)
uint32_t mov_r8_0 = mov_imm(0, 8); // MOV r8, #0
write_to_array(mov_r8_0);

// 5. 進入迴圈

uint32_t cmp_r9_r10 = generate_cmp_reg(9, 10); // CMP r9, r10
write_to_array(cmp_r9_r10);

uint32_t blt_DIVCend3 = generate_branch(4 * 4, 0xb); // BLT DIVCend
write_to_array(blt_DIVCend3);

uint32_t sub_r9_r10 = sub_reg_reg(9, 9, 10, 0); // SUB r9, r9, r10
write_to_array(sub_r9_r10);

uint32_t add_r8_1 = generate_add_imm(8, 8, 1); // ADD r8, r8, #1
write_to_array(add_r8_1);

uint32_t b_loop_start = generate_branch(-4 * 4, 0xe); // B loop_start
write_to_array(b_loop_start);



write_to_array(destination_low_machine_code);

write_to_array(destination_high_machine_code);

uint32_t ldr_r4_dest = generate_ldr_reg_regoffset(4, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r4_dest);

// 1. 用 XOR 判斷被除數 (r9) 和 除數 (r10) 是否異號，結果存入 r4
uint32_t eor_r4 = generate_eor_reg(4, 4, 10, 0); // EOR r4, r9, r10
write_to_array(eor_r4);


uint32_t lsr_32bit_r9 = generate_lsr_imm(4, 4, 31, 0); // EOR r4, r9, r10
write_to_array(lsr_32bit_r9);

// 6. 若原本被除數和除數異號，則將 quotient 轉為負數
uint32_t cmp_r4_0 = generate_cmp(4, 0);
write_to_array(cmp_r4_0);

uint32_t beq_neg_quotient = generate_branch(2 * 4, 0x0);
write_to_array(beq_neg_quotient);

uint32_t rsb_r8 = generate_rsb_imm(9, 9, 0, 0); // RSB r8, r8, #0
write_to_array(rsb_r8);



write_to_array(destination_low_machine_code);




write_to_array(destination_high_machine_code);

// 7. 儲存結果到記憶體
uint32_t str_r8_dest = generate_str_reg_regoffset(9, 6, 7, 0); // STR r8, [r6, r7]
write_to_array(str_r8_dest);



// 15. 設定 AL = 0xFF
uint32_t mov_r5_FF = mov_imm(0xFF, 5); // MOV r5, #0xFF
write_to_array(mov_r5_FF);






    }
}

/*********************************************************************/
static void get_arith_i( char *code_ptr, struct funct *funct_ptr  )   /* get machine - three argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long int j,k;
    long *Reg,*Vaule;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    sprintf(buffer, "%s 0x%lx", "20250119 func=get_arith_i funct_ptr->data.code=",  funct_ptr->data.code); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

    // a1=*code_ptr++;  /* macine word number */
    // a2=*code_ptr++;  /* macine word number */
    // a3=*code_ptr++;  /* macine word number */
    // a4=*code_ptr++;  /* macine word number */


    // while(a1>0)
    // {
    //     *pc_counter++=*code_ptr++;  /* get machine code */
    //     a1--;
    // }

    uint32_t machine_cmp = generate_cmp(5, 0);
    // write_code_to_memory(machine_cmp, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    if (funct_ptr->data.code == 0x27){ //addc

    uint32_t machine_beq = encode_beq(13*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    // temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    Vaule = (long*)(funct_ptr->arg);

    uint32_t destination_index_add_value_machine_code = generate_add_imm(9,9,*Vaule) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_add_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
        //jmp  SUBW2   
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
    
        al_low_machine_code = mov_imm(0x0,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
        temp_ptr = (NUM_TYPE *)pc_counter;
        Reg = (long*)(funct_ptr->arg+6);
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
            sprintf(buffer, "%s 0x%d", "20250119 func=subw destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
        destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
        destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  
    
    }
    /***************************************************************/
    else if (funct_ptr->data.code == 0x39){ //subc
    uint32_t machine_beq = encode_beq(13*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    // temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    Vaule = (long*)(funct_ptr->arg);

    uint32_t destination_index_sub_value_machine_code = generate_sub_imm(9,9,*Vaule,1) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_sub_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=subc destination_index_sub_value_machine_code=", destination_index_sub_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value

    uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
    //jmp  SUBW2   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	

    al_low_machine_code = mov_imm(0x0,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
    	// LDR r4, [r6, r7]  @ Load Register value


    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

        sprintf(buffer, "%s 0x%d", "20250119 func=subw destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

    destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }  

    




    }
    else if (funct_ptr->data.code == 0x4b){ //MULC


        
 
    
    

    }

    else if (funct_ptr->data.code == 0x5d){ //DIVC



        




// 2. 若 AL == 0，則跳至 DIVCend
uint32_t beq_DIVCend = generate_branch(33 * 4, 0x0);
write_to_array(beq_DIVCend);

// 3. 清空 EDX (R10)
uint32_t mov_r10_0 = mov_imm(0, 10); // MOV r10, #0
write_to_array(mov_r10_0);

/* get destination */
Reg = (long*)(funct_ptr->arg+6);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}


uint32_t destination_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
write_to_array(destination_low_machine_code);



uint32_t destination_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
write_to_array(destination_high_machine_code);


// 4. 讀取 dividend (eax -> r9)
uint32_t ldr_r9_dest = generate_ldr_reg_regoffset(9, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r9_dest);


Vaule = (long*)(funct_ptr->arg);

// 5. 設定 divisor (ecx -> r10)
uint32_t destination_index_low_value_machine_code = mov_imm(*Vaule, 10) ; 
write_to_array(destination_index_low_value_machine_code);

uint32_t destination_index_high_value_machine_code = generate_movt_reg_imm(*Vaule, 10) ; 
write_to_array(destination_index_high_value_machine_code);



sprintf(buffer, "%s 0x%d", "20250119 func=divc destination_index_sdivc_value_machine_code=",*Vaule); // 格式化字符串
OutputDebugStringA(buffer); // 輸出到 DebugView




// 6. 若 divisor == 0，則跳至 DIVCend
uint32_t cmp_r10_0 = generate_cmp(10, 0); // CMP r10, #0
write_to_array(cmp_r10_0);

uint32_t beq_DIVCend2 = generate_branch(25 * 4, 0x0);
write_to_array(beq_DIVCend2);




// 2. 確保被除數 (r9) 為正數
uint32_t cmp_r9_0 = generate_cmp(9, 0);
write_to_array(cmp_r9_0);

// 若 `r9` < 0，則跳轉到 `make_r9_pos`
uint32_t bge_r9_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r9_ok);

uint32_t rsb_r9 = generate_rsb_imm(9, 9, 0, 0); // RSB r9, r9, #0
write_to_array(rsb_r9);

// 3. 確保除數 (r10) 為正數

write_to_array(cmp_r10_0);

// 若 `r10` < 0，則跳轉到 `make_r10_pos`
uint32_t bge_r10_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r10_ok);

uint32_t rsb_r10 = generate_rsb_imm(10, 10, 0, 0); // RSB r10, r10, #0
write_to_array(rsb_r10);

// 4. 初始化 quotient (r8 = 0)
uint32_t mov_r8_0 = mov_imm(0, 8); // MOV r8, #0
write_to_array(mov_r8_0);

// 5. 進入迴圈

uint32_t cmp_r9_r10 = generate_cmp_reg(9, 10); // CMP r9, r10
write_to_array(cmp_r9_r10);

uint32_t blt_DIVCend3 = generate_branch(4 * 4, 0xb); // BLT DIVCend
write_to_array(blt_DIVCend3);

uint32_t sub_r9_r10 = sub_reg_reg(9, 9, 10, 0); // SUB r9, r9, r10
write_to_array(sub_r9_r10);

uint32_t add_r8_1 = generate_add_imm(8, 8, 1); // ADD r8, r8, #1
write_to_array(add_r8_1);

uint32_t b_loop_start = generate_branch(-4 * 4, 0xe); // B loop_start
write_to_array(b_loop_start);



write_to_array(destination_low_machine_code);

write_to_array(destination_high_machine_code);

uint32_t ldr_r4_dest = generate_ldr_reg_regoffset(4, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r4_dest);

// 1. 用 XOR 判斷被除數 (r9) 和 除數 (r10) 是否異號，結果存入 r4
uint32_t eor_r4 = generate_eor_reg(4, 4, 10, 0); // EOR r4, r9, r10
write_to_array(eor_r4);


uint32_t lsr_32bit_r9 = generate_lsr_imm(4, 4, 31, 0); // EOR r4, r9, r10
write_to_array(lsr_32bit_r9);

// 6. 若原本被除數和除數異號，則將 quotient 轉為負數
uint32_t cmp_r4_0 = generate_cmp(4, 0);
write_to_array(cmp_r4_0);

uint32_t beq_neg_quotient = generate_branch(2 * 4, 0x0);
write_to_array(beq_neg_quotient);

uint32_t rsb_r8 = generate_rsb_imm(8, 8, 0, 0); // RSB r8, r8, #0
write_to_array(rsb_r8);



write_to_array(destination_low_machine_code);




write_to_array(destination_high_machine_code);

// 7. 儲存結果到記憶體
uint32_t str_r8_dest = generate_str_reg_regoffset(8, 6, 7, 0); // STR r8, [r6, r7]
write_to_array(str_r8_dest);



// 15. 設定 AL = 0xFF
uint32_t mov_r5_FF = mov_imm(0xFF, 5); // MOV r5, #0xFF
write_to_array(mov_r5_FF);


    
    

    }


    else if(funct_ptr->data.code == 0x87){ //andc

          uint32_t machine_beq = encode_beq(13*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    // temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    Vaule = (long*)(funct_ptr->arg);

    uint32_t destination_index_and_value_machine_code = generate_and_imm(9,9,*Vaule) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_and_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
        //jmp  SUBW2   
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
    
        al_low_machine_code = mov_imm(0x0,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
        temp_ptr = (NUM_TYPE *)pc_counter;
        Reg = (long*)(funct_ptr->arg+6);
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
            sprintf(buffer, "%s 0x%d", "20250119 func=andc destination_index_sub_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
        destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
        destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  
    }


    else if(funct_ptr->data.code == 0x99){ //ORC

          uint32_t machine_beq = encode_beq(13*4);
    // write_code_to_memory(machine_beq, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    // temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    Vaule = (long*)(funct_ptr->arg);

    uint32_t destination_index_and_value_machine_code = generate_or_imm(9,9,*Vaule,0) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_and_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
		//movt al , 0
	uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
        //jmp  orc2   
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
    
        al_low_machine_code = mov_imm(0x0,  5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        al_high_machine_code = generate_movt_reg_imm(0, 5);
    
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
    
    
        temp_ptr = (NUM_TYPE *)pc_counter;
        Reg = (long*)(funct_ptr->arg+6);
        *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
            sprintf(buffer, "%s 0x%d", "20250119 func=orc destination_index_orc_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
    
        destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
        destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  


    }
   
    else if(funct_ptr->data.code == 0x0Ab){ //xorc

        uint32_t machine_beq = encode_beq(13*4);
    // write_code_to_memory(machine_beq, &pc_counter);
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }
    
    
    
    // temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
      *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
      isAccessingUsrRegArea=1;
    }/* get destination */

    
    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }
    
    
    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }
    
    
    uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
    
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }
    
    
    Vaule = (long*)(funct_ptr->arg);
    
    uint32_t destination_index_eor_value_machine_code = generate_eor_imm(9,9,*Vaule,0) ;
    
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (destination_index_eor_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }
    
    
    uint32_t al_low_machine_code = mov_imm(0xff,  5);
    
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }	
      //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
    
      for (size_t i = 0; i < sizeof(uint32_t); i++) {
              *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
              pc_counter++;                            // 指向下一個位元組
      }    
      uint32_t jmp_fun2 = generate_branch(3*4, 0xe); // Always (0xE)
      //jmp  xor2   
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (jmp_fun2>> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }	
    
      al_low_machine_code = mov_imm(0x0,  5);
    
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }	
          //movt al , 0
      al_high_machine_code = generate_movt_reg_imm(0, 5);
    
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }    
          // LDR r4, [r6, r7]  @ Load Register value
    
    
      temp_ptr = (NUM_TYPE *)pc_counter;
      Reg = (long*)(funct_ptr->arg+6);
      *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    
          sprintf(buffer, "%s 0x%d", "20250119 func=xor destination_index_xor_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
          OutputDebugStringA(buffer); // 輸出到 DebugView
    
      destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
      // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }
    
    
    
      destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
      // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }
    
      uint32_t str_destination_index_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
      // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                  *pc_counter = (str_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                  pc_counter++;                            // 指向下一個位元組
          }  
    
    
    }
    
    else if(funct_ptr->data.code == 0x5c){ //modc


        sprintf(buffer, "%s ", "20250119 func=divc 1"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        





// 2. 若 AL == 0，則跳至 DIVCend
uint32_t beq_DIVCend = generate_branch(33 * 4, 0x0);
write_to_array(beq_DIVCend);

// 3. 清空 EDX (R10)
uint32_t mov_r10_0 = mov_imm(0, 10); // MOV r10, #0
write_to_array(mov_r10_0);

/* get destination */
Reg = (long*)(funct_ptr->arg+6);
if(USR_REG_AREA_START <= *Reg)
{
    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
    isAccessingUsrRegArea=1;
}


uint32_t destination_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
write_to_array(destination_low_machine_code);



uint32_t destination_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
write_to_array(destination_high_machine_code);


// 4. 讀取 dividend (eax -> r9)
uint32_t ldr_r9_dest = generate_ldr_reg_regoffset(9, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r9_dest);


Vaule = (long*)(funct_ptr->arg);

// 5. 設定 divisor (ecx -> r10)
uint32_t destination_index_low_value_machine_code = mov_imm(*Vaule, 10) ; 
write_to_array(destination_index_low_value_machine_code);

uint32_t destination_index_high_value_machine_code = generate_movt_reg_imm(*Vaule, 10) ; 
write_to_array(destination_index_high_value_machine_code);



sprintf(buffer, "%s 0x%d", "20250119 func=divc destination_index_sdivc_value_machine_code=",*Vaule); // 格式化字符串
OutputDebugStringA(buffer); // 輸出到 DebugView




// 6. 若 divisor == 0，則跳至 DIVCend
uint32_t cmp_r10_0 = generate_cmp(10, 0); // CMP r10, #0
write_to_array(cmp_r10_0);

uint32_t beq_DIVCend2 = generate_branch(25 * 4, 0x0);
write_to_array(beq_DIVCend2);




// 2. 確保被除數 (r9) 為正數
uint32_t cmp_r9_0 = generate_cmp(9, 0);
write_to_array(cmp_r9_0);

// 若 `r9` < 0，則跳轉到 `make_r9_pos`
uint32_t bge_r9_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r9_ok);

uint32_t rsb_r9 = generate_rsb_imm(9, 9, 0, 0); // RSB r9, r9, #0
write_to_array(rsb_r9);

// 3. 確保除數 (r10) 為正數

write_to_array(cmp_r10_0);

// 若 `r10` < 0，則跳轉到 `make_r10_pos`
uint32_t bge_r10_ok = generate_branch(2 * 4, 0xa); // BGE -> 跳過 `RSB`
write_to_array(bge_r10_ok);

uint32_t rsb_r10 = generate_rsb_imm(10, 10, 0, 0); // RSB r10, r10, #0
write_to_array(rsb_r10);

// 4. 初始化 quotient (r8 = 0)
uint32_t mov_r8_0 = mov_imm(0, 8); // MOV r8, #0
write_to_array(mov_r8_0);

// 5. 進入迴圈

uint32_t cmp_r9_r10 = generate_cmp_reg(9, 10); // CMP r9, r10
write_to_array(cmp_r9_r10);

uint32_t blt_DIVCend3 = generate_branch(4 * 4, 0xb); // BLT DIVCend
write_to_array(blt_DIVCend3);

uint32_t sub_r9_r10 = sub_reg_reg(9, 9, 10, 0); // SUB r9, r9, r10
write_to_array(sub_r9_r10);

uint32_t add_r8_1 = generate_add_imm(8, 8, 1); // ADD r8, r8, #1
write_to_array(add_r8_1);

uint32_t b_loop_start = generate_branch(-4 * 4, 0xe); // B loop_start
write_to_array(b_loop_start);



write_to_array(destination_low_machine_code);

write_to_array(destination_high_machine_code);

uint32_t ldr_r4_dest = generate_ldr_reg_regoffset(4, 6, 7, 0); // LDR r9, [r6, r7]
write_to_array(ldr_r4_dest);

// 1. 用 XOR 判斷被除數 (r9) 和 除數 (r10) 是否異號，結果存入 r4
uint32_t eor_r4 = generate_eor_reg(4, 4, 10, 0); // EOR r4, r9, r10
write_to_array(eor_r4);


uint32_t lsr_32bit_r9 = generate_lsr_imm(4, 4, 31, 0); // EOR r4, r9, r10
write_to_array(lsr_32bit_r9);

// 6. 若原本被除數和除數異號，則將 quotient 轉為負數
uint32_t cmp_r4_0 = generate_cmp(4, 0);
write_to_array(cmp_r4_0);

uint32_t beq_neg_quotient = generate_branch(2 * 4, 0x0);
write_to_array(beq_neg_quotient);

uint32_t rsb_r8 = generate_rsb_imm(9, 9, 0, 0); // RSB r8, r8, #0
write_to_array(rsb_r8);



write_to_array(destination_low_machine_code);




write_to_array(destination_high_machine_code);

// 7. 儲存結果到記憶體
uint32_t str_r8_dest = generate_str_reg_regoffset(9, 6, 7, 0); // STR r8, [r6, r7]
write_to_array(str_r8_dest);



// 15. 設定 AL = 0xFF
uint32_t mov_r5_FF = mov_imm(0xFF, 5); // MOV r5, #0xFF
write_to_array(mov_r5_FF);




        
        }
    
   
   
    // while(a4>0)
    // {
    //     *pc_counter++=*code_ptr++;  /* get machine code */
    //     a4--;
    // }
}
/*********************************************************************/
/*********************************************************************/
static void get_arith_i_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - three argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long int j,k;
    long *Reg,*Vaule;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x10000 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */
    a4=*code_ptr++;  /* macine word number */

    sprintf(buffer, 
        "20250217 func=debug_info j=0x%lx k=0x%lx s_a0_reg_ofs=0x%x a1=0x%x a2=0x%x a3=0x%x a4=0x%x", 
        j, k, s_a0_reg_ofs, a1, a2, a3, a4);
    OutputDebugStringA(buffer); // 輸出到 DebugView
    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);



    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Vaule = (long*)(funct_ptr->arg);
    *temp_ptr = *Vaule;  /* get immediate data */
    fprintf(plc_run_cpp,"%d);\n",*Vaule);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a4>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }
}
/*********************************************************************/
static void get_arith_p( char *code_ptr, struct funct *funct_ptr )   /* get machine - three argument */
{
   NUM_TYPE *temp_ptr;
   int a1,a2,a3,a4,a5;
   long *Reg;
   long int j,k;
   int s_a0_reg_ofs;

   j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
   k=(long)MLC_Data;
   s_a0_reg_ofs=j-k;

   a1=*code_ptr++;  /* macine word number */
   a2=*code_ptr++;  /* macine word number */
   a3=*code_ptr++;  /* macine word number */
   a4=*code_ptr++;  /* macine word number */
   a5=*code_ptr++;  /* macine word number */

   while(a1>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a1--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   Reg = (long*)funct_ptr->arg;
   if(USR_REG_AREA_START <= *Reg)
   {
       *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
       isAccessingUsrRegArea=1;
   }
   *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  // get Psource
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while(a2>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a2--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   *temp_ptr = s_a0_reg_ofs;      /* get register pointer */
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while(a3>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a3--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   Reg = (long*)(funct_ptr->arg+6);
   if(USR_REG_AREA_START <= *Reg)
   {
       *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
       isAccessingUsrRegArea=1;
   }
   *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  // get Pdest
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while(a4>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a4--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   *temp_ptr = s_a0_reg_ofs;      /* get register pointer */
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while(a5>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a5--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   *temp_ptr = s_a0_reg_ofs;      /* get register pointer */
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

}

/********************************************************************/
static void get_cmp_r( char *code_ptr, struct funct   *funct_ptr )   /* get machine - two argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;




    uint32_t machine_cmp = generate_cmp(5, 0);
    // write_code_to_memory(machine_cmp, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    if (funct_ptr->data.code ==0x5E){ //CMPWhi


        sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        
        
        uint32_t machine_beq = encode_beq(12*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        
        
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        
        /* get source */ //ecx
        uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        //edx
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
            uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t branch_machine_code = generate_branch(3 * 4, 0xa) ;
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
        
        
        
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
        
        


    }
    else if (funct_ptr->data.code ==0x5F){ //CMPWhieq

        sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        
        
        uint32_t machine_beq = encode_beq(12*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        
        
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        
        /* get source */ //ecx
        uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
        
        //edx
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
            uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t branch_machine_code = generate_branch(3 * 4, 0xc) ;
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
        
        
        
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
        
        
    }

    else if (funct_ptr->data.code ==0x62){ //CMPWls

        sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        
        
        uint32_t machine_beq = encode_beq(12*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        
        
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        
        /* get source */ //ecx
        uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
        
        //edx
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
            uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t branch_machine_code = generate_branch(3 * 4, 0xd) ;
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
        
        
        
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
        
        
    }
    else if (funct_ptr->data.code ==0x63){ //CMPWlseq

        sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
        
        
        uint32_t machine_beq = encode_beq(12*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        
        
        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        
        
        /* get source */ //ecx
        uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
        
        
        uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
        
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }
        
        
        
        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
        fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
        
        //edx
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
        
            uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
        
            uint32_t branch_machine_code = generate_branch(3 * 4, 0xb) ;
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
        
        
        
        uint32_t al_low_machine_code = mov_imm(0xff,  5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value
        
        
    }   
    else if (funct_ptr->data.code ==0x66){ //CMPWeq

        
sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(12*4);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }





    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }



    /* get source */ //ecx
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }



    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }



    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }

    //edx
    uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }


        uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t branch_machine_code = generate_branch(3 * 4, 0x1) ;
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value


    
    }   
    else if (funct_ptr->data.code ==0x67){ //CMPWneq

                
        sprintf(buffer, "%s ", "CMPW"); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView


        uint32_t machine_beq = encode_beq(12*4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }





        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }



        /* get source */ //ecx
        uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



        uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
        }



        Reg = (long*)(funct_ptr->arg);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }

        //edx
        uint32_t destination_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }



        uint32_t destination_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }

            sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", (*Reg*4) + s_a0_reg_ofs); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView

            uint32_t ldr_destination_index_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0);

                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_destination_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }


            uint32_t destination_index_SHRL_value_machine_code = generate_cmp_reg(10,9) ; 

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (destination_index_SHRL_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }

            sprintf(buffer, "%s 0x%lx", "20250119 func=SHRL destination_index_SHRL_value_machine_code=", destination_index_SHRL_value_machine_code); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView

            uint32_t branch_machine_code = generate_branch(3 * 4, 0x0) ;
            // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  



        uint32_t al_low_machine_code = mov_imm(0xff,  5);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }	
            //movt al , 0
        uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }    
            // LDR r4, [r6, r7]  @ Load Register value


    }



}
/*******************************************************************/
static void get_cmp_i( char *code_ptr, struct funct   *funct_ptr )   /* get machine - two argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */

    uint32_t machine_cmp = generate_cmp(5, 0);
    // write_code_to_memory(machine_cmp, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        if (funct_ptr->data.code == 0x6A) // CMPChi
        {

    sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(11*4);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }





    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);


    /* get source */ //ecx
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }



    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }



    Reg = (long*)(funct_ptr->arg); /* get immediate data */

    //edx
    uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView



        uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t branch_machine_code = generate_branch(3 * 4, 0xa) ;
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value



        }

        else if (funct_ptr->data.code == 0x6B) // CMPChieq
        {
        
    sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(11*4);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }





    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);


    /* get source */ //ecx
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }



    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }



    Reg = (long*)(funct_ptr->arg); /* get immediate data */

    //edx
    uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView



        uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t branch_machine_code = generate_branch(3 * 4, 0xc) ;
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value



        }

        else if (funct_ptr->data.code == 0x6C) //  CMPCls
        {

            
    sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(11*4);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }





    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);


    /* get source */ //ecx
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }



    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }



    Reg = (long*)(funct_ptr->arg); /* get immediate data */

    //edx
    uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView



        uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t branch_machine_code = generate_branch(3 * 4, 0xd) ;
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value


        }

        else if (funct_ptr->data.code == 0x6D) // CMPClseq
        {


            sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
            
            
            uint32_t machine_beq = encode_beq(11*4);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            
            
            Reg = (long*)(funct_ptr->arg+6);
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
            fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
            
            
            /* get source */ //ecx
            uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
            
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
            
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            Reg = (long*)(funct_ptr->arg); /* get immediate data */
            
            //edx
            uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
            
            uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            
            
            
                uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            
                uint32_t branch_machine_code = generate_branch(3 * 4, 0xb) ;
                // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }  
            
            
            
            uint32_t al_low_machine_code = mov_imm(0xff,  5);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }	
                //movt al , 0
            uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }    
                // LDR r4, [r6, r7]  @ Load Register value
            
            
        }

        else if (funct_ptr->data.code == 0x6E) // CMPCeq
        {

            sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
            OutputDebugStringA(buffer); // 輸出到 DebugView
            
            
            uint32_t machine_beq = encode_beq(11*4);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            
            
            Reg = (long*)(funct_ptr->arg+6);
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
            fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
            
            
            /* get source */ //ecx
            uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
            
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
            
            
            uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
            
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }
            
            
            
            Reg = (long*)(funct_ptr->arg); /* get immediate data */
            
            //edx
            uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
            
            uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            
            
            
                uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
                OutputDebugStringA(buffer); // 輸出到 DebugView
            
                uint32_t branch_machine_code = generate_branch(3 * 4, 0x1) ;
                // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }  
            
            
            
            uint32_t al_low_machine_code = mov_imm(0xff,  5);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }	
                //movt al , 0
            uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }    
                // LDR r4, [r6, r7]  @ Load Register value
            
            
        }

        else if (funct_ptr->data.code == 0x6F) // CMPCneq
        {

    sprintf(buffer, "%s ", "CMPC"); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


    uint32_t machine_beq = encode_beq(11*4);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }





    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);


    /* get source */ //ecx
    uint32_t source_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }



    uint32_t source_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (source_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }



    Reg = (long*)(funct_ptr->arg); /* get immediate data */

    //edx
    uint32_t destination_index_low_machine_code = mov_imm(*Reg,  9);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



    uint32_t destination_index_high_machine_code = generate_movt_reg_imm(*Reg, 9);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%d", "20250119 func=cmpw destination_index_or_value_machine_code=", *Reg); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView



        uint32_t destination_index_cmp_value_machine_code = generate_cmp_reg(10,9) ; 

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (destination_index_cmp_value_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        sprintf(buffer, "%s 0x%lx", "20250119 func=cmp destination_index_cmp_value_machine_code=", destination_index_cmp_value_machine_code); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView

        uint32_t branch_machine_code = generate_branch(3 * 4, 0x0) ;
        // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (branch_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  



    uint32_t al_low_machine_code = mov_imm(0xff,  5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }	
        //movt al , 0
    uint32_t al_high_machine_code = generate_movt_reg_imm(0, 5);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }    
        // LDR r4, [r6, r7]  @ Load Register value


        }

}
/*******************************************************************/

/********************************************************************/
static void get_cmp_r_x86( char *code_ptr, struct funct   *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    /* 850125 exchange arg1 with arg2  */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    /* 850125 exchange arg1 with arg2  */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
}
/*******************************************************************/
static void get_cmp_i_x86( char *code_ptr, struct funct   *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */
    a3=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = *Reg;  /* get immediate data */
    fprintf(plc_run_cpp,"%d);\n",*Reg);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
}
/*******************************************************************/
static void get_mov_r( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    uint32_t machine_cmp = generate_cmp(5, 0);
    write_to_array(machine_cmp);
    // 2. 若 AL == 0，則跳至 DIVCend
    uint32_t beq_DIVCend = generate_branch(7 * 4, 0x0);
    write_to_array(beq_DIVCend);
    
    
    
    /* get source */
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    
    
    
    
    // 5. 設定 divisor (ecx -> r10)
    uint32_t destination_index_low_value_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
    write_to_array(destination_index_low_value_machine_code);
    
    uint32_t destination_index_high_value_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7) ; 
    write_to_array(destination_index_high_value_machine_code);
    
    uint32_t ldr_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_source_index_machine_code);
    
    
    
    
    
    /* get destination */
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    
    
    uint32_t destination_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
    write_to_array(destination_low_machine_code);
    
    
    
    uint32_t destination_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
    write_to_array(destination_high_machine_code);
    
    
    // 4. 讀取 dividend (eax -> r9)
    uint32_t str_r9_dest = generate_str_reg_regoffset(10, 6, 7, 0); // LDR r9, [r6, r7]
    write_to_array(str_r9_dest);
    
    
    
    
    
}
/*******************************************************************/
static void get_mov_r_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;
}

/*******************************************************************/
static void get_mov_i( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    char buffer[256]; // 分配足夠的debug緩存
    // 2025 armv7
    long *Reg;
    NUM_TYPE *temp_ptr;
    int a1,a2;
    long int j,k;
    //uint32_t machine_cmp, machine_beq;
    int s_a0_reg_ofs;
   // uint32_t low_machine_code, high_machine_code, machine_code2;
    //uint32_t Index1 = data_A + offset; //A3000
    //20250119 arm code : Mov, MovT
    Reg = (long*)(funct_ptr->arg);
    uint32_t reg_imm=*Reg;
    Reg = (long*)(funct_ptr->arg+6);
    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;
    uint32_t reg_r_address=(*Reg*4) + s_a0_reg_ofs;
    sprintf(buffer, "%s 0x%lx %s 0x%x", "20250119 func=get_mov_id type=MOVC reg_imm=", reg_imm," reg_r_address=", reg_r_address); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView
    //MOVC[]                 MOV R0 , #10
            // CMP R5, #0  比對al 跟 0
        uint32_t machine_cmp = generate_cmp(5, 0);
        //存 code
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(20);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


            // mov R8, #immediate =(input_value)
        uint32_t input_value_low_machine_code = mov_imm(reg_imm,  8);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( input_value_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


            // mov R7, data_reg  移動R0之位址的低16位元

        uint32_t R0_low_machine_code = mov_imm(reg_r_address,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( R0_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

            // movt R7, data_reg  移動R0之位址的高16位元
        uint32_t R0_high_machine_code = generate_movt_reg_imm(reg_r_address, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( R0_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

            // STR R8, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code = generate_str_reg_regoffset(8, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


}
/*******************************************************************/
static void get_mov_i_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg;
    int s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = *Reg;  /* get immediate data */
    fprintf(plc_run_cpp,"%d,",*Reg);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

}
/*******************************************************************/
static void get_xmov_i( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long *Reg;
    long s_a0_reg_ofs;
//    long s_a0_breg_ofs;

    s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;
//    s_a0_breg_ofs=(long)s_mlc_breg - (long)MLC_Data;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */
    a3=*code_ptr++;  /* machine word number */
    a4=*code_ptr++;  /* machine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    *temp_ptr = s_a0_reg_ofs;  /* get R0 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = *Reg;               /* get vaule */
    fprintf(plc_run_cpp,"%d);\n",*Reg);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a4>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }

}
/*******************************************************************/
static void get_xmov_r( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long *Reg;
    long s_a0_reg_ofs;
//    long s_a0_breg_ofs;

    s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;
//    s_a0_breg_ofs=(long)s_mlc_breg - (long)MLC_Data;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */
    a3=*code_ptr++;  /* machine word number */
    a4=*code_ptr++;  /* machine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    *temp_ptr = s_a0_reg_ofs;  /* get R0 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a4>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }

}

/************* 840808 **********************************************/
/* It's not used 2015/09/02*/
static void get_xmv_w( char *code_ptr, struct funct *funct_ptr )   // get machine - two argument
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4;
    long *Reg;
    long s_a0_reg_ofs;
    long s_a0_breg_ofs;

    s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;
//    s_a0_breg_ofs=(long)s_mlc_breg - (long)MLC_Data;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;
    a3=*code_ptr++;
    a4=*code_ptr++;

    while( a1>0 ){
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)funct_ptr->arg;
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get source */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while( a2>0 ){
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }

   /* ================== backup ===================
    Ryyy -> Dxxx   get destination
    temp_ptr = (short *)pc_counter;
    *temp_ptr = (funct_ptr->arg2*2) + s_a0_breg_ofs;
   =============================================  */

   // ================== 840808 ===================
   // Ryyy -> DRxxx
   // DRxxx where breg_pointer_offset=value of Rxxx
   // Taylor 840808
   // =============================================

    temp_ptr = (NUM_TYPE *)pc_counter;    // Taylor 840808
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;   // Taylor 840808
    pc_counter+=NUM_SIZE;                  /* get destination pointer */
    code_ptr+=NUM_SIZE;

    while( a3>0 ){          // Taylor 840808
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;             // Taylor 840808
    }                    // Taylor 840808
    temp_ptr = (NUM_TYPE *)pc_counter;        // Taylor 840808
    *temp_ptr = s_a0_breg_ofs;  /* get destination */
    pc_counter+=NUM_SIZE;   // Taylor 840808
    code_ptr+=NUM_SIZE;     // Taylor 840808

    while( a4>0 ){
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    //TED:將breg最後面的位元當作flag,但是程式中無使用到,所以毫無意義 2004/10/15 09:48上午
    //*temp_ptr = 127*2 + s_a0_breg_ofs;  /* get destination */
    *temp_ptr = (SIZE_REG-1) + s_a0_breg_ofs;  /* get destination */
    //
    // Jack 2003/03/31
//    *temp_ptr = SIZE_BREG*2 + s_a0_breg_ofs;
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;
}

/********** 840816 *************************************************/
/* It's not used. 2015/09/02 */
static void get_xmv_d( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
   NUM_TYPE *temp_ptr;
   int a1,a2,a3;
   long *Reg;
   long s_a0_reg_ofs;
   long s_a0_breg_ofs;

   // s_mlc_reg = MLC_Data + 0x2400
   s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;
//   s_a0_breg_ofs=(long)s_mlc_breg - (long)MLC_Data;

   a1=*code_ptr++;  /* machine word number */
   a2=*code_ptr++;  /* machine word number */
   a3=*code_ptr++;  /* machine word number */

   while(a1>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a1--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   Reg = (long*)(funct_ptr->arg);
   if(USR_REG_AREA_START <= *Reg)
   {
       *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
       isAccessingUsrRegArea=1;
   }
   *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get source */
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;

   while(a2>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a2--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
   Reg = (long*)(funct_ptr->arg+6);
   if(USR_REG_AREA_START <= *Reg)
   {
       *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
       isAccessingUsrRegArea=1;
   }
   *temp_ptr = (*Reg*4) + s_a0_breg_ofs;
   pc_counter+=NUM_SIZE;                  /* get destination pointer */
   code_ptr+=NUM_SIZE;

   while(a3>0)
   {
      *pc_counter++=*code_ptr++;  /* get machine code */
      a3--;
   }
   temp_ptr = (NUM_TYPE *)pc_counter;
    //TED:將breg最後面的位元當作flag,但是程式中無使用到,所以毫無意義 2004/10/15 09:48上午
    //*temp_ptr = 127*2 + s_a0_breg_ofs;  /* get destination */
    *temp_ptr = (SIZE_REG-1) + s_a0_breg_ofs;  /* get destination */
    //
//    Jack 2003/03/31
//       *temp_ptr = SIZE_BREG*2 + s_a0_breg_ofs;  /* get destination */
   pc_counter+=NUM_SIZE;
   code_ptr+=NUM_SIZE;
}


/*******************************************************************/
static void get_sch( char *code_ptr, struct funct *funct_ptr )      /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4,a5,a6,a7;
    int s_a0_reg_ofs;
//    int s_a0_breg_ofs;
    long *Reg;

    // s_mlc_reg = MLC_Data + 0x2400
    s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;
//    s_a0_breg_ofs=(long)s_mlc_breg - (long)MLC_Data;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */
    a3=*code_ptr++;  /* machine word number */
    a4=*code_ptr++;  /* machine word number */
    a5=*code_ptr++;  /* machine word number */
    a6=*code_ptr++;  /* machine word number */
    a7=*code_ptr++;  /* machine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    *temp_ptr = s_a0_reg_ofs;  /* get R0 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get R004:刀具表起點 */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg+1)*4 + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get R002:刀具表容量 */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg+1)*4 + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a4>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get R001:欲收尋刀具編號 */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a5>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a5--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get R002:刀具表容量 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a6>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a6--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get R003:計算結果 */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a7>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a7--;
    }
}
/*******************************************************************/
static void get_rot( char *code_ptr, struct funct *funct_ptr )      /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    int a1,a2,a3,a4,a5,a6,a7,a8,a9,a10;
    int s_a0_reg_ofs;
    long *Reg;

    s_a0_reg_ofs=(long)s_mlc_reg - (long)MLC_Data;

    a1=*code_ptr++;  /* machine word number */
    a2=*code_ptr++;  /* machine word number */
    a3=*code_ptr++;  /* machine word number */
    a4=*code_ptr++;  /* machine word number */
    a5=*code_ptr++;  /* machine word number */
    a6=*code_ptr++;  /* machine word number */
    a7=*code_ptr++;  /* machine word number */
    a8=*code_ptr++;  /* machine word number */
    a9=*code_ptr++;  /* machine word number */
    a10=*code_ptr++;  /* machine word number */

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get source */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get destination + 1*/
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg+1)*4 + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a3--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get source + 1 */
    fprintf(plc_run_cpp,"*(long*)(Data+%d),",(*Reg+1)*4 + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a4>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a4--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get destination + 1 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a5>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a5--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
    fprintf(plc_run_cpp,"*(long*)(Data+%d));\n",(*Reg*4) + s_a0_reg_ofs);
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a6>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a6--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get destination + 1 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a7>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a7--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a8>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a8--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get dest. + 1 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a9>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a9--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg+1)*4 + s_a0_reg_ofs;  /* get dest. + 1 */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a10>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a10--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    *temp_ptr = (*Reg)*4 + s_a0_reg_ofs;  /* get destination */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;
}
/*********************************************************************/
static void get_ucnt( char *code_ptr, struct funct *funct_ptr )   /* get UCNT machine code */
{
    // 2025 armv7
    char buffer[256]; // 分配足夠的debug緩存

    NUM_TYPE *temp_ptr;
    char index,a[10];
    long int j,k;
    long *Reg,*Cnt;
    long s_a0_reg_ofs;
    long s_a0_cnt_ofs ;
    long s_a0_cnt_s_ofs ;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_cnt;           /* MLC_Data + 0x1000 */
    s_a0_cnt_ofs=j-k;

    j=(long)s_mlc_cnt_stus;      /* MLC_Data + 0x1600 */
    s_a0_cnt_s_ofs=j-k;

    for(index=0;index<=9;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C6) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Cnt = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    UCNT(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),Data+%d);\n",
                                    bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Cnt*12)+s_a0_cnt_ofs,(*Cnt*2)+s_a0_cnt_s_ofs,
                                (*Cnt*12)+s_a0_cnt_ofs+4,(*Cnt*2)+s_a0_cnt_s_ofs+1);


                    Reg = (long*)(funct_ptr->arg+6);
                    if(USR_REG_AREA_START <= *Reg)
                    {
                        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                        isAccessingUsrRegArea=1;
                    }


                    Cnt = (long*)(funct_ptr->arg);



        sprintf(buffer,"    ucnt  bvalue = %d, valye  = %d,  reg = %lx ,  ;\n",
            bValue,Value,(*Reg*4) + s_a0_reg_ofs);
        OutputDebugStringA(buffer); // 輸出到 DebugView
        sprintf(buffer,"    ucnt (*Cnt*12)+s_a0_cnt_ofs = %lx ,  ;\n",
            (*Cnt*12)+s_a0_cnt_ofs);
        OutputDebugStringA(buffer); // 輸出到 DebugView  
        sprintf(buffer,"    ucnt (*Cnt*2)+s_a0_cnt_s_ofs = %lx ,  ;\n",
            (*Cnt*2)+s_a0_cnt_s_ofs);
        OutputDebugStringA(buffer); // 輸出到 DebugView          
        sprintf(buffer,"    ucnt (*Cnt*12)+s_a0_cnt_ofs+4= %lx ,  ;\n",
            (*Cnt*12)+s_a0_cnt_ofs+4);
        OutputDebugStringA(buffer); // 輸出到 DebugView            
        sprintf(buffer,"    ucnt (*Cnt*2)+s_a0_cnt_s_ofs+1 = %lx ,  ;\n",
            (*Cnt*2)+s_a0_cnt_s_ofs+1);
        OutputDebugStringA(buffer); // 輸出到 DebugView    

        Reg = (long*)(funct_ptr->arg+6);
        if(USR_REG_AREA_START <= *Reg)
        {
            *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
            isAccessingUsrRegArea=1;
        }
        
        
           // mov  ecx,[ebx+registr]  
           
           uint32_t setup_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
           uint32_t setup_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs, 7);
           uint32_t str_ecx_source_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        
        
           if (bValue == 0){
        
            uint32_t eax_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
            uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
            uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            Cnt = (long*)(funct_ptr->arg);
            //  dd_m mov  [ebx+dd_m],ecx   
        
        
        
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
            
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_ecx_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        }
           
        
            // mov  [ebx+dd_n],al 
        
            uint32_t dd_n_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
            uint32_t dd_n_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
            uint32_t str_al_source_index_machine_code = generate_strb_reg_regoffset(5, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
               // cmp     al,0
               uint32_t machine_cmp = generate_cmp(5, 0);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                   *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                   pc_counter++;                            // 指向下一個位元組
               }
           
        
               // je      LUCNT1_1
               uint32_t machine_beq = encode_beq(23*4);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                   *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                   pc_counter++;                            // 指向下一個位元組
               }
           
               // cmp  BYTE PTR [ebx+dd_n_1],0 
               // load dd_n_1
        
               uint32_t one_shot_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs+1,  7);
               // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                       *pc_counter = (one_shot_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                       pc_counter++;                            // 指向下一個位元組
               }
           
           
           
               uint32_t one_shot_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1, 7);
               // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                       *pc_counter = (one_shot_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                       pc_counter++;                            // 指向下一個位元組
               }
           
           
               uint32_t ldr_dd_n_1_source_index_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0);
           
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                       *pc_counter = (ldr_dd_n_1_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                       pc_counter++;                            // 指向下一個位元組
               }
        
               // cmp     dd_n_1,0
               uint32_t dd_n_1_0_machine_cmp = generate_cmp(7, 0);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                   *pc_counter = (dd_n_1_0_machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                   pc_counter++;                            // 指向下一個位元組
               }
                  
               // jne  LUCNT1_2 
        
               uint32_t branch_LUCNT1_2_machine_code = generate_branch(23 * 4, 0x1) ;  // 0x1 for BNE
               // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                   for (size_t i = 0; i < sizeof(uint32_t); i++) {
                           *pc_counter = (branch_LUCNT1_2_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                           pc_counter++;                            // 指向下一個位元組
                   }  
        
        
        
                   //  mov  ecx,[ebx+dd_m_1]     
        
                uint32_t dd_m_1_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4,  7);
                // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (dd_m_1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                
            
                uint32_t dd_m_1_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4, 7);
                // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (dd_m_1_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
                uint32_t ldr_dd_m_1_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_dd_m_1_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
                  // cmp  ecx,[ebx+dd_m]  
        
        
        
                  for (size_t i = 0; i < sizeof(uint32_t); i++) {
                          *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                          pc_counter++;                            // 指向下一個位元組
                  }
              
              
              
        
                  for (size_t i = 0; i < sizeof(uint32_t); i++) {
                          *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                          pc_counter++;                            // 指向下一個位元組
                  }
              
              
                  uint32_t ldr_dd_m_index_machine_code = generate_ldr_reg_regoffset(7, 6, 7, 0);
      
                  for (size_t i = 0; i < sizeof(uint32_t); i++) {
                          *pc_counter = (ldr_dd_m_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                          pc_counter++;                            // 指向下一個位元組
                  }
        
               // cmp     dd_m_1
               uint32_t dd_m_1_machine_cmp = generate_cmp_reg(10, 7);
               for (size_t i = 0; i < sizeof(uint32_t); i++) {
                   *pc_counter = (dd_m_1_machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                   pc_counter++;                            // 指向下一個位元組
               }
                  
               // jge  LUCNT1_2 
        
               uint32_t branch_ge_LUCNT1_2_machine_code = generate_branch(15 * 4, 0xa) ;  // 0xa for BGE
               // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                   for (size_t i = 0; i < sizeof(uint32_t); i++) {
                           *pc_counter = (branch_ge_LUCNT1_2_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                           pc_counter++;                            // 指向下一個位元組
                   }  
        
        
                uint32_t inc_ecx_machine_code = generate_add_imm(10, 10, 1) ;
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (inc_ecx_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }  
            
                   //  mov  [ebx+dd_m_1],ecx        
        
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (dd_m_1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
                
            
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (dd_m_1_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
                uint32_t str_dd_m_1_source_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_dd_m_1_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
                     // cmp  ecx,[ebx+dd_m] 
                     
        
                     for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
            
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
        
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (ldr_dd_m_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
             // cmp     dd_m_1
        
             for (size_t i = 0; i < sizeof(uint32_t); i++) {
                 *pc_counter = (dd_m_1_machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                 pc_counter++;                            // 指向下一個位元組
             }
                
             // je   LUCNT1_2 
        
             uint32_t branch_eq_LUCNT1_2_machine_code = generate_branch(6 * 4, 0x0) ;  // 0x0 for BEQ
             // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (branch_eq_LUCNT1_2_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }  
        
        
                // LUCNT1_1:
        
        
                // xor  al,al  
        
                uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( al_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
                }
        
                // mov  [ebx+dd_n_1],al        
        
                
        
                // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (one_shot_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
            
        
                // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (one_shot_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
            
            
        
            
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                        *pc_counter = (str_al_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                        pc_counter++;                            // 指向下一個位元組
                }
        
                // jump jump_LUCNTend
                uint32_t jump_LUCNTend_machine_code = generate_branch(6 * 4, 0xe) ;  // 0xe
                // write_code_to_memory( str_destination_index_machine_code, &pc_counter);
                    for (size_t i = 0; i < sizeof(uint32_t); i++) {
                            *pc_counter = (jump_LUCNTend_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                            pc_counter++;                            // 指向下一個位元組
                    }  
        
            // LUCNT1_2: 
        
            // mov  al,0ffh  
            uint32_t al_0xff_low_machine_code = mov_imm(0xff,  5);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_0xff_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
            uint32_t al_0xff_high_machine_code = generate_movt_reg_imm(0x0, 5);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_0xff_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            };
        
        
                // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (one_shot_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (one_shot_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
            //LUCNTend:  

               
    

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'C',*arg1,*arg2,1,0,0);
}
/*********************************************************************/
static void get_dcnt( char *code_ptr, struct funct *funct_ptr )   /* get DCNT machine code */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[10];
    long int j,k;
    long *Reg,*Cnt;
    long s_a0_reg_ofs;
    long s_a0_cnt_ofs ;
    long s_a0_cnt_s_ofs ;
    long *arg1,*arg2;

    char buffer[256]; // 分配足夠的debug緩存

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_cnt;           /* MLC_Data + 0x1000 */
    s_a0_cnt_ofs=j-k;

    j=(long)s_mlc_cnt_stus;      /* MLC_Data + 0x1600 */
    s_a0_cnt_s_ofs=j-k;

    for(index=0;index<=9;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C7) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Cnt = (long*)(funct_ptr->arg);

    


    sprintf(buffer,"    ucnt  bvalue = %d, valye  = %d,  reg = %lx ,  ;\n",
        bValue,Value,(*Reg*4) + s_a0_reg_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView
    sprintf(buffer,"    ucnt (*Cnt*12)+s_a0_cnt_ofs = %lx ,  ;\n",
        (*Cnt*12)+s_a0_cnt_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView  
    sprintf(buffer,"    ucnt (*Cnt*2)+s_a0_cnt_s_ofs = %lx ,  ;\n",
        (*Cnt*2)+s_a0_cnt_s_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView          
    sprintf(buffer,"    ucnt (*Cnt*12)+s_a0_cnt_ofs+4= %lx ,  ;\n",
        (*Cnt*12)+s_a0_cnt_ofs+4);
    OutputDebugStringA(buffer); // 輸出到 DebugView            
    sprintf(buffer,"    ucnt (*Cnt*2)+s_a0_cnt_s_ofs+1 = %lx ,  ;\n",
        (*Cnt*2)+s_a0_cnt_s_ofs+1);
    OutputDebugStringA(buffer); // 輸出到 DebugView    

    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    
    
        // mov  ecx,[ebx+registr]  
        
        uint32_t setup_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
        uint32_t setup_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs, 7);
        uint32_t str_ecx_source_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);


        if (bValue == 0){

            uint32_t eax_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
            uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
            uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
            Cnt = (long*)(funct_ptr->arg);
            //  dd_m mov  [ebx+dd_m],ecx   
        
        
        
            // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
        
        
            // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        
            
        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_ecx_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
        
        }


    

uint32_t dd_n_index_1 = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs,  7);
write_to_array(dd_n_index_1);


uint32_t dd_n_index_2 = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs, 7);
write_to_array( dd_n_index_2);

// LDRB r4, [r6, r7]  // mov cl,[ebx+dd_n]
uint32_t ldrb_r4 = generate_ldrb_reg_regoffset(4, 6, 7, 0);
write_to_array(ldrb_r4);

// STRB r5, [r6, r7]  // mov [ebx+dd_n],al
uint32_t strb_r5 = generate_strb_reg_regoffset(5, 6, 7, 0);
write_to_array(strb_r5);

// CMP r5, #0  // cmp al,0
uint32_t cmp_r5_0 = generate_cmp(5, 0);
write_to_array(cmp_r5_0);

// BEQ LDCNT1_1
uint32_t beq_LDCNT1_1 = generate_branch(24 * 4 , 0x0);
write_to_array(beq_LDCNT1_1);

// dd_n_1

uint32_t dd_n_1_index_1 = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs +1,  7);
write_to_array(dd_n_1_index_1);


uint32_t dd_n_1_index_2 = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1, 7);
write_to_array( dd_n_1_index_2);

// LDRB r5, [r6, r7]  // cmp BYTE PTR [ebx+dd_n_1],0
uint32_t ldrb_r5 = generate_ldrb_reg_regoffset(5, 6, 7, 0);
write_to_array(ldrb_r5);

uint32_t cmp_r5_0_again = generate_cmp(5, 0);
write_to_array(cmp_r5_0_again);

// BNE LDCNT1_2
uint32_t bne_LDCNT1_2 = generate_branch(24 * 4, 0x1);
write_to_array(bne_LDCNT1_2);

// MVN r4, r4  // not cl
uint32_t mvn_r4 = generate_mvn_reg(4, 4);
write_to_array(mvn_r4);

// AND r5, r4, r5  // and al, cl
uint32_t and_r5_r4 = generate_and_reg(5, 5, 4, 1);
write_to_array(and_r5_r4);


// BEQ LDCNT1_0
uint32_t beq_LDCNT1_0 = generate_branch( 5 * 4, 0x0);
write_to_array(beq_LDCNT1_0);



// dd_m

uint32_t dd_m_index_1 = mov_imm((*Cnt*12)+s_a0_cnt_ofs  ,  7);
write_to_array(dd_m_index_1);


uint32_t dd_m_index_2 = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs  , 7);
write_to_array( dd_m_index_2);






// LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m]
uint32_t ldr_r10_m = generate_ldr_reg_regoffset(10, 6, 7, 0);
write_to_array(ldr_r10_m);

// JMP LDCNT_ON
uint32_t b_LDCNT_ON = generate_branch(  6 * 4, 0xe);
write_to_array(b_LDCNT_ON);


/************************************************************* */
// LDCNT1_0:

// dd_m

uint32_t dd_m_1_index_1 = mov_imm( (*Cnt*12)+s_a0_cnt_ofs+4 ,  7);
write_to_array(dd_m_1_index_1);


uint32_t dd_m_1_index_2 = generate_movt_reg_imm( (*Cnt*12)+s_a0_cnt_ofs+4, 7);
write_to_array( dd_m_1_index_2);




// LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m_1]
uint32_t ldr_r10_m1 = generate_ldr_reg_regoffset(10, 6, 7, 0);
write_to_array(ldr_r10_m1);

// CMP r10, #0
uint32_t cmp_r10_0 = generate_cmp(10, 0);
write_to_array(cmp_r10_0);

// BEQ LDCNT1_2
uint32_t beq_LDCNT1_2 = generate_branch(12 * 4, 0x0);
write_to_array(beq_LDCNT1_2);


/************************************************************* */

// LDCNT_ON: SUB r10, r10, #1  // dec ecx
uint32_t sub_r10_1 = generate_sub_imm(10, 10, 1,0);
write_to_array(sub_r10_1);


uint32_t dd_m_1_index_1_1 = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4 ,  7);
write_to_array(dd_m_1_index_1_1);


uint32_t dd_m_1_index_2_2 = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4, 7);
write_to_array( dd_m_1_index_2_2);




// STR r10, [r6, r7]  // mov [ebx+dd_m_1],ecx
uint32_t str_r10_m1 = generate_str_reg_regoffset(10, 6, 7, 0);
write_to_array(str_r10_m1);

// CMP r10, #0
uint32_t cmp_r10_0_again = generate_cmp(10, 0);
write_to_array(cmp_r10_0_again);


// BEQ LDCNT1_2
uint32_t beq_LDCNT1_2_again = generate_branch( 6 * 4, 0x0);
write_to_array(beq_LDCNT1_2_again);


/************************************************************* */
// LDCNT1_1: MOV r5, #0  // xor al,al
     // xor  al,al  

uint32_t al_0_machine_code =  generate_eor_reg(5,5,5,0);
write_to_array(al_0_machine_code);



uint32_t dd_n_1_index_1_1 = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs+1,  7);
write_to_array(dd_n_1_index_1_1);


uint32_t dd_n_1_index_2_1 = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1, 7);
write_to_array( dd_n_1_index_2_1);



// STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
uint32_t strb_r5_dd_n1 = generate_strb_reg_regoffset(5, 6, 7, 0);
write_to_array(strb_r5_dd_n1);

// JMP LDCNTend
uint32_t b_LDCNTend = generate_branch(6 * 4, 0xe);
write_to_array(b_LDCNTend);

/************************************************************* */

// LDCNT1_2: MOV r5, #0xFF  // mov al,0xff
uint32_t mov_r5_0xFF = mov_imm(0xFF, 5);
write_to_array(mov_r5_0xFF);

uint32_t mov_r5_0xFF_high = generate_movt_reg_imm(0,5);
write_to_array( mov_r5_0xFF_high);

uint32_t dd_n_1_index_1_2 = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs+1,  7);
write_to_array(dd_n_1_index_1_2);


uint32_t dd_n_1_index_2_2 = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1, 7);
write_to_array( dd_n_1_index_2_2);




// STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
uint32_t strb_r5_dd_n1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
write_to_array(strb_r5_dd_n1_again);

// LDCNTend:








    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'C',*arg1,*arg2,2,0,0);
}

/*********************************************************************/
static void get_r_ucnt( char *code_ptr, struct funct *funct_ptr )   /* get R_UCNT machine code */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[9];
    long int j,k;
    long *Reg,*Cnt;
    long s_a0_reg_ofs;
    long s_a0_cnt_ofs ;
    long s_a0_cnt_s_ofs ;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_cnt;           /* MLC_Data + 0x1000 */
    s_a0_cnt_ofs=j-k;

    j=(long)s_mlc_cnt_stus;      /* MLC_Data + 0x1600 */
    s_a0_cnt_s_ofs=j-k;

    for(index=0;index<=8;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0E0) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }



    Reg = (long*)(funct_ptr->arg+6);
    Cnt = (long*)(funct_ptr->arg);
    
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    
    
       // mov  ecx,[ebx+registr]  
       
       uint32_t setup_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
       uint32_t setup_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs, 7);
       uint32_t str_ecx_source_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    
    
       if (bValue == 0){
    
        uint32_t eax_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
        uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
        uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
        Cnt = (long*)(funct_ptr->arg);
        //  dd_m mov  [ebx+dd_m],ecx   
    
    
    
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
    
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
        
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_ecx_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    }
       
    
    uint32_t dd_n_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs ,  7);
    write_to_array( dd_n_low_machine_code);
    
    
    
    uint32_t dd_n_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs , 7);
    write_to_array(dd_n_high_machine_code);
    
    
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n],al
    uint32_t strb_r5 = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5);
    
    
    
    uint32_t dd_m_1_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4 ,  7);
    write_to_array( dd_m_1_low_machine_code);
    
    
    
    uint32_t dd_m_1_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4 , 7);
    write_to_array(dd_m_1_high_machine_code);
    
    
    
    // LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m_1]
    uint32_t ldr_r10_m1 = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_r10_m1);
    
    // CMP r5, #0  // cmp al,0
    uint32_t cmp_r5_0 = generate_cmp(5, 0);
    write_to_array(cmp_r5_0);
    
    // BEQ UCNT_low
    uint32_t beq_UCNT_low = generate_branch(19 * 4, 0x0);
    write_to_array(beq_UCNT_low);
    
    
    
    uint32_t dd_m_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs ,  7);
    write_to_array( dd_m_low_machine_code);
    
    
    
    uint32_t dd_m_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs , 7);
    write_to_array(dd_m_high_machine_code);
    
    
    
    //  // mov ecx,[ebx+dd_m]
    uint32_t ldr_r7_dd_m = generate_ldr_reg_regoffset(7, 6, 7, 0);
    write_to_array(ldr_r7_dd_m);
    
    
    
    
    // CMP r10, r7  // cmp ecx,[ebx+dd_m]
    uint32_t cmp_r10_r9 = generate_cmp_reg(10, 7);
    write_to_array(cmp_r10_r9);
    
    // BLT UCNTon
    uint32_t blt_UCNTon = generate_branch(2* 4 , 0xb);
    write_to_array(blt_UCNTon);
    
    // MOV r10, #0  // xor ecx,ecx
    uint32_t mov_r10_0 = generate_eor_reg(10, 10,10,0);
    write_to_array(mov_r10_0);
    
    
    /*************************************************************************** */
    // UCNTon: ADD r10, r10, #1  // inc ecx
    uint32_t add_r10_1 = generate_add_imm(10, 10, 1);
    write_to_array(add_r10_1);
    
    
    
    write_to_array( dd_m_low_machine_code);
    
    
    
    
    write_to_array(dd_m_high_machine_code);
    
    
    
    //  mov ecx,[ebx+dd_m]
    write_to_array(ldr_r7_dd_m);
    
    
    
    // CMP r10, r7  // cmp ecx,[ebx+dd_m]
    uint32_t cmp_r10_r7_again = generate_cmp_reg(10, 7);
    write_to_array(cmp_r10_r7_again);
    
    // BNE UCNT_low
    uint32_t bne_UCNT_low = generate_branch(7 * 4 , 0x1);
    write_to_array(bne_UCNT_low);
    
    // MOV r5, #0xFF  // mov al,0xff
    uint32_t mov_r5_0xFF = mov_imm(0xFF, 5);
    write_to_array(mov_r5_0xFF);
    uint32_t movt_r5_0x0 = generate_movt_reg_imm(0,5);
    write_to_array(movt_r5_0x0);
    
    uint32_t dd_n_1_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs+1 ,  7);
    write_to_array( dd_n_1_low_machine_code);
    
    
    
    uint32_t dd_n_1_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1 , 7);
    write_to_array(dd_n_1_high_machine_code);
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
    uint32_t strb_r5_dd_n1 = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5_dd_n1);
    
    // B UCNT_low1
    uint32_t b_UCNT_low1 = generate_branch(5 * 4, 0xe);
    write_to_array(b_UCNT_low1);
    
    
    /************************************************* */
    
    // UCNT_low: MOV r5, #0  // xor al,al
    uint32_t mov_r5_0 = generate_eor_reg(5, 5,5,0);
    write_to_array(mov_r5_0);
    
    
    write_to_array( dd_n_1_low_machine_code);
    write_to_array(dd_n_1_high_machine_code);
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
    uint32_t strb_r5_dd_n1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5_dd_n1_again);
    
    /*******************************************************/
    
    
    write_to_array( dd_m_1_low_machine_code);
    
    
    write_to_array(dd_m_1_high_machine_code);
    
    // UCNT_low1: STR r10, [r6, r7]  // mov [ebx+dd_m_1],ecx
    uint32_t str_r10_m1 = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_r10_m1);
    


    /********************************************************* */

 
 
   
   
 
   
   

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'C',*arg1,*arg2,1,0,0);
}

/*********************************************************************/
static void get_r_dcnt( char *code_ptr, struct funct *funct_ptr )   /* get R_DCNT machine code */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[9];
    long int j,k;
    long *Reg,*Cnt;
    long s_a0_reg_ofs;
    long s_a0_cnt_ofs ;
    long s_a0_cnt_s_ofs ;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_cnt;           /* MLC_Data + 0x1000 */
    s_a0_cnt_ofs=j-k;

    j=(long)s_mlc_cnt_stus;      /* MLC_Data + 0x1600 */
    s_a0_cnt_s_ofs=j-k;




    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0E1) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Cnt = (long*)(funct_ptr->arg);

    char buffer[256]; // 分配足夠的debug緩存
    sprintf(buffer,"    rdcnt  bvalue = %d, valye  = %d,  reg = %lx ,  ;\n",
        bValue,Value,(*Reg*4) + s_a0_reg_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView
    sprintf(buffer,"    rdcnt (*Cnt*12)+s_a0_cnt_ofs = %lx ,  ;\n",
        (*Cnt*12)+s_a0_cnt_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView  
    sprintf(buffer,"    rdcnt (*Cnt*2)+s_a0_cnt_s_ofs = %lx ,  ;\n",
        (*Cnt*2)+s_a0_cnt_s_ofs);
    OutputDebugStringA(buffer); // 輸出到 DebugView          
    sprintf(buffer,"    rdcnt (*Cnt*12)+s_a0_cnt_ofs+4= %lx ,  ;\n",
        (*Cnt*12)+s_a0_cnt_ofs+4);
    OutputDebugStringA(buffer); // 輸出到 DebugView            
    sprintf(buffer,"    rdcnt (*Cnt*2)+s_a0_cnt_s_ofs+1 = %lx ,  ;\n",
        (*Cnt*2)+s_a0_cnt_s_ofs+1);
    OutputDebugStringA(buffer); // 輸出到 DebugView    


    
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    
    
       // mov  ecx,[ebx+registr]  
       
       uint32_t setup_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
       uint32_t setup_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs, 7);
       uint32_t str_ecx_source_index_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    
    
       if (bValue == 0){
    
        uint32_t eax_index_low_machine_code = mov_imm((*Reg*4) + s_a0_reg_ofs,  7);
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_index_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
        uint32_t eax_index_high_machine_code = generate_movt_reg_imm((*Reg*4) + s_a0_reg_ofs, 7);
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_index_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
        uint32_t ldr_eax_source_index_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_eax_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
        Cnt = (long*)(funct_ptr->arg);
        //  dd_m mov  [ebx+dd_m],ecx   
    
    
    
        // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (setup_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
    
    
        // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (setup_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
        
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_ecx_source_index_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    }
    


    uint32_t dd_n_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs ,  7);
    write_to_array( dd_n_low_machine_code);
    
    
    
    uint32_t dd_n_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs , 7);
    write_to_array(dd_n_high_machine_code);
    
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n],al
    uint32_t strb_r5 = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5);
    
    uint32_t dd_m_1_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4,  7);
    write_to_array( dd_m_1_low_machine_code);
    
    
    
    uint32_t dd_m_1_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4 , 7);
    write_to_array(dd_m_1_high_machine_code);
    
    
    
    // LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m_1]
    uint32_t ldr_r10_m1 = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_r10_m1);
    
    // CMP r5, #0  // cmp al,0
    uint32_t cmp_r5_0 = generate_cmp(5, 0);
    write_to_array(cmp_r5_0);
    
    // BEQ DCNT_low
    uint32_t beq_DCNT_low = generate_branch(19 * 4, 0x0);
    write_to_array(beq_DCNT_low);
    
    // CMP r10, #0  // cmp ecx,0
    uint32_t cmp_r10_0 = generate_cmp(10, 0);
    write_to_array(cmp_r10_0);
    
    // BEQ DCNTon_1
    uint32_t beq_DCNTon_1 = generate_branch( 6 * 4, 0x0);
    write_to_array(beq_DCNTon_1);
    
    
    uint32_t dd_m_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
    write_to_array( dd_m_low_machine_code);
    
    
    uint32_t dd_m_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs , 7);
    write_to_array(dd_m_high_machine_code);
    
    uint32_t ldr_r7_m = generate_ldr_reg_regoffset(7, 6, 7, 0);
    write_to_array(ldr_r7_m);
    
    // CMP r10, r7  // cmp ecx,[ebx+dd_m]
    uint32_t cmp_r10_r7 = generate_cmp_reg(10, 7);
    write_to_array(cmp_r10_r7);
    
    // BLE DCNTon
    uint32_t ble_DCNTon = generate_branch(4 * 4, 0xd);
    write_to_array(ble_DCNTon);
    
    
    /*                  DCNTon_1                */
    
    // DCNTon_1: LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m]
    
    write_to_array(dd_m_low_machine_code);
    
    
    write_to_array(dd_m_high_machine_code);
    
    
    uint32_t ldr_r10_m = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_r10_m);
    
    /*                  DCNTon                */
    
    // DCNTon: SUB r10, r10, #1  // dec ecx
    uint32_t sub_r10_1 = generate_sub_imm(10, 10, 1,1);
    write_to_array(sub_r10_1);
    
    // BNE DCNT_low
    uint32_t bne_DCNT_low = generate_branch(7 * 4, 0x1);
    write_to_array(bne_DCNT_low);
    
    // MOV r5, #0xFF  // mov al,0xff
    uint32_t mov_r5_0xFF = mov_imm(0xFF, 5);
    write_to_array(mov_r5_0xFF);
    
    uint32_t mov_r5_0xFF_high = generate_movt_reg_imm(0,5);
    write_to_array( mov_r5_0xFF_high);
    
    uint32_t dd_n_1_low_machine_code = mov_imm((*Cnt*2)+s_a0_cnt_s_ofs+1 , 7);
    write_to_array(dd_n_1_low_machine_code);
    
    uint32_t dd_n_1_high_machine_code = generate_movt_reg_imm((*Cnt*2)+s_a0_cnt_s_ofs+1 , 7);
    write_to_array(dd_n_1_high_machine_code);
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
    uint32_t strb_r5_dd_n1 = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5_dd_n1);
    
    // B DCNT_low1
    uint32_t b_DCNT_low1 = generate_branch( 6 * 4 , 0xe);
    write_to_array(b_DCNT_low1);
    
    /*                  DCNT_low               */
    
    // DCNT_low: MOV r5, #0  // xor al,al
    uint32_t mov_r5_0 = mov_imm(0, 5);
    write_to_array(mov_r5_0);
    uint32_t mov_r5_0_high = generate_movt_reg_imm(0,5);
    write_to_array( mov_r5_0_high);
    
    write_to_array(dd_n_1_low_machine_code);
    write_to_array(dd_n_1_high_machine_code);
    
    
    // STRB r5, [r6, r7]  // mov [ebx+dd_n_1],al
    uint32_t strb_r5_dd_n1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
    write_to_array(strb_r5_dd_n1_again);
    
    
    /*                  DCNT_low1               */
    
    write_to_array(dd_m_1_low_machine_code);
    write_to_array(dd_m_1_high_machine_code);
    
    
    // DCNT_low1: STR r10, [r6, r7]  // mov [ebx+dd_m_1],ecx
    uint32_t str_r10_m1 = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_r10_m1);
    
    
    
    
    /*                  DCNT_end                */
        
    
    /*                  DCNT_end                */
    
   


/*                                  test                                                  */
{
    uint32_t dd_m_1_low_machine_code1 = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4,  7);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (dd_m_1_low_machine_code1 >> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }
   
   
   
   uint32_t dd_m_1_high_machine_code1 = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4, 7);
   for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (dd_m_1_high_machine_code1>> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }
   
   uint32_t ldr_dd_m_1_source_index_machine_code1 = generate_ldr_reg_regoffset(10, 6, 7, 0);
   for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (ldr_dd_m_1_source_index_machine_code1>> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }
   
   
   
   
   uint32_t test_low_machine_code = mov_imm(0x10004,  7);
   // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
   for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (test_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }
   
   
   
   uint32_t test_high_machine_code = generate_movt_reg_imm(0x10004, 7);
   // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
   for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (test_high_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }
   
   
   
   uint32_t str_test_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
   
   for (size_t i = 0; i < sizeof(uint32_t); i++) {
       *pc_counter = (str_test_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
       pc_counter++;                            // 指向下一個位元組
   }


   /****************************************************** */


       uint32_t dd_m_low_machine_code_1 = mov_imm((*Cnt*12)+s_a0_cnt_ofs,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (dd_m_low_machine_code_1 >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
    
    
    
    uint32_t dd_m_high_machine_code_1 = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (dd_m_high_machine_code_1>> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
    
    uint32_t ldr_dd_m_source_index_machine_code_1 = generate_ldr_reg_regoffset(10, 6, 7, 0);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (ldr_dd_m_source_index_machine_code_1>> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
    
    
    
    
    // mov  al,0ffh  
    uint32_t test_low_machine_code2 = mov_imm(0x10008,  7);
    // write_code_to_memory(destination_index_low_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (test_low_machine_code2 >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
    
    
    
    uint32_t test_high_machine_code2 = generate_movt_reg_imm(0x10008, 7);
    // write_code_to_memory(destination_index_high_machine_code, &pc_counter);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (test_high_machine_code2>> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
    
    
    
    uint32_t str_test_machine_code2 = generate_str_reg_regoffset(10, 6, 7, 0);
    
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (str_test_machine_code2>> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
    }
}
/*                                  test                                                  */
 
   
   
arg1=(long*)(funct_ptr->arg);
arg2=(long*)(funct_ptr->arg+6);
if (funct_ptr->data.code == 0x0E1)
    parpt=(struct regpar *)par_tab(parpt,'C',*arg1,*arg2,2,0,0);
else
    parpt=(struct regpar *)par_tab(parpt,'C',*arg1,0,2,0,0); //取Reg時,要把現在值設定為0



}

/*******************************************************************/
static void get_reset( char *code_ptr, struct funct *funct_ptr )   /* get RESET machine code */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char a1,a2,a3,a4;    //a5,a6;
    long int d,c;
    long *Reg,*Cnt;
    long s_a0_cnt_ofs ;
    long s_a0_cnt_s_ofs ;

    d=(long)s_mlc_cnt_stus;
    c=(long)MLC_Data;
    s_a0_cnt_s_ofs=d-c;

    d=(long)s_mlc_cnt;
    s_a0_cnt_ofs=d-c;

    a1=*code_ptr++;
    a2=*code_ptr++;
    a3=*code_ptr++;
    a4=*code_ptr++;

    Reg = (long*)(funct_ptr->arg);
    Cnt = (long*)(funct_ptr->arg);
// CMP r5, #0  // cmp al,0
uint32_t cmp_r5_0 = generate_cmp(5, 0);
write_to_array(cmp_r5_0);

// BEQ RESETend
uint32_t beq_RESETend = generate_branch(17*4, 0x0);
write_to_array(beq_RESETend);

// MOV r4, #0  // mov cl,0
uint32_t mov_r4_0 = mov_imm(0, 4);
write_to_array(mov_r4_0);


Cnt = (long*)(funct_ptr->arg);

uint32_t dd_n_1_low_machine_code = mov_imm((*Reg*2)+s_a0_cnt_s_ofs+1  ,  7);
write_to_array( dd_n_1_low_machine_code);



uint32_t dd_n_1_high_machine_code = generate_movt_reg_imm((*Reg*2)+s_a0_cnt_s_ofs+1  , 7);
write_to_array(dd_n_1_high_machine_code);



// STRB r4, [r6, r7]  // mov [ebx+dd_n_1],cl
uint32_t strb_r4 = generate_strb_reg_regoffset(4, 6, 7, 0);
write_to_array(strb_r4);

// MOV r10, #0  // xor ecx,ecx
uint32_t mov_r10_0 = generate_eor_reg(10, 10,10,0);
write_to_array(mov_r10_0);


Cnt = (long*)(funct_ptr->arg);

uint32_t dd_m_2_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs+8  ,  7);
write_to_array( dd_m_2_low_machine_code);



uint32_t dd_m_2_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+8  , 7);
write_to_array(dd_m_2_high_machine_code);



// LDR r9, [r6, r7]  // cmp [ebx+dd_m_2],1
uint32_t ldr_r9 = generate_ldr_reg_regoffset(9, 6, 7, 0);
write_to_array(ldr_r9);
uint32_t cmp_r9_1 = generate_cmp(9, 1);
write_to_array(cmp_r9_1);

// BEQ RESETon
uint32_t beq_RESETon = generate_branch(4 * 4, 0x0);
write_to_array(beq_RESETon);


Cnt = (long*)(funct_ptr->arg);

uint32_t dd_m_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs  ,  7);
write_to_array( dd_m_low_machine_code);



uint32_t dd_m_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs  , 7);
write_to_array( dd_m_high_machine_code);



// LDR r10, [r6, r7]  // mov ecx,[ebx+dd_m]
uint32_t ldr_r10 = generate_ldr_reg_regoffset(10, 6, 7, 0);
write_to_array(ldr_r10);



/*************************************************** */
Cnt = (long*)(funct_ptr->arg);

uint32_t dd_m_1_low_machine_code = mov_imm((*Cnt*12)+s_a0_cnt_ofs+4 ,  7);
write_to_array( dd_m_1_low_machine_code);



uint32_t dd_m_1_high_machine_code = generate_movt_reg_imm((*Cnt*12)+s_a0_cnt_ofs+4  , 7);
write_to_array(dd_m_1_high_machine_code);




// RESETon: STR r10, [r6, r7]  // mov [ebx+dd_m_1],ecx
uint32_t str_r10 = generate_str_reg_regoffset(10, 6, 7, 0);
write_to_array(str_r10);

// RESETend:

}

// 850226 get CNTVAL machine code
// ------------------------------
static void get_cntval( char *code_ptr, struct funct *funct_ptr )
{
    NUM_TYPE *temp_ptr;
    int a1,a2;
    long int j,k;
    long *Reg,*Cnt;
    long s_a0_cnt_ofs;
    long s_a0_reg_ofs;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;            /* register offset */

    j=(long)s_mlc_cnt;           /* MLC_Data + 0x1000 */
    s_a0_cnt_ofs=j-k;            /* counter offset */

    a1=*code_ptr++;  /* macine word number */
    a2=*code_ptr++;  /* macine word number */

    Reg = (long*)(funct_ptr->arg+6);
    Cnt = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    CNTVAL(*(long*)(Data+%d),*(long*)(Data+%d))\n",(*Cnt*12)+s_a0_cnt_ofs+4,(*Reg*4) + s_a0_reg_ofs);

    while(a1>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a1--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Cnt= (long*)(funct_ptr->arg);
    *temp_ptr = (*Cnt*12)+s_a0_cnt_ofs+4;  /* get update value */
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;  /* get machine code */
        a2--;
    }
    temp_ptr = (NUM_TYPE *)pc_counter;
    Reg = (long*)(funct_ptr->arg+6);
    if(USR_REG_AREA_START <= *Reg)
    {
        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
        isAccessingUsrRegArea=1;
    }
    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  /* get destination */

    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;
}


void get_timer0( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=14;index++){
        a[index]=*code_ptr++;
    }

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C1) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    char buffer[256]; // 分配足夠的debug緩存
    // 2025 armv7

    // fprintf(plc_run_cpp,"    TIMER0(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d);\n",
    //                             bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
    //                             (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf,(*Timer*16)+s_a0_tmr_ofs+8,(*Timer*2)+s_a0_tmr_s_ofs+1);
    sprintf(buffer,"    TIMER0(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d);\n",
                                bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
                                (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf,(*Timer*16)+s_a0_tmr_ofs+8,(*Timer*2)+s_a0_tmr_s_ofs+1);
    OutputDebugStringA(buffer); // 輸出到 DebugView


    // (*Timer*16) + s_a0_tmr_ofs = dd_1  Set_Value 7e00
    // (*Timer*2) + s_a0_tmr_s_ofs = 8E00  dd_n
    // (*Timer*16)+s_a0_tmr_ofs+4 = dd_2 7e04
    // tmrbuf = 0x9000
    // (*Timer*16)+s_a0_tmr_ofs+8 = 7e08
    // (*Timer*2)+s_a0_tmr_s_ofs+1 = 8e01 dd_n_1
    
    // 若 bValue = 0 則 讀取 (*Reg*4) + s_a0_reg_ofs 為 mov  ecx,[ebx+registr]        //取Register值
    // mov  [ebx+dd_1],ecx           //SetValue  需要增加這兩段machine code


	//Timer0[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
    uint32_t machine_cmp = generate_cmp(5, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    	// BEQ instruction #跳轉指令
    uint32_t machine_beq = encode_beq(47*4 );
    //write_to_array( machine_beq);
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
  
    //mov cl,[ebx+dd_n]
		//mov r7 ,dd_n

    uint32_t dd_n = (*Timer*2) + s_a0_tmr_s_ofs; //8E00
    uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
	//write_to_array( dd_n_low_machine_code);    // 低16位
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
  
		//movt r7 , dd_n
	uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
    //write_to_array( dd_n_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    
    	// LDR R4, [R6, R7]  r4 = cl
    uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
    //write_to_array( ldr_dd_n_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    //mov [ebx+dd_n],al

    	// STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
    uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_r0_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // not cl
    uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
    //write_to_array( mvn_cl_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        //and al , cl
    uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
    //write_to_array( and_al_cl_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // beq TIMER0_3
    uint32_t and_al_cl_beq_machine_code = encode_beq(12 *4) ;
    //write_to_array( and_al_cl_beq_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        //mov eax , 0
    uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
    //write_to_array( eax_low_0_machine_code );
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//mov eax , 0
	uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
    //write_to_array( eax_high_0_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_high_0_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t Index_dd_2 = (*Timer*16)+s_a0_tmr_ofs+4; 
        //movt r8 , dd_2之低offset
    uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
	//write_to_array( dd_2_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r8 , dd_2之高offset
	uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
    //write_to_array( dd_2_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    

        // mov [ebx+dd_2] , eax  
    uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    //write_to_array( str_dd_2_0_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_dd_2_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



        //  mov edx,[ebx+TIMBUF0] 
    uint32_t Index_TIMBUF0 = tmrbuf;
        //mov r7 TIMBUF3
    uint32_t Index_TIMBUF0_low_machine_code = mov_imm(Index_TIMBUF0,  7);
	//write_to_array( Index_TIMBUF0_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (Index_TIMBUF0_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r7 , TIMBUF3 移動A3000之高16位元
	uint32_t Index_TIMBUF0_high_machine_code = generate_movt_reg_imm(Index_TIMBUF0, 7);
    //write_to_array( Index_TIMBUF0_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (Index_TIMBUF0_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    
        //  mov  edx,[ebx+TIMBUF0] 
    uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF0]        //FTimerBuffer0
    //write_to_array( ldr_TIMEBUF3_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // mov  [ebx+dd_3],edx 
    uint32_t dd_3 = (*Timer*16)+s_a0_tmr_ofs+8;
    uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
    //write_to_array( dd_3_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
    //write_to_array( dd_3_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
    //write_to_array( dd_3_str_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    //Timer0_3:
        // Load dd_n_1
    uint32_t dd_n_1 = (*Timer*2)+s_a0_tmr_s_ofs+1;
    uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
    //write_to_array( dd_n_1_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
    //write_to_array( dd_n_1_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //TimerRef3  mov  [ebx+dd_6],dl  
    //write_to_array( dd_n_1_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // cmp al , #0
    uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
    //write_to_array( cmp_al_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // bne Timer0_6
    uint32_t  bne_timer0_6 = generate_branch(18 * 4,0x1); // offser , branch type  1為bne
    //write_to_array( bne_timer0_6);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (bne_timer0_6 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    // @ Load FTimerBuffer3 and CountValue
        // Load FTimerBuffer0
    uint32_t FTimerBuffer0 = tmrbuf;
    uint32_t FTimerBuffer0_low = mov_imm(FTimerBuffer0, 7); // 設置低16位
    //write_to_array( FTimerBuffer0_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t FTimerBuffer0_high = generate_movt_reg_imm(FTimerBuffer0, 7); // 設置高16位
    //write_to_array( FTimerBuffer0_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t FTimerBuffer0_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer0  mov  ecx,[ebx+TIMBUF0]
    //write_to_array( FTimerBuffer0_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




        // Load CountValue dd_3

    uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
    //write_to_array( CountValue_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
    //write_to_array( CountValue_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
    //write_to_array( CountValue_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



        // sub  ecx,eax
    uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
    //write_to_array( FTimerBuffer3_sub_CountValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }






    //Timer0_5:
        //@ Store NowValue

        //movt r8 , dd_2之低offset
    dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
	//write_to_array( dd_2_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r8 , dd_2之高offset
	dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
    //write_to_array( dd_2_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        //mov  [ebx+dd_2],ecx  存入 //NowValue
    uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
    //write_to_array( dd_2_str_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // al = 0

    uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
    //write_to_array( al_low_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
    //write_to_array( al_high_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




        //cmp  ecx,[ebx+dd_1]           //SetValue

        
    uint32_t dd_1 = (*Timer*16) + s_a0_tmr_ofs ;
    uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
    //write_to_array( dd_1_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
    //write_to_array( dd_1_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
    //write_to_array( dd_1_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_ldr_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // cmp ecx ,  ebx+dd_1
    uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
    //write_to_array( cmp_NowValue_SetValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (cmp_NowValue_SetValue>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        // jl Timer0_1
    uint32_t blt_NowValue_SetValue = generate_branch(15*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
    //write_to_array( blt_NowValue_SetValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


 
    // TIMER0_6
        // MOV AL, #0xFF
    uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
    //write_to_array( mov_al_0xff_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0xff_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
    //write_to_array( mov_al_0xff_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // MOV [ebx+dd_n_1], AL
    uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
    //write_to_array( mov_dd_n_1_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
    //write_to_array( mov_dd_n_1_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_high  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_al_dd_n_1);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_al_dd_n_1  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // B TIMER0_1
    uint32_t g_timer0_1 = generate_branch(9*4, 0xe); // Always (0xE)
    //write_to_array( g_timer3_1);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (g_timer0_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    // TIMER0_4
        // MOV AL, #0x0
    uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
    //write_to_array( mov_al_0x0_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0x0_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
    //write_to_array( mov_al_0x0_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0x0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // MOV [ebx+dd_n], AL
    dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
    //write_to_array( dd_n_low_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
    //write_to_array( dd_n_high_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_high_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_al_dd_n);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // MOV [ebx+dd_n_1], AL
    uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
    //write_to_array( mov_dd_n_1_low_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
    //write_to_array( mov_dd_n_1_high_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_al_dd_n_1_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    // Timer0_1








    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0); 


}
/*********************************************************************/
void get_timer0_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=14;index++){
        a[index]=*code_ptr++;
    }

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C1) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    TIMER0(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d);\n",
                                bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
                                (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf,(*Timer*16)+s_a0_tmr_ofs+8,(*Timer*2)+s_a0_tmr_s_ofs+1);

    for(index=0;index<=14;index++){
        if ( !((funct_ptr->data.code == 0x0C1) && (index <=1))){
            while( a[index] > 0 ){
                *pc_counter++=*code_ptr++;  /* get machine code */
                a[index]--;
            }
        }
        else {
            while( a[index] > 0 ){
                *code_ptr++;  /* skip machine code for immediate data case */
                a[index]--;
            }
        }

        if (!( (funct_ptr->data.code == 0x0C1) && (index <=1) ) ){
            temp_ptr = (NUM_TYPE *)pc_counter;
            switch( index ){
            case 0:
                Reg = (long*)(funct_ptr->arg+6);
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
                break;
            case 1:
            case 11:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr = (*Timer*16) + s_a0_tmr_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 13:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*2) + s_a0_tmr_s_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 10:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+4;    //NowValue
                break;
            case 5:
            case 8:
                *temp_ptr=tmrbuf;                       //FTimerBuffer0 /* get timer buffer */
                break;
            case 6:
            case 9:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+8;    //CountValue
                break;
            case 7:
            case 12:
            case 14:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs+1;    //TimerOneShotBits /* get one shot buff*/
                break;
            }
            pc_counter+=NUM_SIZE;
        }
        code_ptr+=NUM_SIZE;
    }

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}





   
/*********************************************************************/
void get_timer1( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=18;index++){
        a[index]=*code_ptr++;
    }

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C3) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    // 2025 armv7
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    char buffer[256]; // 分配足夠的debug緩存
        sprintf(buffer,"    TIMER1(%lx,%lx,*(long*)(Data+%lx),*(long*)(Data+%lx),Data+%lx,*(long*)(Data+%lx),*(long*)(Data+%lx),*(long*)(Data+%lx),Data+%lx,Data+%lx,Data+%lx);\n",
        bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
        (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf+4,(*Timer*16)+s_a0_tmr_ofs+8,tmrbuf+16,(*Timer*16)+s_a0_tmr_ofs+14,(*Timer*2)+s_a0_tmr_s_ofs+1);
        OutputDebugStringA(buffer); // 輸出到 DebugView


    // (*Reg*4) + s_a0_reg_ofs == reg位址    
    // (*Timer*16) + s_a0_tmr_ofs = dd_1  Set_Value 7e00

    // (*Timer*2) + s_a0_tmr_s_ofs = 8E00  dd_n
    // (*Timer*16)+s_a0_tmr_ofs+4 = dd_2 7e04
    // tmrbuf = 0x9000
    // tmrbuf+4 = 0x9004
    // tmrbuf+16 = 0x9010
    // (*Timer*16)+s_a0_tmr_ofs+8 = 7e08
    // (*Timer*2)+s_a0_tmr_s_ofs+1 = 8e01
    // (*Timer*16)+s_a0_tmr_ofs+14 = 7e0e


	//Timer1[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(62*4 );
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n = (*Timer*2) + s_a0_tmr_s_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            // beq Timer1_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(18 *4) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_high_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t Index_dd_2 =(*Timer*16)+s_a0_tmr_ofs+4;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+4;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMBUF1] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF1]        //FTimerBuffer1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*Timer*16)+s_a0_tmr_ofs+8;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*Timer*16)+s_a0_tmr_ofs+14;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_strb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        // uint32_t dd_n_1 = 0x8E00 + (timer_n * 2) + 1;
        uint32_t dd_n_1 = (*Timer*2)+s_a0_tmr_s_ofs+1; //dd_n_1
        //Timer1_3:
            // Load dd_n_1
        
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_ldr_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // bne Timer1_6
        uint32_t  bne_Timer1_6 = generate_branch(27*4,0x1); // offser , branch type  1為bne
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bne_Timer1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  

        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer1
        uint32_t FTimerBuffer1 = tmrbuf+4;
        uint32_t FTimerBuffer1_low = mov_imm(FTimerBuffer1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_high = generate_movt_reg_imm(FTimerBuffer1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer1  mov  ecx,[ebx+TIMBUF1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
    
        //Timer1_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            
        uint32_t dd_1 = (*Timer*16) + s_a0_tmr_ofs;
        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;

            // jl Timer1_1        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t blt_NowValue_SetValue = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        //write_to_array( dd_6_low);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
        //write_to_array( dd_6_high);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_ldrb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        //write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        //write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        //write_to_array( ldrb_temp_TIMREF1_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_temp_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        //write_to_array( cmp_dd_6_TIMREF1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( cmp_dd_6_TIMREF1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        //write_to_array( bgt_NowValue_SetValue_1_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue_1_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        // Timer1_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // B Timer1_1
        uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (g_timer3_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        // Timer1_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        // Timer1_1



    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}

/*********************************************************************/
void get_timer1_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=18;index++){
        a[index]=*code_ptr++;
    }

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C3) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    TIMER1(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,Data+%d,Data+%d);\n",
                                bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
                                (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf+4,(*Timer*16)+s_a0_tmr_ofs+8,tmrbuf+16,(*Timer*16)+s_a0_tmr_ofs+14,(*Timer*2)+s_a0_tmr_s_ofs+1);

    for(index=0;index<=18;index++){
        if ( !((funct_ptr->data.code == 0x0C3) && (index <=1))){
            while( a[index] > 0 ){
                *pc_counter++=*code_ptr++;  /* get machine code */
                a[index]--;
            }
        }
        else {
            while( a[index] > 0 ){
                *code_ptr++;  /* skip machine code for immediate data case */
                a[index]--;
            }
        }

        if (!( (funct_ptr->data.code == 0x0C3) && (index <=1) ) ){
            temp_ptr = (NUM_TYPE *)pc_counter;
            switch( index ){
            case 0:
                Reg = (long*)(funct_ptr->arg+6);
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
                break;
            case 1:
            case 13:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr = (*Timer*16) + s_a0_tmr_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 17:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*2) + s_a0_tmr_s_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 12:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+4;    //NowValue
                break;
            case 5:
            case 10:
                *temp_ptr=tmrbuf+4;                       //FTimerBuffer1 /* get timer buffer */
                break;
            case 6:
            case 11:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+8;    //CountValue
                break;
            case 7:
            case 15:
                *temp_ptr=tmrbuf+16;                     //FTimerRef1 /* get timer reference */
                break;
            case 8:
            case 14:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+14;    //TimerRef3 /*get .001 sec update*/
                break;
            case 9:
            case 16:
            case 18:
                Timer = (long*)(funct_ptr->arg);
                *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs+1;    //TimerOneShotBits /* get one shot buff*/
                break;
            }
            pc_counter+=NUM_SIZE;
        }
        code_ptr+=NUM_SIZE;
    }
    //while( a[18]>0 ){
    //    *pc_counter++=*code_ptr++;  /* get machine code */
    //    a[18]--;
    //}

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}
/*********************************************************************/
void get_timer2( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=22;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C4) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);

    // 2025 armv7
    char buffer[256]; // 分配足夠的debug緩存
    sprintf(buffer,"   TIMER2(%d,%d,*(long*)(Data+%lx),*(long*)(Data+%lx),Data+%lx,*(long*)(Data+%lx),*(long*)(Data+%lx),*(long*)(Data+%lx),Data+%lx,Data+%lx,Data+%lx,Data+%lx,Data+%lx);\n",
    bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
    (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf+8,(*Timer*16)+s_a0_tmr_ofs+8,tmrbuf+17,
    (*Timer*16)+s_a0_tmr_ofs+13,tmrbuf+16,(*Timer*16)+s_a0_tmr_ofs+14,(*Timer*2)+s_a0_tmr_s_ofs+1);
    OutputDebugStringA(buffer); // 輸出到 DebugView
                        

    // (*Reg*4) + s_a0_reg_ofs == reg位址    
    // (*Timer*16) + s_a0_tmr_ofs = dd_1  Set_Value 7e00

    // (*Timer*2) + s_a0_tmr_s_ofs = 8E00  dd_n
    // (*Timer*16)+s_a0_tmr_ofs+4 = dd_2 7e04
    // tmrbuf = 0x9000
    // tmrbuf+4 = 0x9004
    // tmrbuf+16 = 0x9010
    // (*Timer*16)+s_a0_tmr_ofs+8 = 7e08
    // (*Timer*2)+s_a0_tmr_s_ofs+1 = 8e01
    // (*Timer*16)+s_a0_tmr_ofs+14 = 7e0e

      



	//Timer2[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        write_to_array( machine_cmp);
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(77*4 );
        write_to_array( machine_beq);
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n = (*Timer*2) + s_a0_tmr_s_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        write_to_array( dd_n_low_machine_code);    // 低16位
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        write_to_array( dd_n_high_machine_code);   // 高16位
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        write_to_array( ldr_dd_n_machine_code);
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_r0_machine_code);
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        write_to_array( mvn_cl_machine_code);
    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        write_to_array( and_al_cl_machine_code);
    
            // beq Timer2_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(24 *4) ;
        write_to_array( and_al_cl_beq_machine_code);
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        write_to_array( eax_low_0_machine_code );
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        write_to_array(eax_high_0_machine_code);   // 高16位
    
        uint32_t Index_dd_2 = (*Timer*16)+s_a0_tmr_ofs+4;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        write_to_array( str_dd_2_0_machine_code);
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+8;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        write_to_array( Index_TIMBUF1_low_machine_code);    // 低16位
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        write_to_array( Index_TIMBUF1_high_machine_code);   // 高16位
        
            //  mov  edx,[ebx+TIMBUF2] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF2]        //FTimerBuffer2
        write_to_array( ldr_TIMEBUF3_machine_code);
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*Timer*16)+s_a0_tmr_ofs+8;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( dd_3_low);
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( dd_3_high);
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        write_to_array( dd_3_str_code);
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
        uint32_t Index_TIMREF2 = tmrbuf+17;
        uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
            //movt r7 , TIMREF2 
        uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_TIMREF2_machine_code);
    
    
        //              mov  [ebx+dd_5],dl            //TimerRef2
        uint32_t dd_5 = (*Timer*16)+s_a0_tmr_ofs+13;
        uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
        write_to_array( dd_5_low);
        uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
        write_to_array( dd_5_high);
        uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
        write_to_array( dd_5_strb_code);
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF1_machine_code);
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*Timer*16)+s_a0_tmr_ofs+14;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        write_to_array( dd_6_low);
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        write_to_array( dd_6_high);
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
        write_to_array( dd_6_strb_code);
    
    
    
    
        //Timer2_3:
            // Load dd_n_1
        uint32_t dd_n_1 = (*Timer*2) + s_a0_tmr_s_ofs + 1;
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( dd_n_1_low);
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( dd_n_1_high);
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        write_to_array( dd_n_1_ldr_code);
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        write_to_array( cmp_al_0);
    
            // bne Timer2_6
        uint32_t  bne_Timer2_6 = generate_branch(36*4,0x1); // offser , branch type  1為bne
        write_to_array( bne_Timer2_6);
    
        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer2
        uint32_t FTimerBuffer2 = tmrbuf+8;
        uint32_t FTimerBuffer2_low = mov_imm(FTimerBuffer2, 7); // 設置低16位
        write_to_array( FTimerBuffer2_low);
        uint32_t FTimerBuffer2_high = generate_movt_reg_imm(FTimerBuffer2, 7); // 設置高16位
        write_to_array( FTimerBuffer2_high);
        uint32_t FTimerBuffer2_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF2]
        write_to_array( FTimerBuffer2_ldr_code);
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( CountValue_low);
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( CountValue_high);
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        write_to_array( CountValue_ldr_code);
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        write_to_array( FTimerBuffer3_sub_CountValue);
    
    
    
    
    
        //Timer2_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        write_to_array( dd_2_str_code);
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        write_to_array( al_low_0);
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( al_high_0);
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            
        uint32_t dd_1 = (*Timer*16) + s_a0_tmr_ofs;
        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        write_to_array( dd_1_low);
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        write_to_array( dd_1_high);
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        write_to_array( dd_1_ldr_code);
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
        write_to_array( cmp_NowValue_SetValue);
            // jl Timer2_1
        uint32_t blt_NowValue_SetValue = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_NowValue_SetValue);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue1_6);
    
    ////////////////////////
    
            //mov  dl,[ebx+dd_5]            //TimerRef2
    
            
    
    
        write_to_array( dd_5_low);
    
        write_to_array( dd_5_high);
        uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
        write_to_array( dd_5_ldrb_code);
            // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
    
    
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
        write_to_array( ldrb_temp_TIMREF2_machine_code);
    
         
        uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_5_TIMREF2);
            // jl Timer2_1
        uint32_t blt_jg_TIMER2_1 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_jg_TIMER2_1);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_jl_TIMER2_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_jl_TIMER2_6);
    
    
    ////////////////////////
            //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        write_to_array( dd_6_low);
    
        write_to_array( dd_6_high);
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        write_to_array( dd_6_ldrb_code);
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_temp_TIMREF1_machine_code);
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_6_TIMREF1);
    
    
        //jg   Timer2_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue_1_1);
    
    
    
    
        // Timer2_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
        write_to_array( mov_al_0xff_low);
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0xff_high);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low);
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high);
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1);
    
            // B Timer2_1
        uint32_t g_timer2_1 = generate_branch(9*4, 0xe); // Always (0xE)
        write_to_array( g_timer2_1);
    
        // Timer2_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
        write_to_array( mov_al_0x0_low);
    
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0x0_high);
    
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
        write_to_array( dd_n_low_machine_code);
    
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
        write_to_array( dd_n_high_machine_code);
    
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low_again);
    
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high_again);
    
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1_again);
    
    
        // Timer2_1

        
    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}
/*********************************************************************/
void get_timer2_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *Reg,*Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=22;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C4) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    TIMER2(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,Data+%d,Data+%d,Data+%d,Data+%d);\n",
                                bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
                                (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf+8,(*Timer*16)+s_a0_tmr_ofs+8,tmrbuf+17,
                                (*Timer*16)+s_a0_tmr_ofs+13,tmrbuf+16,(*Timer*16)+s_a0_tmr_ofs+14,(*Timer*2)+s_a0_tmr_s_ofs+1);

    for(index=0;index<=22;index++)
    {
        if(!((funct_ptr->data.code == 0x0C4) && (index <=1)))
        {
             while( a[index] > 0 )
             {
                *pc_counter++=*code_ptr++;  /* get machine code */
                a[index]--;
             }
        }
        else
        {
             while( a[index] > 0 )
             {
                *code_ptr++;  /* skip machine code for immediate data case */
                a[index]--;
             }
        }

        if(!( (funct_ptr->data.code == 0x0C4) && (index <= 1) ) )
        {
            temp_ptr = (NUM_TYPE *)pc_counter;
            switch( index )
            {
                case 0:
                    Reg = (long*)(funct_ptr->arg+6);
                    if(USR_REG_AREA_START <= *Reg)
                    {
                        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                        isAccessingUsrRegArea=1;
                    }
                    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
                    break;
                case 1:
                case 15:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs; //SetValue
                    break;
                case 2:
                case 3:
                case 21:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs;  //TimerStatus
                    break;
                case 4:
                case 14:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+4; //NowValue
                    break;
                case 5:
                case 12:
                    *temp_ptr=tmrbuf+8;           //FTimerBuffer2 /* get timer buffer 2 */
                    break;
                case 6:
                case 13:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+8; //CountValue
                    break;
                case 7:
                case 17:
                    *temp_ptr=tmrbuf+17;        //FTimerRef2 /* get timer reference 2 */
                    break;
                case 8:
                case 16:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+13; //TimerRef2 /*get 0.01 sec update*/
                    break;
                case 9:
                case 19:
                    *temp_ptr=tmrbuf+16;         //FTimerRef1 /* get timer reference 1 */
                    break;
                case 10:
                case 18:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+14; //TimerRef3 /*get .001 sec update*/
                    break;
                case 11:
                case 20:
                case 22:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs+1; //TimerOneShotBits /* get one shot buff*/
                    break;
            }
            pc_counter+=NUM_SIZE;
        }
        code_ptr+=NUM_SIZE;
    }
    //while(a[22]>0)
    //{
    //    *pc_counter++=*code_ptr++;  /* get machine code */
    //    a[22]--;
    //}

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}


/*********************************************************************/
/*********************************************************************/
void get_timer3( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *Reg, *Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=26;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C5) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);

    // 2025 armv7

    // char buffer[256]; // 分配足夠的debug緩存
    // sprintf(buffer,
    //     "   TIMER3(\n"
    //     "       bValue = %d,\n"
    //     "       Value = %d,\n"
    //     "       (*Reg * 4) + s_a0_reg_ofs = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs = 0x%lx,\n"
    //     "       (*Timer * 2) + s_a0_tmr_s_ofs = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs + 4 = 0x%lx,\n"
    //     "       tmrbuf + 12 = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs + 8 = 0x%lx,\n"
    //     "       tmrbuf + 18 = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs + 12 = 0x%lx,\n"
    //     "       tmrbuf + 17 = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs + 13 = 0x%lx,\n"
    //     "       tmrbuf + 16 = 0x%lx,\n"
    //     "       (*Timer * 16) + s_a0_tmr_ofs + 14 = 0x%lx,\n"
    //     "       (*Timer * 2) + s_a0_tmr_s_ofs + 1 = 0x%lx\n"
    //     "   );\n",
    //     bValue, 
    //     Value,
    //     (*Reg * 4) + s_a0_reg_ofs,
    //     (*Timer * 16) + s_a0_tmr_ofs,
    //     (*Timer * 2) + s_a0_tmr_s_ofs,
    //     (*Timer * 16) + s_a0_tmr_ofs + 4,
    //     tmrbuf + 12,
    //     (*Timer * 16) + s_a0_tmr_ofs + 8,
    //     tmrbuf + 18,
    //     (*Timer * 16) + s_a0_tmr_ofs + 12,
    //     tmrbuf + 17,
    //     (*Timer * 16) + s_a0_tmr_ofs + 13,
    //     tmrbuf + 16,
    //     (*Timer * 16) + s_a0_tmr_ofs + 14,
    //     (*Timer * 2) + s_a0_tmr_s_ofs + 1
    // );
    
    // // 輸出到 DebugView
    // OutputDebugStringA(buffer);
    
 
    // [20692]    TIMER3(
    //     [20692]        bValue = 1,
    //     [20692]        Value = 50,
    //     [20692]        (*Reg * 4) + s_a0_reg_ofs = 0x100c8,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs = 0x7e00,
    //     [20692]        (*Timer * 2) + s_a0_tmr_s_ofs = 0x8e00,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs + 4 = 0x7e04,
    //     [20692]        tmrbuf + 12 = 0x900c,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs + 8 = 0x7e08,
    //     [20692]        tmrbuf + 18 = 0x9012,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs + 12 = 0x7e0c,
    //     [20692]        tmrbuf + 17 = 0x9011,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs + 13 = 0x7e0d,
    //     [20692]        tmrbuf + 16 = 0x9010,
    //     [20692]        (*Timer * 16) + s_a0_tmr_ofs + 14 = 0x7e0e,
    //     [20692]        (*Timer * 2) + s_a0_tmr_s_ofs + 1 = 0x8e01
    //     [20692]    );
	//Timer3[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        write_to_array( machine_cmp);
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(92*4 );
        write_to_array( machine_beq);
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n =(*Timer * 2) + s_a0_tmr_s_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        write_to_array( dd_n_low_machine_code);    // 低16位
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        write_to_array( dd_n_high_machine_code);   // 高16位
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        write_to_array( ldr_dd_n_machine_code);
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_r0_machine_code);
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        write_to_array( mvn_cl_machine_code);
    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        write_to_array( and_al_cl_machine_code);
    
            // beq Timer3_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(30 *4) ;
        write_to_array( and_al_cl_beq_machine_code);
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        write_to_array( eax_low_0_machine_code );
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        write_to_array(eax_high_0_machine_code);   // 高16位
    
        uint32_t Index_dd_2 = (*Timer * 16) + s_a0_tmr_ofs + 4;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        write_to_array( str_dd_2_0_machine_code);
    
    
            //  mov edx,[ebx+TIMBUF3] 
        uint32_t Index_TIMBUF3 = tmrbuf + 12 ;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF3_low_machine_code = mov_imm(Index_TIMBUF3,  7);
        write_to_array( Index_TIMBUF3_low_machine_code);    // 低16位
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF3_high_machine_code = generate_movt_reg_imm(Index_TIMBUF3, 7);
        write_to_array( Index_TIMBUF3_high_machine_code);   // 高16位
        
            //  mov  edx,[ebx+TIMBUF3] 
        uint32_t ldr_TIMBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF3]        //FTimerBuffer2
        write_to_array(ldr_TIMBUF3_machine_code);
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*Timer * 16) + s_a0_tmr_ofs + 8;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( dd_3_low);
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( dd_3_high);
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        write_to_array( dd_3_str_code);
    ///////////////////////////////////////////////////
    
    
    
        //              mov  dl,[ebx+TIMREF3]          //FTimerRef2
            //mov r7 TIMREF3
        uint32_t Index_TIMREF3 = tmrbuf + 18 ;
        uint32_t Index_TIMREF3_low_machine_code = mov_imm(Index_TIMREF3,  7);
        write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
            //movt r7 , TIMREF3 
        uint32_t Index_TIMREF3_high_machine_code = generate_movt_reg_imm(Index_TIMREF3, 7);
        write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF3] 
        uint32_t ldrb_TIMREF3_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF3_machine_code);
    
    
        //              mov  [ebx+dd_4],dl            //TimerRef2
        uint32_t dd_4 = (*Timer * 16) + s_a0_tmr_ofs + 12;
        uint32_t dd_4_low = mov_imm(dd_4, 7); // 設置低16位
        write_to_array( dd_4_low);
        uint32_t dd_4_high = generate_movt_reg_imm(dd_4, 7); // 設置高16位
        write_to_array( dd_4_high);
        uint32_t dd_4_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_4],dl  
        write_to_array( dd_4_strb_code);
    
    
    
    ///////////////////////////////////////////////////
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
        uint32_t Index_TIMREF2 = tmrbuf + 17;
        uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
            //movt r7 , TIMREF2 
        uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF2_machine_code);
    
    
        //              mov  [ebx+dd_5],dl            //TimerRef2
        uint32_t dd_5 =(*Timer * 16) + s_a0_tmr_ofs + 13;
        uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
        write_to_array( dd_5_low);
        uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
        write_to_array( dd_5_high);
        uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
        write_to_array( dd_5_strb_code);
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf + 16 ;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF1_machine_code);
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*Timer * 16) + s_a0_tmr_ofs + 14;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        write_to_array( dd_6_low);
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        write_to_array( dd_6_high);
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
        write_to_array( dd_6_strb_code);
    
    
    
    
        //Timer3_3:
            // Load dd_n_1
        uint32_t dd_n_1 = (*Timer * 2) + s_a0_tmr_s_ofs + 1;
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( dd_n_1_low);
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( dd_n_1_high);
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        write_to_array( dd_n_1_ldr_code);
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        write_to_array( cmp_al_0);
    
            // bne Timer3_6
        uint32_t  bne_Timer3_6 = generate_branch(45*4,0x1); // offser , branch type  1為bne
        write_to_array( bne_Timer3_6);
    
        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer3
        uint32_t FTimerBuffer3 = tmrbuf + 12 ;
        uint32_t FTimerBuffer3_low = mov_imm(FTimerBuffer3, 7); // 設置低16位
        write_to_array( FTimerBuffer3_low);
        uint32_t FTimerBuffer3_high = generate_movt_reg_imm(FTimerBuffer3, 7); // 設置高16位
        write_to_array( FTimerBuffer3_high);
        uint32_t FTimerBuffer3_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF3]
        write_to_array( FTimerBuffer3_ldr_code);
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( CountValue_low);
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( CountValue_high);
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        write_to_array( CountValue_ldr_code);
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        write_to_array( FTimerBuffer3_sub_CountValue);
    
    
    
    
    
        //Timer3_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        write_to_array( dd_2_str_code);
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        write_to_array( al_low_0);
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( al_high_0);
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            
        uint32_t dd_1 = (*Timer * 16) + s_a0_tmr_ofs;
        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        write_to_array( dd_1_low);
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        write_to_array( dd_1_high);
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        write_to_array( dd_1_ldr_code);
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
        write_to_array( cmp_NowValue_SetValue);
            // jl Timer3_1
        uint32_t blt_NowValue_SetValue = generate_branch(42*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_NowValue_SetValue);
    
            //jg   Timer3_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(27*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue1_6);
    
    //////////////////////////////////////////////
    
            //mov  dl,[ebx+dd_4]            //TimerRef3
    
            
    
    
        write_to_array( dd_4_low);
    
        write_to_array( dd_4_high);
        uint32_t dd_4_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_4]            //TimerRef3  
        write_to_array( dd_4_ldrb_code);
            // cmp  dl,[ebx+TIMREF3]         //FTimerRef3
    
        //              mov  dl,[ebx+TIMREF3]          //FTimerRef3
            //mov r7 TIMREF3
    
    
        write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF3] 
        uint32_t ldrb_temp_TIMREF3_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF3]        //FTimerRef3
        write_to_array(ldrb_temp_TIMREF3_machine_code);
    
         
        uint32_t cmp_dd_4_TIMREF3 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_4_TIMREF3);
            // jl Timer3_1
        uint32_t blt_jl_TIMREF3 = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_jl_TIMREF3);
    
            //jg   Timer3_6                 //超過設定值
    
        uint32_t bgt_jg_TIMREF3 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_jg_TIMREF3);
    
    
    
    //////////////////////////////////////////////
    ////////////////////////
    
            //mov  dl,[ebx+dd_5]            //TimerRef2
    
            
    
    
        write_to_array( dd_5_low);
    
        write_to_array( dd_5_high);
        uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
        write_to_array( dd_5_ldrb_code);
            // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
    
    
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
        write_to_array(ldrb_temp_TIMREF2_machine_code);
    
         
        uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_5_TIMREF2);
            // jl Timer3_1
        uint32_t blt_jl_TIMREF2 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_jl_TIMREF2);
    
            //jg   Timer3_6                 //超過設定值
    
        uint32_t bgt_jg_TIMREF2 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_jg_TIMREF2);
    
    
    ////////////////////////
            //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        write_to_array( dd_6_low);
    
        write_to_array( dd_6_high);
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        write_to_array( dd_6_ldrb_code);
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF3
    
    
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  r7,[ebx+TIMBUF3] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_temp_TIMREF1_machine_code);
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        write_to_array(cmp_dd_6_TIMREF1);
    
    
        //jg   Timer3_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue_1_1);
    
    
    
    
        // Timer3_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
        write_to_array( mov_al_0xff_low);
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0xff_high);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low);
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high);
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1);
    
            // B Timer3_1
        uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)
        write_to_array( g_timer3_1);
    
        // Timer3_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
        write_to_array( mov_al_0x0_low);
    
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0x0_high);
    
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
        write_to_array( dd_n_low_machine_code);
    
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
        write_to_array( dd_n_high_machine_code);
    
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low_again);
    
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high_again);
    
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1_again);
    
    
        // Timer3_1
    

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}

/*********************************************************************/
void get_timer3_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *Reg, *Timer;
    long s_a0_reg_ofs;
    long s_a0_tmr_ofs, s_a0_tmr_s_ofs;
    long tmrbuf;
    long *arg1,*arg2;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;

    j=(long)s_mlc_tmr1;         /* s_a1_ref + 0x1800 */
    s_a0_tmr_ofs=j-k;

    j=(long)s_mlc_tmr_stus;     /* s_a1_ref + 0x2200 */
    s_a0_tmr_s_ofs=j-k;

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=26;index++)
        a[index]=*code_ptr++;

    bool bValue=false;
    long Value = 0;     
    if (funct_ptr->data.code == 0x0C5) //Value
    {
      bValue = true;
      Value = *(long*)(funct_ptr->arg+6);
    }
    Reg = (long*)(funct_ptr->arg+6);
    Timer = (long*)(funct_ptr->arg);
    fprintf(plc_run_cpp,"    TIMER3(%d,%d,*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,*(long*)(Data+%d),*(long*)(Data+%d),*(long*)(Data+%d),Data+%d,Data+%d,Data+%d,Data+%d,Data+%d,Data+%d,Data+%d);\n",
                                bValue,Value,(*Reg*4) + s_a0_reg_ofs,(*Timer*16) + s_a0_tmr_ofs,(*Timer*2) + s_a0_tmr_s_ofs,
                                (*Timer*16)+s_a0_tmr_ofs+4,tmrbuf+12,(*Timer*16)+s_a0_tmr_ofs+8,tmrbuf+18,(*Timer*16)+s_a0_tmr_ofs+12,
                                tmrbuf+17,(*Timer*16)+s_a0_tmr_ofs+13,tmrbuf+16,(*Timer*16)+s_a0_tmr_ofs+14,(*Timer*2)+s_a0_tmr_s_ofs+1);

    for(index=0;index<=26;index++)
    {
        if(!( (funct_ptr->data.code == 0x0C5) && (index <=1) ) )
        {
            while( a[index] > 0 )
            {
                *pc_counter++=*code_ptr++;  /* get machine code */
                a[index]--;
            }
        }
        else
        {
            while( a[index] > 0 )
            {
                *code_ptr++;  /* skip machine code for immediate data case */
                a[index]--;
            }
        }

        if(!( (funct_ptr->data.code == 0x0C5) && (index <=1) ) )
        {
            temp_ptr = (NUM_TYPE *)pc_counter;
            switch( index )
            {
                case 0:
                    Reg = (long*)(funct_ptr->arg+6);
                    if(USR_REG_AREA_START <= *Reg)
                    {
                        *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                        isAccessingUsrRegArea=1;
                    }
                    *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  //取Register值
                    break;
                case 1:
                case 17:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs;   //SetValue
                    break;
                case 2:
                case 3:
                case 25:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs;  //TimerStatus /* get one shot buff*/
                    break;
                case 4:
                case 16:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+4; //NowValue
                    break;
                case 5:
                case 14:
                    *temp_ptr=tmrbuf+12;                  //FTimerBuffer3 /* get timer buffer 3 */
                    break;
                case 6:
                case 15:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+8; //CountValue
                    break;
                case 7:
                case 19:
                    *temp_ptr=tmrbuf+18;                  //FTimerRef3 /* get timer reference 3 */
                    break;
                case 8:
                case 18:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+12;//TimerRef1 /*get 0.1 sec update*/
                    break;
                case 9:
                case 21:
                    *temp_ptr=tmrbuf+17;                  //FTimerRef2 /* get timer reference 2 */
                    break;
                case 10:
                case 20:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+13;//TimerRef2 /*get 0.01 sec update*/
                    break;
                case 11:
                case 23:
                    *temp_ptr=tmrbuf+16;                  //FTimerRef1 /* get timer reference 1 */
                    break;
                case 12:
                case 22:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*16)+s_a0_tmr_ofs+14;//TimerRef3 /*get .001 sec update*/
                    break;
                case 13:
                case 24:
                case 26:
                    Timer = (long*)(funct_ptr->arg);
                    *temp_ptr=(*Timer*2)+s_a0_tmr_s_ofs+1;//TimerOneShotBits /* get one shot buff*/
                    break;
            }
            pc_counter+=NUM_SIZE;
        }
        code_ptr+=NUM_SIZE;
    }
    //while(a[26]>0)
    //{
    //    *pc_counter++=*code_ptr++;  /* get machine code */
    //    a[26]--;
    //}

    arg1=(long*)(funct_ptr->arg);
    arg2=(long*)(funct_ptr->arg+6);
    parpt=(struct regpar *)par_tab(parpt,'T',*arg1,*arg2&0x7FFFFFFF,0,0,0);
}


/*********************************************************************/


void Get_RTimer1MS_Reg( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;
    char buffer[256]; // 分配足夠的debug緩存

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;


    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    // 2025 armv7
    sprintf(buffer, "%s 0x%lx ", "20250119 func=RTimer1MS_Reg*tmrbuf", tmrbuf); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

    sprintf(buffer, "%s 0x%lx ", "20250119 func=RTimer1MS_Reg*tmrbuf", tmrbuf); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView

    sprintf(buffer, "%s 0x%lx ", "20250119 func=RTimer1MS_Reg*tmrbuf", tmrbuf); // 格式化字符串
    OutputDebugStringA(buffer); // 輸出到 DebugView


    //Timer0[]                 MOV R0 , #10


    uint32_t registr = (*Reg*4) + s_a0_reg_ofs; //reg

    uint32_t reg_index_offset_low_machine_code = mov_imm(registr,  7);
	write_to_array( reg_index_offset_low_machine_code);    // 低16位

	uint32_t reg_index_offset_high_machine_code = generate_movt_reg_imm(registr, 7);
    write_to_array(reg_index_offset_high_machine_code);   // 高16位
    

    uint32_t ldr_reg_index_offset_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_reg_index_offset_machine_code);

    // mov  [ebx+dd_1],ecx           //SetValue
    uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;
    uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
	write_to_array( dd_1_low_machine_code);    // 低16位

	uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
    write_to_array(dd_1_high_machine_code);   // 高16位
    

    uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_dd_1_offset_machine_code);







    // CMP R5, #0  比對al 跟 0 
    uint32_t machine_cmp = generate_cmp(5, 0);

    for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }


    	// BEQ instruction #跳轉指令
    uint32_t machine_beq = encode_beq(48*4 );
    //write_to_array( machine_beq);
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
  
    //mov cl,[ebx+dd_n]
		//mov r7 ,dd_n

    uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs; //8E00 
    uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
	//write_to_array( dd_n_low_machine_code);    // 低16位
          for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
  
		//movt r7 , dd_n
	uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
    //write_to_array( dd_n_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    
    	// LDR R4, [R6, R7]  r4 = cl
    uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
    //write_to_array( ldr_dd_n_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    //mov [ebx+dd_n],al

    uint32_t al_8bit = generate_and_imm(5,5,0x01);
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        *pc_counter = (al_8bit >> (i * 8)) & 0xFF; // 提取每個位元組
        pc_counter++;                            // 指向下一個位元組
}
    	// STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
    uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_r0_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // not cl
    uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
    //write_to_array( mvn_cl_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        
        //and al , cl
    uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
    //write_to_array( and_al_cl_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // beq TIMER0_3
    uint32_t and_al_cl_beq_machine_code = encode_beq(12 *4) ;
    //write_to_array( and_al_cl_beq_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        //mov eax , 0
    uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
    //write_to_array( eax_low_0_machine_code );
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//mov eax , 0
	uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
    //write_to_array( eax_high_0_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (eax_high_0_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t Index_dd_2 =(*TimerReg*4+8)+s_a0_reg_ofs;
        //movt r8 , dd_2之低offset
    uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
	//write_to_array( dd_2_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r8 , dd_2之高offset
	uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
    //write_to_array( dd_2_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    

        // mov [ebx+dd_2] , eax  
    uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
    //write_to_array( str_dd_2_0_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_dd_2_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



        //  mov edx,[ebx+TIMBUF0] 
    uint32_t Index_TIMBUF0 = tmrbuf;
        //mov r7 TIMBUF3
    uint32_t Index_TIMBUF0_low_machine_code = mov_imm(Index_TIMBUF0,  7);
	//write_to_array( Index_TIMBUF0_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (Index_TIMBUF0_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r7 , TIMBUF3 移動A3000之高16位元
	uint32_t Index_TIMBUF0_high_machine_code = generate_movt_reg_imm(Index_TIMBUF0, 7);
    //write_to_array( Index_TIMBUF0_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (Index_TIMBUF0_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    
        //  mov  edx,[ebx+TIMBUF0] 
    uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF0]        //FTimerBuffer0
    //write_to_array( ldr_TIMEBUF3_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // mov  [ebx+dd_3],edx 
    uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
    uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
    //write_to_array( dd_3_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
    //write_to_array( dd_3_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
    //write_to_array( dd_3_str_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    //Timer0_3:
        // Load dd_n_1
    uint32_t dd_n_1 = (*TimerReg*4)+s_a0_reg_ofs;
    uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
    //write_to_array( dd_n_1_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
    //write_to_array( dd_n_1_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //TimerRef3  mov  [ebx+dd_6],dl  
    //write_to_array( dd_n_1_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // cmp al , #0
    uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
    //write_to_array( cmp_al_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // bne Timer0_6
    uint32_t  bne_timer0_6 = generate_branch(18 * 4,0x1); // offser , branch type  1為bne
    //write_to_array( bne_timer0_6);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (bne_timer0_6 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    // @ Load FTimerBuffer3 and CountValue
        // Load FTimerBuffer0
    uint32_t FTimerBuffer0 = tmrbuf;
    uint32_t FTimerBuffer0_low = mov_imm(FTimerBuffer0, 7); // 設置低16位
    //write_to_array( FTimerBuffer0_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t FTimerBuffer0_high = generate_movt_reg_imm(FTimerBuffer0, 7); // 設置高16位
    //write_to_array( FTimerBuffer0_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t FTimerBuffer0_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer0  mov  ecx,[ebx+TIMBUF0]
    //write_to_array( FTimerBuffer0_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer0_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




        // Load CountValue dd_3

    uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
    //write_to_array( CountValue_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
    //write_to_array( CountValue_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
    //write_to_array( CountValue_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }



        // sub  ecx,eax
    uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
    //write_to_array( FTimerBuffer3_sub_CountValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }






    //Timer0_5:
        //@ Store NowValue

        //movt r8 , dd_2之低offset
    dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
	//write_to_array( dd_2_low_machine_code);    // 低16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

		//movt r8 , dd_2之高offset
	dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
    //write_to_array( dd_2_high_machine_code);   // 高16位
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        //mov  [ebx+dd_2],ecx  存入 //NowValue
    uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
    //write_to_array( dd_2_str_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // al = 0

    uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
    //write_to_array( al_low_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
    //write_to_array( al_high_0);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = ( al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




        //cmp  ecx,[ebx+dd_1]           //SetValue

        

    uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
    //write_to_array( dd_1_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
    //write_to_array( dd_1_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

    uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
    //write_to_array( dd_1_ldr_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_1_ldr_code>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // cmp ecx ,  ebx+dd_1
    uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
    //write_to_array( cmp_NowValue_SetValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (cmp_NowValue_SetValue>> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }

        // jl Timer0_1
    uint32_t blt_NowValue_SetValue = generate_branch(15*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
    //write_to_array( blt_NowValue_SetValue);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


 
    // TIMER0_6
        // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0x00ff, 5); // 設置低16位
        //write_to_array( mov_al_0xff_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0xff_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        //write_to_array( mov_al_0xff_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        //write_to_array( mov_dd_n_1_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        //write_to_array( mov_dd_n_1_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_high  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_dd_n_1  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    

        // B TIMER0_1
    uint32_t g_timer0_1 = generate_branch(9*4, 0xe); // Always (0xE)
    //write_to_array( g_timer3_1);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (g_timer0_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    // TIMER0_4
        // MOV AL, #0x0
    uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
    //write_to_array( mov_al_0x0_low);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0x0_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
    //write_to_array( mov_al_0x0_high);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_al_0x0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // MOV [ebx+dd_n], AL
    dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
    //write_to_array( dd_n_low_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
    //write_to_array( dd_n_high_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (dd_n_high_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_al_dd_n);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


        // MOV [ebx+dd_n_1], AL
    uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
    //write_to_array( mov_dd_n_1_low_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
    //write_to_array( mov_dd_n_1_high_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }


    uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
    //write_to_array( str_al_dd_n_1_again);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }




    // Timer0_1


}

/*********************************************************************/
void Get_RTimer1MS_Imm( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;
    // 2025 armv7

    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=14;index++){
        a[index]=*code_ptr++;
    }
    char buffer[256]; // 分配足夠的debug緩存
    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

        sprintf(buffer, "%s 0x%lx ", "20250119 func=get_ld type=LD *immVal", *immVal); // 格式化字符串
        OutputDebugStringA(buffer); // 輸出到 DebugView
    
        //Timer0[]                 MOV R0 , #10
    
        uint32_t imm_data =*immVal;  // Rtimer
        uint32_t IMM_low_machine_code = mov_imm(imm_data,  10);
        write_to_array(IMM_low_machine_code);    // 低16位
    
        uint32_t IMM_high_machine_code = generate_movt_reg_imm(imm_data, 10);
        write_to_array( IMM_high_machine_code);   // 高16位
    
            
        uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;  // Rtimer
        // mov  [ebx+dd_1],ecx           //SetValue
        uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
        write_to_array( dd_1_low_machine_code);    // 低16位
    
        uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
        write_to_array( dd_1_high_machine_code);   // 高16位
        
    
        uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        write_to_array( str_dd_1_offset_machine_code);
    
        
    
    
    
    
    
    
    
        // CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
    
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
                *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
                pc_counter++;                            // 指向下一個位元組
        }
    
    
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(47*4 );
        //write_to_array( machine_beq);
              for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
      
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs; //8E00 
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        //write_to_array( dd_n_low_machine_code);    // 低16位
              for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
      
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        //write_to_array( dd_n_high_machine_code);   // 高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        //write_to_array( ldr_dd_n_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_r0_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        //write_to_array( mvn_cl_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        //write_to_array( and_al_cl_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // beq TIMER0_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(12 *4) ;
        //write_to_array( and_al_cl_beq_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //mov eax , 0
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        //write_to_array( eax_low_0_machine_code );
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        //write_to_array( eax_high_0_machine_code);   // 高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (eax_high_0_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t Index_dd_2 =(*TimerReg*4+8)+s_a0_reg_ofs;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        //write_to_array( dd_2_low_machine_code);    // 低16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        //write_to_array( dd_2_high_machine_code);   // 高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        //write_to_array( str_dd_2_0_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_dd_2_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
            //  mov edx,[ebx+TIMBUF0] 
        uint32_t Index_TIMBUF0 = tmrbuf;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF0_low_machine_code = mov_imm(Index_TIMBUF0,  7);
        //write_to_array( Index_TIMBUF0_low_machine_code);    // 低16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (Index_TIMBUF0_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF0_high_machine_code = generate_movt_reg_imm(Index_TIMBUF0, 7);
        //write_to_array( Index_TIMBUF0_high_machine_code);   // 高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (Index_TIMBUF0_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        
            //  mov  edx,[ebx+TIMBUF0] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF0]        //FTimerBuffer0
        //write_to_array( ldr_TIMEBUF3_machine_code);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        //write_to_array( dd_3_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_3_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        //write_to_array( dd_3_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        //write_to_array( dd_3_str_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        //Timer0_3:
            // Load dd_n_1
        uint32_t dd_n_1 = (*TimerReg*4)+s_a0_reg_ofs;
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        //write_to_array( dd_n_1_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        //write_to_array( dd_n_1_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //TimerRef3  mov  [ebx+dd_6],dl  
        //write_to_array( dd_n_1_ldr_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        //write_to_array( cmp_al_0);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // bne Timer0_6
        uint32_t  bne_timer0_6 = generate_branch(18 * 4,0x1); // offser , branch type  1為bne
        //write_to_array( bne_timer0_6);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (bne_timer0_6 >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer0
        uint32_t FTimerBuffer0 = tmrbuf;
        uint32_t FTimerBuffer0_low = mov_imm(FTimerBuffer0, 7); // 設置低16位
        //write_to_array( FTimerBuffer0_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (FTimerBuffer0_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t FTimerBuffer0_high = generate_movt_reg_imm(FTimerBuffer0, 7); // 設置高16位
        //write_to_array( FTimerBuffer0_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (FTimerBuffer0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t FTimerBuffer0_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer0  mov  ecx,[ebx+TIMBUF0]
        //write_to_array( FTimerBuffer0_ldr_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (FTimerBuffer0_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        //write_to_array( CountValue_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        //write_to_array( CountValue_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( CountValue_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        //write_to_array( CountValue_ldr_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        //write_to_array( FTimerBuffer3_sub_CountValue);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
    
    
    
        //Timer0_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        //write_to_array( dd_2_low_machine_code);    // 低16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_2_low_machine_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        //write_to_array( dd_2_high_machine_code);   // 高16位
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        //write_to_array( dd_2_str_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        //write_to_array( al_low_0);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        //write_to_array( al_high_0);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = ( al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            
    
        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        //write_to_array( dd_1_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        //write_to_array( dd_1_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        //write_to_array( dd_1_ldr_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_1_ldr_code>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
        //write_to_array( cmp_NowValue_SetValue);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (cmp_NowValue_SetValue>> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
            // jl Timer0_1
        uint32_t blt_NowValue_SetValue = generate_branch(15*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        //write_to_array( blt_NowValue_SetValue);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
     
        // TIMER0_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0x00ff, 5); // 設置低16位
        //write_to_array( mov_al_0xff_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0xff_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        //write_to_array( mov_al_0xff_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        //write_to_array( mov_dd_n_1_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        //write_to_array( mov_dd_n_1_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_high  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_dd_n_1  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // B TIMER0_1
        uint32_t g_timer0_1 = generate_branch(9*4, 0xe); // Always (0xE)
        //write_to_array( g_timer3_1);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (g_timer0_1 >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        // TIMER0_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
        //write_to_array( mov_al_0x0_low);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0x0_low  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
        //write_to_array( mov_al_0x0_high);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_al_0x0_high >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
        //write_to_array( dd_n_low_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
        //write_to_array( dd_n_high_machine_code);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (dd_n_high_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n);
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
        //write_to_array( mov_dd_n_1_low_again);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        //write_to_array( mov_dd_n_1_high_again);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1_again);
                for (size_t i = 0; i < sizeof(uint32_t); i++) {
                    *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
                    pc_counter++;                            // 指向下一個位元組
            }
    
    
    
    
        // Timer0_1
    

}

/*********************************************************************/
/*********************************************************************/
void Get_RTimer1MS_Reg_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=14;index++){
        a[index]=*code_ptr++;
    }

    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=14;index++){
        while( a[index] > 0 ){
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index ){
        case 0:
            Reg = (long*)(funct_ptr->arg+6);
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
            break;
        case 1:
        case 11:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
            break;
        case 2:
        case 3:
        case 13:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
            break;
        case 4:
        case 10:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
            break;
        case 5:
        case 8:
            *temp_ptr=tmrbuf;                       //FTimerBuffer0 /* get timer buffer */
            break;
        case 6:
        case 9:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
            break;
        case 7:
        case 12:
        case 14:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
            break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for(index=0;index<=14;index++)

}

/*********************************************************************/
void Get_RTimer1MS_Imm_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[15];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=14;index++){
        a[index]=*code_ptr++;
    }

    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=14;index++){
        while( a[index] > 0 ){
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index ){
        case 0:
            immVal = (long*)(funct_ptr->arg+6);
            *temp_ptr = (*immVal);
            break;
        case 1:
        case 11:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
            break;
        case 2:
        case 3:
        case 13:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
            break;
        case 4:
        case 10:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
            break;
        case 5:
        case 8:
            *temp_ptr=tmrbuf;                       //FTimerBuffer0 /* get timer buffer */
            break;
        case 6:
        case 9:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
            break;
        case 7:
        case 12:
        case 14:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
            break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for(index=0;index<=14;index++)

}
/*********************************************************************/
void Get_RTimer10MS_Reg( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;



    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);


    //Timer0[]                 MOV R0 , #10


    uint32_t registr = (*Reg*4) + s_a0_reg_ofs; //reg

    uint32_t reg_index_offset_low_machine_code = mov_imm(registr,  7);
	write_to_array( reg_index_offset_low_machine_code);    // 低16位

	uint32_t reg_index_offset_high_machine_code = generate_movt_reg_imm(registr, 7);
    write_to_array(reg_index_offset_high_machine_code);   // 高16位
    

    uint32_t ldr_reg_index_offset_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_reg_index_offset_machine_code);

    // mov  [ebx+dd_1],ecx           //SetValue
    uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;
    uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
	write_to_array( dd_1_low_machine_code);    // 低16位

	uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
    write_to_array(dd_1_high_machine_code);   // 高16位
    

    uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_dd_1_offset_machine_code);





	//Timer1[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(62*4 );
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            // beq Timer1_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(18 *4) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_high_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t Index_dd_2 =(*TimerReg*4+8)+s_a0_reg_ofs;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+4;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMBUF1] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF1]        //FTimerBuffer1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_strb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        // uint32_t dd_n_1 = 0x8E00 + (timer_n * 2) + 1;
        uint32_t dd_n_1 =(*TimerReg*4) + s_a0_reg_ofs; //dd_n_1
        //Timer1_3:
            // Load dd_n_1
        
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_ldr_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // bne Timer1_6
        uint32_t  bne_Timer1_6 = generate_branch(27*4,0x1); // offser , branch type  1為bne
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bne_Timer1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  

        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer1
        uint32_t FTimerBuffer1 = tmrbuf+4;
        uint32_t FTimerBuffer1_low = mov_imm(FTimerBuffer1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_high = generate_movt_reg_imm(FTimerBuffer1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer1  mov  ecx,[ebx+TIMBUF1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
    
        //Timer1_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            

        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;

            // jl Timer1_1        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t blt_NowValue_SetValue = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        //write_to_array( dd_6_low);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
        //write_to_array( dd_6_high);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_ldrb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        //write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        //write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        //write_to_array( ldrb_temp_TIMREF1_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_temp_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        //write_to_array( cmp_dd_6_TIMREF1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( cmp_dd_6_TIMREF1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        //write_to_array( bgt_NowValue_SetValue_1_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue_1_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        // Timer1_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // B Timer1_1
        uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (g_timer3_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        // Timer1_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        // Timer1_1



}


/*********************************************************************/
/*********************************************************************/
void Get_RTimer10MS_Reg_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=18;index++){
        a[index]=*code_ptr++;
    }

    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=18;index++){
        while( a[index] > 0 ){
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index ){
        case 0:
            Reg = (long*)(funct_ptr->arg+6);
            if(USR_REG_AREA_START <= *Reg)
            {
                *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
            break;
        case 1:
        case 13:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
            break;
        case 2:
        case 3:
        case 17:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
            break;
        case 4:
        case 12:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
            break;
        case 5:
        case 10:
            *temp_ptr=tmrbuf+4;                       //FTimerBuffer1 /* get timer buffer */
            break;
        case 6:
        case 11:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
            break;
        case 7:
        case 15:
            *temp_ptr=tmrbuf+16;                     //FTimerRef1 /* get timer reference */
            break;
        case 8:
        case 14:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
            break;
        case 9:
        case 16:
        case 18:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
            break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for(index=0;index<=18;index++)
}


/*********************************************************************/



/*********************************************************************/
void Get_RTimer10MS_Imm( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;



    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);


    //Timer0[]                 MOV R0 , #10

        //Timer0[]                 MOV R0 , #10
    
        uint32_t imm_data =*immVal;  // Rtimer
        uint32_t IMM_low_machine_code = mov_imm(imm_data,  10);
        write_to_array(IMM_low_machine_code);    // 低16位
    
        uint32_t IMM_high_machine_code = generate_movt_reg_imm(imm_data, 10);
        write_to_array( IMM_high_machine_code);   // 高16位
    
            
        uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;  // Rtimer
        // mov  [ebx+dd_1],ecx           //SetValue
        uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
        write_to_array( dd_1_low_machine_code);    // 低16位
    
        uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
        write_to_array( dd_1_high_machine_code);   // 高16位
        
    
        uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        write_to_array( str_dd_1_offset_machine_code);
    




	//Timer1[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_cmp >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(62*4 );
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (machine_beq >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
        uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_dd_n_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_r0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mvn_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            // beq Timer1_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(18 *4) ;

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (and_al_cl_beq_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_low_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }    
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (eax_high_0_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t Index_dd_2 =(*TimerReg*4+8)+s_a0_reg_ofs;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+4;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMBUF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMBUF1] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF1]        //FTimerBuffer1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldr_TIMEBUF3_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_3_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
            //  mov  edx,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_strb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        // uint32_t dd_n_1 = 0x8E00 + (timer_n * 2) + 1;
        uint32_t dd_n_1 =(*TimerReg*4) + s_a0_reg_ofs; //dd_n_1
        //Timer1_3:
            // Load dd_n_1
        
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_1_ldr_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_al_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // bne Timer1_6
        uint32_t  bne_Timer1_6 = generate_branch(27*4,0x1); // offser , branch type  1為bne
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bne_Timer1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  

        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer1
        uint32_t FTimerBuffer1 = tmrbuf+4;
        uint32_t FTimerBuffer1_low = mov_imm(FTimerBuffer1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_high = generate_movt_reg_imm(FTimerBuffer1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t FTimerBuffer1_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer1  mov  ecx,[ebx+TIMBUF1]
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (CountValue_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (FTimerBuffer3_sub_CountValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
    
        //Timer1_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_low_machine_code  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_2_str_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_low_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (al_high_0 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    
            

        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_1_ldr_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;

            // jl Timer1_1        
            for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (cmp_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t blt_NowValue_SetValue = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (blt_NowValue_SetValue >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue1_6 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
        //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        //write_to_array( dd_6_low);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        
        //write_to_array( dd_6_high);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_6_ldrb_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        //write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        //write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (Index_TIMREF1_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        //write_to_array( ldrb_temp_TIMREF1_machine_code);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (ldrb_temp_TIMREF1_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        //write_to_array( cmp_dd_6_TIMREF1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = ( cmp_dd_6_TIMREF1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
        //jg   TIMER1_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        //write_to_array( bgt_NowValue_SetValue_1_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (bgt_NowValue_SetValue_1_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
    
    
    
        // Timer1_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0xff_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        //write_to_array( str_al_dd_n_1);
        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1>> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // B Timer1_1
        uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (g_timer3_1 >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        // Timer1_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_low >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_al_0x0_high  >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_low_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (dd_n_high_machine_code >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_low_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (mov_dd_n_1_high_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);

        for (size_t i = 0; i < sizeof(uint32_t); i++) {
            *pc_counter = (str_al_dd_n_1_again >> (i * 8)) & 0xFF; // 提取每個位元組
            pc_counter++;                            // 指向下一個位元組
    }  
    
        // Timer1_1




}


/*********************************************************************/
void Get_RTimer10MS_Imm_x86( char *code_ptr, struct funct *funct_ptr )     /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[19];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;          /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=18;index++){
        a[index]=*code_ptr++;
    }

    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=18;index++){
        while( a[index] > 0 ){
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index ){
        case 0:
            immVal = (long*)(funct_ptr->arg+6);
            *temp_ptr = *immVal;
            break;
        case 1:
        case 13:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
            break;
        case 2:
        case 3:
        case 17:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
            break;
        case 4:
        case 12:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
            break;
        case 5:
        case 10:
            *temp_ptr=tmrbuf+4;                       //FTimerBuffer1 /* get timer buffer */
            break;
        case 6:
        case 11:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
            break;
        case 7:
        case 15:
            *temp_ptr=tmrbuf+16;                     //FTimerRef1 /* get timer reference */
            break;
        case 8:
        case 14:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
            break;
        case 9:
        case 16:
        case 18:
            TimerReg = (long*)(funct_ptr->arg);
            if(USR_REG_AREA_START <= *TimerReg)
            {
                *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                isAccessingUsrRegArea=1;
            }
            *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
            break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for(index=0;index<=18;index++)
}



/*********************************************************************/
void Get_RTimer100MS_Reg( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;


    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    uint32_t registr = (*Reg*4) + s_a0_reg_ofs; //reg

    uint32_t reg_index_offset_low_machine_code = mov_imm(registr,  7);
	write_to_array( reg_index_offset_low_machine_code);    // 低16位

	uint32_t reg_index_offset_high_machine_code = generate_movt_reg_imm(registr, 7);
    write_to_array(reg_index_offset_high_machine_code);   // 高16位
    

    uint32_t ldr_reg_index_offset_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_reg_index_offset_machine_code);

    // mov  [ebx+dd_1],ecx           //SetValue
    uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;
    uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
	write_to_array( dd_1_low_machine_code);    // 低16位

	uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
    write_to_array(dd_1_high_machine_code);   // 高16位
    

    uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_dd_1_offset_machine_code);



	//Timer2[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        write_to_array( machine_cmp);
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(77*4 );
        write_to_array( machine_beq);
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
            uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        write_to_array( dd_n_low_machine_code);    // 低16位
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        write_to_array( dd_n_high_machine_code);   // 高16位
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        write_to_array( ldr_dd_n_machine_code);
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_r0_machine_code);
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        write_to_array( mvn_cl_machine_code);
    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        write_to_array( and_al_cl_machine_code);
    
            // beq Timer2_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(24 *4) ;
        write_to_array( and_al_cl_beq_machine_code);
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        write_to_array( eax_low_0_machine_code );
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        write_to_array(eax_high_0_machine_code);   // 高16位
    
        uint32_t Index_dd_2 = (*TimerReg*4+8)+s_a0_reg_ofs;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        write_to_array( str_dd_2_0_machine_code);
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+8;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        write_to_array( Index_TIMBUF1_low_machine_code);    // 低16位
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        write_to_array( Index_TIMBUF1_high_machine_code);   // 高16位
        
            //  mov  edx,[ebx+TIMBUF2] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF2]        //FTimerBuffer2
        write_to_array( ldr_TIMEBUF3_machine_code);
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( dd_3_low);
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( dd_3_high);
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        write_to_array( dd_3_str_code);
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
        uint32_t Index_TIMREF2 = tmrbuf+17;
        uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
            //movt r7 , TIMREF2 
        uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_TIMREF2_machine_code);
    
    
        //              mov  [ebx+dd_5],dl            //TimerRef2
        uint32_t dd_5 = (*TimerReg*4+17)+s_a0_reg_ofs;
        uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
        write_to_array( dd_5_low);
        uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
        write_to_array( dd_5_high);
        uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
        write_to_array( dd_5_strb_code);
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF1_machine_code);
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        write_to_array( dd_6_low);
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        write_to_array( dd_6_high);
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
        write_to_array( dd_6_strb_code);
    
    
    
    
        //Timer2_3:
            // Load dd_n_1
        uint32_t dd_n_1 =(*TimerReg*4) + s_a0_reg_ofs;
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( dd_n_1_low);
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( dd_n_1_high);
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        write_to_array( dd_n_1_ldr_code);
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        write_to_array( cmp_al_0);
    
            // bne Timer2_6
        uint32_t  bne_Timer2_6 = generate_branch(36*4,0x1); // offser , branch type  1為bne
        write_to_array( bne_Timer2_6);
    
        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer2
        uint32_t FTimerBuffer2 = tmrbuf+8;
        uint32_t FTimerBuffer2_low = mov_imm(FTimerBuffer2, 7); // 設置低16位
        write_to_array( FTimerBuffer2_low);
        uint32_t FTimerBuffer2_high = generate_movt_reg_imm(FTimerBuffer2, 7); // 設置高16位
        write_to_array( FTimerBuffer2_high);
        uint32_t FTimerBuffer2_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF2]
        write_to_array( FTimerBuffer2_ldr_code);
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( CountValue_low);
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( CountValue_high);
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        write_to_array( CountValue_ldr_code);
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        write_to_array( FTimerBuffer3_sub_CountValue);
    
    
    
    
    
        //Timer2_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        write_to_array( dd_2_str_code);
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        write_to_array( al_low_0);
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( al_high_0);
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    

        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        write_to_array( dd_1_low);
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        write_to_array( dd_1_high);
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        write_to_array( dd_1_ldr_code);
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
        write_to_array( cmp_NowValue_SetValue);
            // jl Timer2_1
        uint32_t blt_NowValue_SetValue = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_NowValue_SetValue);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue1_6);
    
    ////////////////////////
    
            //mov  dl,[ebx+dd_5]            //TimerRef2
    
            
    
    
        write_to_array( dd_5_low);
    
        write_to_array( dd_5_high);
        uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
        write_to_array( dd_5_ldrb_code);
            // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
    
    
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
        write_to_array( ldrb_temp_TIMREF2_machine_code);
    
         
        uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_5_TIMREF2);
            // jl Timer2_1
        uint32_t blt_jg_TIMER2_1 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_jg_TIMER2_1);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_jl_TIMER2_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_jl_TIMER2_6);
    
    
    ////////////////////////
            //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        write_to_array( dd_6_low);
    
        write_to_array( dd_6_high);
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        write_to_array( dd_6_ldrb_code);
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_temp_TIMREF1_machine_code);
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_6_TIMREF1);
    
    
        //jg   Timer2_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue_1_1);
    
    
    
    
        // Timer2_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
        write_to_array( mov_al_0xff_low);
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0xff_high);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low);
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high);
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1);
    
            // B Timer2_1
        uint32_t g_timer2_1 = generate_branch(9*4, 0xe); // Always (0xE)
        write_to_array( g_timer2_1);
    
        // Timer2_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
        write_to_array( mov_al_0x0_low);
    
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0x0_high);
    
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
        write_to_array( dd_n_low_machine_code);
    
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
        write_to_array( dd_n_high_machine_code);
    
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low_again);
    
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high_again);
    
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1_again);
    
    
        // Timer2_1

}

/*********************************************************************/
void Get_RTimer100MS_Imm( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;



    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

        //Timer0[]                 MOV R0 , #10

        //Timer0[]                 MOV R0 , #10
    
        uint32_t imm_data =*immVal;  // Rtimer
        uint32_t IMM_low_machine_code = mov_imm(imm_data,  10);
        write_to_array(IMM_low_machine_code);    // 低16位
    
        uint32_t IMM_high_machine_code = generate_movt_reg_imm(imm_data, 10);
        write_to_array( IMM_high_machine_code);   // 高16位
    
            
        uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;  // Rtimer
        // mov  [ebx+dd_1],ecx           //SetValue
        uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
        write_to_array( dd_1_low_machine_code);    // 低16位
    
        uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
        write_to_array( dd_1_high_machine_code);   // 高16位
        
    
        uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        write_to_array( str_dd_1_offset_machine_code);
    
 
	//Timer2[]                 MOV R0 , #10
    	// CMP R5, #0  比對al 跟 0 
        uint32_t machine_cmp = generate_cmp(5, 0);
        write_to_array( machine_cmp);
        
            // BEQ instruction #跳轉指令
        uint32_t machine_beq = encode_beq(77*4 );
        write_to_array( machine_beq);
        
        //mov cl,[ebx+dd_n]
            //mov r7 ,dd_n
    
            uint32_t dd_n = (*TimerReg*4+1) + s_a0_reg_ofs;
        uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
        write_to_array( dd_n_low_machine_code);    // 低16位
            //movt r7 , dd_n
        uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
        write_to_array( dd_n_high_machine_code);   // 高16位
        
            // LDR R4, [R6, R7]  r4 = cl
        uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
        write_to_array( ldr_dd_n_machine_code);
    
        //mov [ebx+dd_n],al
    
            // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
        uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_r0_machine_code);
    
            // not cl
        uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
        write_to_array( mvn_cl_machine_code);
    
            //and al , cl
        uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
        write_to_array( and_al_cl_machine_code);
    
            // beq Timer2_3
        uint32_t and_al_cl_beq_machine_code = encode_beq(24 *4) ;
        write_to_array( and_al_cl_beq_machine_code);
    
            //mov eax , 0
    
        uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
        write_to_array( eax_low_0_machine_code );
            //mov eax , 0
        uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
        write_to_array(eax_high_0_machine_code);   // 高16位
    
        uint32_t Index_dd_2 = (*TimerReg*4+8)+s_a0_reg_ofs;
            //movt r8 , dd_2之低offset
        uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
        
    
            // mov [ebx+dd_2] , eax  
        uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
        write_to_array( str_dd_2_0_machine_code);
    
    
            //  mov edx,[ebx+TIMBUF1] 
        uint32_t Index_TIMBUF1 = tmrbuf+8;
            //mov r7 TIMBUF3
        uint32_t Index_TIMBUF1_low_machine_code = mov_imm(Index_TIMBUF1,  7);
        write_to_array( Index_TIMBUF1_low_machine_code);    // 低16位
            //movt r7 , TIMBUF3 移動A3000之高16位元
        uint32_t Index_TIMBUF1_high_machine_code = generate_movt_reg_imm(Index_TIMBUF1, 7);
        write_to_array( Index_TIMBUF1_high_machine_code);   // 高16位
        
            //  mov  edx,[ebx+TIMBUF2] 
        uint32_t ldr_TIMEBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF2]        //FTimerBuffer2
        write_to_array( ldr_TIMEBUF3_machine_code);
    
    
            // mov  [ebx+dd_3],edx 
        uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
        uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( dd_3_low);
        uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( dd_3_high);
        uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
        write_to_array( dd_3_str_code);
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
        uint32_t Index_TIMREF2 = tmrbuf+17;
        uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
            //movt r7 , TIMREF2 
        uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_TIMREF2_machine_code);
    
    
        //              mov  [ebx+dd_5],dl            //TimerRef2
        uint32_t dd_5 = (*TimerReg*4+17)+s_a0_reg_ofs;
        uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
        write_to_array( dd_5_low);
        uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
        write_to_array( dd_5_high);
        uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
        write_to_array( dd_5_strb_code);
    
    
        //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
            //mov r7 TIMREF1
        uint32_t Index_TIMREF1 = tmrbuf+16;
        uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
            //movt r7 , TIMREF1 
        uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF1] 
        uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array(ldrb_TIMREF1_machine_code);
    
    
        //              mov  [ebx+dd_6],dl            //TimerRef3
        uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
        uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
        write_to_array( dd_6_low);
        uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
        write_to_array( dd_6_high);
        uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
        write_to_array( dd_6_strb_code);
    
    
    
    
        //Timer2_3:
            // Load dd_n_1
        uint32_t dd_n_1 =(*TimerReg*4) + s_a0_reg_ofs;
        uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( dd_n_1_low);
        uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( dd_n_1_high);
        uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
        write_to_array( dd_n_1_ldr_code);
    
            // cmp al , #0
        uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
        write_to_array( cmp_al_0);
    
            // bne Timer2_6
        uint32_t  bne_Timer2_6 = generate_branch(36*4,0x1); // offser , branch type  1為bne
        write_to_array( bne_Timer2_6);
    
        // @ Load FTimerBuffer3 and CountValue
            // Load FTimerBuffer2
        uint32_t FTimerBuffer2 = tmrbuf+8;
        uint32_t FTimerBuffer2_low = mov_imm(FTimerBuffer2, 7); // 設置低16位
        write_to_array( FTimerBuffer2_low);
        uint32_t FTimerBuffer2_high = generate_movt_reg_imm(FTimerBuffer2, 7); // 設置高16位
        write_to_array( FTimerBuffer2_high);
        uint32_t FTimerBuffer2_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF2]
        write_to_array( FTimerBuffer2_ldr_code);
    
    
    
            // Load CountValue dd_3
    
        uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
        write_to_array( CountValue_low);
        uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
        write_to_array( CountValue_high);
        uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
        write_to_array( CountValue_ldr_code);
    
    
            // sub  ecx,eax
        uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
        write_to_array( FTimerBuffer3_sub_CountValue);
    
    
    
    
    
        //Timer2_5:
            //@ Store NowValue
    
            //movt r8 , dd_2之低offset
        dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
        write_to_array( dd_2_low_machine_code);    // 低16位
            //movt r8 , dd_2之高offset
        dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
        write_to_array( dd_2_high_machine_code);   // 高16位
            //mov  [ebx+dd_2],ecx  存入 //NowValue
        uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
        write_to_array( dd_2_str_code);
    
            // al = 0
    
        uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
        write_to_array( al_low_0);
        uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( al_high_0);
    
    
    
            //cmp  ecx,[ebx+dd_1]           //SetValue
    

        uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
        write_to_array( dd_1_low);
        uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
        write_to_array( dd_1_high);
        uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
        write_to_array( dd_1_ldr_code);
    
            // cmp ecx ,  ebx+dd_1
        uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
        write_to_array( cmp_NowValue_SetValue);
            // jl Timer2_1
        uint32_t blt_NowValue_SetValue = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_NowValue_SetValue);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_NowValue_SetValue1_6 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue1_6);
    
    ////////////////////////
    
            //mov  dl,[ebx+dd_5]            //TimerRef2
    
            
    
    
        write_to_array( dd_5_low);
    
        write_to_array( dd_5_high);
        uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
        write_to_array( dd_5_ldrb_code);
            // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
    
        //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
            //mov r7 TIMREF2
    
    
        write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
        
            //  mov  dl,[ebx+TIMREF2] 
        uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
        write_to_array( ldrb_temp_TIMREF2_machine_code);
    
         
        uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_5_TIMREF2);
            // jl Timer2_1
        uint32_t blt_jg_TIMER2_1 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( blt_jg_TIMER2_1);
    
            //jg   Timer2_6                 //超過設定值
    
        uint32_t bgt_jl_TIMER2_6 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_jl_TIMER2_6);
    
    
    ////////////////////////
            //mov  dl,[ebx+dd_6]            //TimerRef3
    
    
        write_to_array( dd_6_low);
    
        write_to_array( dd_6_high);
        uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
        write_to_array( dd_6_ldrb_code);
    
    
    
        //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
    
    
            //mov r7 TIMBUF1
    
    
        write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
    
        write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
        
            //  mov  r7,[ebx+TIMBUF1] 
        uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
        write_to_array( ldrb_temp_TIMREF1_machine_code);
    
    
            //cmp ebx+dd_6,[ebx+TIMREF1] 
        uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
        write_to_array( cmp_dd_6_TIMREF1);
    
    
        //jg   Timer2_1                 //還沒超過最小單位值
    
        uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
        write_to_array( bgt_NowValue_SetValue_1_1);
    
    
    
    
        // Timer2_6
            // MOV AL, #0xFF
        uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
        write_to_array( mov_al_0xff_low);
    
        uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0xff_high);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low);
    
        uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high);
    
        uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1);
    
            // B Timer2_1
        uint32_t g_timer2_1 = generate_branch(9*4, 0xe); // Always (0xE)
        write_to_array( g_timer2_1);
    
        // Timer2_4
            // MOV AL, #0x0
        uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
        write_to_array( mov_al_0x0_low);
    
        uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
        write_to_array( mov_al_0x0_high);
    
            // MOV [ebx+dd_n], AL
        dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
        write_to_array( dd_n_low_machine_code);
    
        dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
        write_to_array( dd_n_high_machine_code);
    
        uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n);
    
            // MOV [ebx+dd_n_1], AL
        uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
        write_to_array( mov_dd_n_1_low_again);
    
        uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
        write_to_array( mov_dd_n_1_high_again);
    
        uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
        write_to_array( str_al_dd_n_1_again);
    
    
        // Timer2_1

}


/*********************************************************************/
/*********************************************************************/
void Get_RTimer100MS_Reg_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *Reg,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=22;index++)
        a[index]=*code_ptr++;

    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=22;index++)
    {
        while( a[index] > 0 )
        {
           *pc_counter++=*code_ptr++;  /* get machine code */
           a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index )
        {
            case 0:
                Reg = (long*)(funct_ptr->arg+6);
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*Reg*4) + s_a0_reg_ofs;
                break;
            case 1:
            case 15:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 21:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 14:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
                break;
            case 5:
            case 12:
                *temp_ptr=tmrbuf+8;           //FTimerBuffer2 /* get timer buffer 2 */
                break;
            case 6:
            case 13:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
                break;
            case 7:
            case 17:
                *temp_ptr=tmrbuf+17;        //FTimerRef2 /* get timer reference 2 */
                break;
            case 8:
            case 16:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+17)+s_a0_reg_ofs;    //TimerRef2 /*get .01 sec update*/
                break;
            case 9:
            case 19:
                *temp_ptr=tmrbuf+16;         //FTimerRef1 /* get timer reference 1 */
                break;
            case 10:
            case 18:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
                break;
            case 11:
            case 20:
            case 22:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
                break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for loop
}

/*********************************************************************/
void Get_RTimer100MS_Imm_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[23];
    long int j,k;
    long *immVal,*TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=22;index++)
        a[index]=*code_ptr++;

    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=22;index++)
    {
        while( a[index] > 0 )
        {
           *pc_counter++=*code_ptr++;  /* get machine code */
           a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index )
        {
            case 0:
                immVal = (long*)(funct_ptr->arg+6);
                *temp_ptr = *immVal;
                break;
            case 1:
            case 15:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 21:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 14:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
                break;
            case 5:
            case 12:
                *temp_ptr=tmrbuf+8;           //FTimerBuffer2 /* get timer buffer 2 */
                break;
            case 6:
            case 13:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
                break;
            case 7:
            case 17:
                *temp_ptr=tmrbuf+17;        //FTimerRef2 /* get timer reference 2 */
                break;
            case 8:
            case 16:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+17)+s_a0_reg_ofs;    //TimerRef2 /*get .01 sec update*/
                break;
            case 9:
            case 19:
                *temp_ptr=tmrbuf+16;         //FTimerRef1 /* get timer reference 1 */
                break;
            case 10:
            case 18:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
                break;
            case 11:
            case 20:
            case 22:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
                break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }//for loop
}



/*********************************************************************/
void Get_RTimer1S_Reg( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *Reg, *TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;


    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);


    uint32_t registr = (*Reg*4) + s_a0_reg_ofs; //reg

    uint32_t reg_index_offset_low_machine_code = mov_imm(registr,  7);
	write_to_array( reg_index_offset_low_machine_code);    // 低16位

	uint32_t reg_index_offset_high_machine_code = generate_movt_reg_imm(registr, 7);
    write_to_array(reg_index_offset_high_machine_code);   // 高16位
    

    uint32_t ldr_reg_index_offset_machine_code = generate_ldr_reg_regoffset(10, 6, 7, 0);
    write_to_array(ldr_reg_index_offset_machine_code);

    // mov  [ebx+dd_1],ecx           //SetValue
    uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;
    uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
	write_to_array( dd_1_low_machine_code);    // 低16位

	uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
    write_to_array(dd_1_high_machine_code);   // 高16位
    

    uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
    write_to_array(str_dd_1_offset_machine_code);



   	// CMP R5, #0  比對al 跟 0 
       uint32_t machine_cmp = generate_cmp(5, 0);
       write_to_array( machine_cmp);
       
           // BEQ instruction #跳轉指令
       uint32_t machine_beq = encode_beq(92*4 );
       write_to_array( machine_beq);
       
       //mov cl,[ebx+dd_n]
           //mov r7 ,dd_n
   
       uint32_t dd_n =(*TimerReg*4+1) + s_a0_reg_ofs;
       uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
       write_to_array( dd_n_low_machine_code);    // 低16位
           //movt r7 , dd_n
       uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
       write_to_array( dd_n_high_machine_code);   // 高16位
       
           // LDR R4, [R6, R7]  r4 = cl
       uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
       write_to_array( ldr_dd_n_machine_code);
   
       //mov [ebx+dd_n],al
   
           // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
       uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_r0_machine_code);
   
           // not cl
       uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
       write_to_array( mvn_cl_machine_code);
   
           //and al , cl
       uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
       write_to_array( and_al_cl_machine_code);
   
           // beq Timer3_3
       uint32_t and_al_cl_beq_machine_code = encode_beq(30 *4) ;
       write_to_array( and_al_cl_beq_machine_code);
   
           //mov eax , 0
   
       uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
       write_to_array( eax_low_0_machine_code );
           //mov eax , 0
       uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
       write_to_array(eax_high_0_machine_code);   // 高16位
   
       uint32_t Index_dd_2 = (*TimerReg*4+8)+s_a0_reg_ofs;
           //movt r8 , dd_2之低offset
       uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
       write_to_array( dd_2_low_machine_code);    // 低16位
           //movt r8 , dd_2之高offset
       uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
       write_to_array( dd_2_high_machine_code);   // 高16位
       
   
           // mov [ebx+dd_2] , eax  
       uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
       write_to_array( str_dd_2_0_machine_code);
   
   
           //  mov edx,[ebx+TIMBUF3] 
       uint32_t Index_TIMBUF3 = tmrbuf + 12 ;
           //mov r7 TIMBUF3
       uint32_t Index_TIMBUF3_low_machine_code = mov_imm(Index_TIMBUF3,  7);
       write_to_array( Index_TIMBUF3_low_machine_code);    // 低16位
           //movt r7 , TIMBUF3 移動A3000之高16位元
       uint32_t Index_TIMBUF3_high_machine_code = generate_movt_reg_imm(Index_TIMBUF3, 7);
       write_to_array( Index_TIMBUF3_high_machine_code);   // 高16位
       
           //  mov  edx,[ebx+TIMBUF3] 
       uint32_t ldr_TIMBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF3]        //FTimerBuffer2
       write_to_array(ldr_TIMBUF3_machine_code);
   
   
           // mov  [ebx+dd_3],edx 
       uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
       uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
       write_to_array( dd_3_low);
       uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
       write_to_array( dd_3_high);
       uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
       write_to_array( dd_3_str_code);
   ///////////////////////////////////////////////////
   
   
   
       //              mov  dl,[ebx+TIMREF3]          //FTimerRef2
           //mov r7 TIMREF3
       uint32_t Index_TIMREF3 = tmrbuf + 18 ;
       uint32_t Index_TIMREF3_low_machine_code = mov_imm(Index_TIMREF3,  7);
       write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
           //movt r7 , TIMREF3 
       uint32_t Index_TIMREF3_high_machine_code = generate_movt_reg_imm(Index_TIMREF3, 7);
       write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF3] 
       uint32_t ldrb_TIMREF3_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF3_machine_code);
   
   
       //              mov  [ebx+dd_4],dl            //TimerRef2
       uint32_t dd_4 = (*TimerReg*4+16)+s_a0_reg_ofs;
       uint32_t dd_4_low = mov_imm(dd_4, 7); // 設置低16位
       write_to_array( dd_4_low);
       uint32_t dd_4_high = generate_movt_reg_imm(dd_4, 7); // 設置高16位
       write_to_array( dd_4_high);
       uint32_t dd_4_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_4],dl  
       write_to_array( dd_4_strb_code);
   
   
   
   ///////////////////////////////////////////////////
       //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
           //mov r7 TIMREF2
       uint32_t Index_TIMREF2 = tmrbuf + 17;
       uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
       write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
           //movt r7 , TIMREF2 
       uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
       write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF2] 
       uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF2_machine_code);
   
   
       //              mov  [ebx+dd_5],dl            //TimerRef2
       uint32_t dd_5 =(*TimerReg*4+17)+s_a0_reg_ofs;
       uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
       write_to_array( dd_5_low);
       uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
       write_to_array( dd_5_high);
       uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
       write_to_array( dd_5_strb_code);
   
   
       //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
           //mov r7 TIMREF1
       uint32_t Index_TIMREF1 = tmrbuf + 16 ;
       uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
       write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
           //movt r7 , TIMREF1 
       uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
       write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF1] 
       uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF1_machine_code);
   
   
       //              mov  [ebx+dd_6],dl            //TimerRef3
       uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
       uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
       write_to_array( dd_6_low);
       uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
       write_to_array( dd_6_high);
       uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
       write_to_array( dd_6_strb_code);
   
   
   
   
       //Timer3_3:
           // Load dd_n_1
       uint32_t dd_n_1 = (*TimerReg*4)+s_a0_reg_ofs;
       uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( dd_n_1_low);
       uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( dd_n_1_high);
       uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
       write_to_array( dd_n_1_ldr_code);
   
           // cmp al , #0
       uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
       write_to_array( cmp_al_0);
   
           // bne Timer3_6
       uint32_t  bne_Timer3_6 = generate_branch(45*4,0x1); // offser , branch type  1為bne
       write_to_array( bne_Timer3_6);
   
       // @ Load FTimerBuffer3 and CountValue
           // Load FTimerBuffer3
       uint32_t FTimerBuffer3 = tmrbuf + 12 ;
       uint32_t FTimerBuffer3_low = mov_imm(FTimerBuffer3, 7); // 設置低16位
       write_to_array( FTimerBuffer3_low);
       uint32_t FTimerBuffer3_high = generate_movt_reg_imm(FTimerBuffer3, 7); // 設置高16位
       write_to_array( FTimerBuffer3_high);
       uint32_t FTimerBuffer3_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF3]
       write_to_array( FTimerBuffer3_ldr_code);
   
   
   
           // Load CountValue dd_3
   
       uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
       write_to_array( CountValue_low);
       uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
       write_to_array( CountValue_high);
       uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
       write_to_array( CountValue_ldr_code);
   
   
           // sub  ecx,eax
       uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
       write_to_array( FTimerBuffer3_sub_CountValue);
   
   
   
   
   
       //Timer3_5:
           //@ Store NowValue
   
           //movt r8 , dd_2之低offset
       dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
       write_to_array( dd_2_low_machine_code);    // 低16位
           //movt r8 , dd_2之高offset
       dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
       write_to_array( dd_2_high_machine_code);   // 高16位
           //mov  [ebx+dd_2],ecx  存入 //NowValue
       uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
       write_to_array( dd_2_str_code);
   
           // al = 0
   
       uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
       write_to_array( al_low_0);
       uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( al_high_0);
   
   
   
           //cmp  ecx,[ebx+dd_1]           //SetValue
   
           

       uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
       write_to_array( dd_1_low);
       uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
       write_to_array( dd_1_high);
       uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
       write_to_array( dd_1_ldr_code);
   
           // cmp ecx ,  ebx+dd_1
       uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
       write_to_array( cmp_NowValue_SetValue);
           // jl Timer3_1
       uint32_t blt_NowValue_SetValue = generate_branch(42*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_NowValue_SetValue);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_NowValue_SetValue1_6 = generate_branch(27*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_NowValue_SetValue1_6);
   
   //////////////////////////////////////////////
   
           //mov  dl,[ebx+dd_4]            //TimerRef3
   
           
   
   
       write_to_array( dd_4_low);
   
       write_to_array( dd_4_high);
       uint32_t dd_4_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_4]            //TimerRef3  
       write_to_array( dd_4_ldrb_code);
           // cmp  dl,[ebx+TIMREF3]         //FTimerRef3
   
       //              mov  dl,[ebx+TIMREF3]          //FTimerRef3
           //mov r7 TIMREF3
   
   
       write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF3] 
       uint32_t ldrb_temp_TIMREF3_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF3]        //FTimerRef3
       write_to_array(ldrb_temp_TIMREF3_machine_code);
   
        
       uint32_t cmp_dd_4_TIMREF3 = generate_cmp_reg(4,7)  ;
       write_to_array( cmp_dd_4_TIMREF3);
           // jl Timer3_1
       uint32_t blt_jl_TIMREF3 = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_jl_TIMREF3);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_jg_TIMREF3 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_jg_TIMREF3);
   
   
   
   //////////////////////////////////////////////
   ////////////////////////
   
           //mov  dl,[ebx+dd_5]            //TimerRef2
   
           
   
   
       write_to_array( dd_5_low);
   
       write_to_array( dd_5_high);
       uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
       write_to_array( dd_5_ldrb_code);
           // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
   
       //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
           //mov r7 TIMREF2
   
   
       write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF2] 
       uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
       write_to_array(ldrb_temp_TIMREF2_machine_code);
   
        
       uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
       write_to_array( cmp_dd_5_TIMREF2);
           // jl Timer3_1
       uint32_t blt_jl_TIMREF2 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_jl_TIMREF2);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_jg_TIMREF2 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_jg_TIMREF2);
   
   
   ////////////////////////
           //mov  dl,[ebx+dd_6]            //TimerRef3
   
   
       write_to_array( dd_6_low);
   
       write_to_array( dd_6_high);
       uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
       write_to_array( dd_6_ldrb_code);
   
   
   
       //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
   
   
           //mov r7 TIMBUF3
   
   
       write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
       
           //  mov  r7,[ebx+TIMBUF3] 
       uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_temp_TIMREF1_machine_code);
   
   
           //cmp ebx+dd_6,[ebx+TIMREF1] 
       uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
       write_to_array(cmp_dd_6_TIMREF1);
   
   
       //jg   Timer3_1                 //還沒超過最小單位值
   
       uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_NowValue_SetValue_1_1);
   
   
   
   
       // Timer3_6
           // MOV AL, #0xFF
       uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
       write_to_array( mov_al_0xff_low);
   
       uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( mov_al_0xff_high);
   
           // MOV [ebx+dd_n_1], AL
       uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( mov_dd_n_1_low);
   
       uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( mov_dd_n_1_high);
   
       uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n_1);
   
           // B Timer3_1
       uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)
       write_to_array( g_timer3_1);
   
       // Timer3_4
           // MOV AL, #0x0
       uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
       write_to_array( mov_al_0x0_low);
   
       uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( mov_al_0x0_high);
   
           // MOV [ebx+dd_n], AL
       dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
       write_to_array( dd_n_low_machine_code);
   
       dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
       write_to_array( dd_n_high_machine_code);
   
       uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n);
   
           // MOV [ebx+dd_n_1], AL
       uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( mov_dd_n_1_low_again);
   
       uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( mov_dd_n_1_high_again);
   
       uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n_1_again);
   
   
       // Timer3_1
}

/*********************************************************************/
void Get_RTimer1S_Imm( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    // 2025 armv7
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *immVal, *TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    

    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);


    
        //Timer0[]                 MOV R0 , #10

        //Timer0[]                 MOV R0 , #10
    
        uint32_t imm_data =*immVal;  // Rtimer
        uint32_t IMM_low_machine_code = mov_imm(imm_data,  10);
        write_to_array(IMM_low_machine_code);    // 低16位
    
        uint32_t IMM_high_machine_code = generate_movt_reg_imm(imm_data, 10);
        write_to_array( IMM_high_machine_code);   // 高16位
    
            
        uint32_t dd_1 = (*TimerReg*4+4) + s_a0_reg_ofs;  // Rtimer
        // mov  [ebx+dd_1],ecx           //SetValue
        uint32_t dd_1_low_machine_code = mov_imm(dd_1,  7);
        write_to_array( dd_1_low_machine_code);    // 低16位
    
        uint32_t dd_1_high_machine_code = generate_movt_reg_imm(dd_1, 7);
        write_to_array( dd_1_high_machine_code);   // 高16位
        
    
        uint32_t str_dd_1_offset_machine_code = generate_str_reg_regoffset(10, 6, 7, 0);
        write_to_array( str_dd_1_offset_machine_code);
    

   	// CMP R5, #0  比對al 跟 0 
       uint32_t machine_cmp = generate_cmp(5, 0);
       write_to_array( machine_cmp);
       
           // BEQ instruction #跳轉指令
       uint32_t machine_beq = encode_beq(92*4 );
       write_to_array( machine_beq);
       
       //mov cl,[ebx+dd_n]
           //mov r7 ,dd_n
   
       uint32_t dd_n =(*TimerReg*4+1) + s_a0_reg_ofs;
       uint32_t dd_n_low_machine_code = mov_imm(dd_n,  7);
       write_to_array( dd_n_low_machine_code);    // 低16位
           //movt r7 , dd_n
       uint32_t dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7);
       write_to_array( dd_n_high_machine_code);   // 高16位
       
           // LDR R4, [R6, R7]  r4 = cl
       uint32_t ldr_dd_n_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);
       write_to_array( ldr_dd_n_machine_code);
   
       //mov [ebx+dd_n],al
   
           // STR R5, [R6, R7]   R8為輸入值 此為#10 R6為陣列起始位置  R7為R0之offset
       uint32_t str_r0_machine_code= generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_r0_machine_code);
   
           // not cl
       uint32_t mvn_cl_machine_code= generate_mvn_reg(4,4);
       write_to_array( mvn_cl_machine_code);
   
           //and al , cl
       uint32_t and_al_cl_machine_code= generate_and_reg(5,4,5 , 1)  ;  // setflag == 1 因要執行beq , bne 
       write_to_array( and_al_cl_machine_code);
   
           // beq Timer3_3
       uint32_t and_al_cl_beq_machine_code = encode_beq(30 *4) ;
       write_to_array( and_al_cl_beq_machine_code);
   
           //mov eax , 0
   
       uint32_t eax_low_0_machine_code = generate_and_imm(9,9,0);
       write_to_array( eax_low_0_machine_code );
           //mov eax , 0
       uint32_t eax_high_0_machine_code = generate_movt_reg_imm(9, 7);
       write_to_array(eax_high_0_machine_code);   // 高16位
   
       uint32_t Index_dd_2 = (*TimerReg*4+8)+s_a0_reg_ofs;
           //movt r8 , dd_2之低offset
       uint32_t dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
       write_to_array( dd_2_low_machine_code);    // 低16位
           //movt r8 , dd_2之高offset
       uint32_t dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
       write_to_array( dd_2_high_machine_code);   // 高16位
       
   
           // mov [ebx+dd_2] , eax  
       uint32_t str_dd_2_0_machine_code = generate_str_reg_regoffset(9, 6, 7, 0);
       write_to_array( str_dd_2_0_machine_code);
   
   
           //  mov edx,[ebx+TIMBUF3] 
       uint32_t Index_TIMBUF3 = tmrbuf + 12 ;
           //mov r7 TIMBUF3
       uint32_t Index_TIMBUF3_low_machine_code = mov_imm(Index_TIMBUF3,  7);
       write_to_array( Index_TIMBUF3_low_machine_code);    // 低16位
           //movt r7 , TIMBUF3 移動A3000之高16位元
       uint32_t Index_TIMBUF3_high_machine_code = generate_movt_reg_imm(Index_TIMBUF3, 7);
       write_to_array( Index_TIMBUF3_high_machine_code);   // 高16位
       
           //  mov  edx,[ebx+TIMBUF3] 
       uint32_t ldr_TIMBUF3_machine_code = generate_ldr_reg_regoffset(9, 6, 7, 0); // mov  edx,[ebx+TIMBUF3]        //FTimerBuffer2
       write_to_array(ldr_TIMBUF3_machine_code);
   
   
           // mov  [ebx+dd_3],edx 
       uint32_t dd_3 = (*TimerReg*4+12)+s_a0_reg_ofs;
       uint32_t dd_3_low = mov_imm(dd_3, 7); // 設置低16位
       write_to_array( dd_3_low);
       uint32_t dd_3_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
       write_to_array( dd_3_high);
       uint32_t dd_3_str_code = generate_str_reg_regoffset(9, 6, 7, 0);  //CountValue  mov  [ebx+dd_3],edx   
       write_to_array( dd_3_str_code);
   ///////////////////////////////////////////////////
   
   
   
       //              mov  dl,[ebx+TIMREF3]          //FTimerRef2
           //mov r7 TIMREF3
       uint32_t Index_TIMREF3 = tmrbuf + 18 ;
       uint32_t Index_TIMREF3_low_machine_code = mov_imm(Index_TIMREF3,  7);
       write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
           //movt r7 , TIMREF3 
       uint32_t Index_TIMREF3_high_machine_code = generate_movt_reg_imm(Index_TIMREF3, 7);
       write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF3] 
       uint32_t ldrb_TIMREF3_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF3_machine_code);
   
   
       //              mov  [ebx+dd_4],dl            //TimerRef2
       uint32_t dd_4 = (*TimerReg*4+16)+s_a0_reg_ofs;
       uint32_t dd_4_low = mov_imm(dd_4, 7); // 設置低16位
       write_to_array( dd_4_low);
       uint32_t dd_4_high = generate_movt_reg_imm(dd_4, 7); // 設置高16位
       write_to_array( dd_4_high);
       uint32_t dd_4_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_4],dl  
       write_to_array( dd_4_strb_code);
   
   
   
   ///////////////////////////////////////////////////
       //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
           //mov r7 TIMREF2
       uint32_t Index_TIMREF2 = tmrbuf + 17;
       uint32_t Index_TIMREF2_low_machine_code = mov_imm(Index_TIMREF2,  7);
       write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
           //movt r7 , TIMREF2 
       uint32_t Index_TIMREF2_high_machine_code = generate_movt_reg_imm(Index_TIMREF2, 7);
       write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF2] 
       uint32_t ldrb_TIMREF2_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF2_machine_code);
   
   
       //              mov  [ebx+dd_5],dl            //TimerRef2
       uint32_t dd_5 =(*TimerReg*4+17)+s_a0_reg_ofs;
       uint32_t dd_5_low = mov_imm(dd_5, 7); // 設置低16位
       write_to_array( dd_5_low);
       uint32_t dd_5_high = generate_movt_reg_imm(dd_5, 7); // 設置高16位
       write_to_array( dd_5_high);
       uint32_t dd_5_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  // mov  [ebx+dd_5],dl  
       write_to_array( dd_5_strb_code);
   
   
       //              mov  dl,[ebx+TIMREF1]         //FTimerRef1
           //mov r7 TIMREF1
       uint32_t Index_TIMREF1 = tmrbuf + 16 ;
       uint32_t Index_TIMREF1_low_machine_code = mov_imm(Index_TIMREF1,  7);
       write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
           //movt r7 , TIMREF1 
       uint32_t Index_TIMREF1_high_machine_code = generate_movt_reg_imm(Index_TIMREF1, 7);
       write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF1] 
       uint32_t ldrb_TIMREF1_machine_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_TIMREF1_machine_code);
   
   
       //              mov  [ebx+dd_6],dl            //TimerRef3
       uint32_t dd_6 = (*TimerReg*4+18)+s_a0_reg_ofs;
       uint32_t dd_6_low = mov_imm(dd_6, 7); // 設置低16位
       write_to_array( dd_6_low);
       uint32_t dd_6_high = generate_movt_reg_imm(dd_6, 7); // 設置高16位
       write_to_array( dd_6_high);
       uint32_t dd_6_strb_code = generate_strb_reg_regoffset(4, 6, 7, 0);  //mov  [ebx+dd_6],dl  
       write_to_array( dd_6_strb_code);
   
   
   
   
       //Timer3_3:
           // Load dd_n_1
       uint32_t dd_n_1 = (*TimerReg*4)+s_a0_reg_ofs;
       uint32_t dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( dd_n_1_low);
       uint32_t dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( dd_n_1_high);
       uint32_t dd_n_1_ldr_code = generate_ldrb_reg_regoffset(5, 6, 7, 0); //mov al, [ebx+dd_n_1]
       write_to_array( dd_n_1_ldr_code);
   
           // cmp al , #0
       uint32_t cmp_al_0 = generate_cmp(5,0); // 設置低16位
       write_to_array( cmp_al_0);
   
           // bne Timer3_6
       uint32_t  bne_Timer3_6 = generate_branch(45*4,0x1); // offser , branch type  1為bne
       write_to_array( bne_Timer3_6);
   
       // @ Load FTimerBuffer3 and CountValue
           // Load FTimerBuffer3
       uint32_t FTimerBuffer3 = tmrbuf + 12 ;
       uint32_t FTimerBuffer3_low = mov_imm(FTimerBuffer3, 7); // 設置低16位
       write_to_array( FTimerBuffer3_low);
       uint32_t FTimerBuffer3_high = generate_movt_reg_imm(FTimerBuffer3, 7); // 設置高16位
       write_to_array( FTimerBuffer3_high);
       uint32_t FTimerBuffer3_ldr_code = generate_ldr_reg_regoffset(10, 6, 7, 0); //FTimerBuffer2  mov  ecx,[ebx+TIMBUF3]
       write_to_array( FTimerBuffer3_ldr_code);
   
   
   
           // Load CountValue dd_3
   
       uint32_t CountValue_low = mov_imm(dd_3, 7); // 設置低16位
       write_to_array( CountValue_low);
       uint32_t CountValue_high = generate_movt_reg_imm(dd_3, 7); // 設置高16位
       write_to_array( CountValue_high);
       uint32_t CountValue_ldr_code = generate_ldr_reg_regoffset(9, 6, 7, 0); //TimerRef3  eax,[ebx+dd_3]  
       write_to_array( CountValue_ldr_code);
   
   
           // sub  ecx,eax
       uint32_t FTimerBuffer3_sub_CountValue = sub_reg_reg(10,10,9,0) ;
       write_to_array( FTimerBuffer3_sub_CountValue);
   
   
   
   
   
       //Timer3_5:
           //@ Store NowValue
   
           //movt r8 , dd_2之低offset
       dd_2_low_machine_code = mov_imm(Index_dd_2,  7);
       write_to_array( dd_2_low_machine_code);    // 低16位
           //movt r8 , dd_2之高offset
       dd_2_high_machine_code = generate_movt_reg_imm(Index_dd_2, 7);
       write_to_array( dd_2_high_machine_code);   // 高16位
           //mov  [ebx+dd_2],ecx  存入 //NowValue
       uint32_t dd_2_str_code = generate_str_reg_regoffset(10, 6, 7, 0); //TimerRef3  mov  [ebx+dd_2],ecx 
       write_to_array( dd_2_str_code);
   
           // al = 0
   
       uint32_t al_low_0 = mov_imm(0, 5); // 設置低16位
       write_to_array( al_low_0);
       uint32_t al_high_0 = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( al_high_0);
   
   
   
           //cmp  ecx,[ebx+dd_1]           //SetValue
   
           

       uint32_t dd_1_low = mov_imm(dd_1, 7); // 設置低16位
       write_to_array( dd_1_low);
       uint32_t dd_1_high = generate_movt_reg_imm(dd_1, 7); // 設置高16位
       write_to_array( dd_1_high);
       uint32_t dd_1_ldr_code = generate_ldr_reg_regoffset(7, 6, 7, 0); //TimerRef3  mov  ecx,[ebx+dd_1]  
       write_to_array( dd_1_ldr_code);
   
           // cmp ecx ,  ebx+dd_1
       uint32_t cmp_NowValue_SetValue = generate_cmp_reg(10,7)  ;
       write_to_array( cmp_NowValue_SetValue);
           // jl Timer3_1
       uint32_t blt_NowValue_SetValue = generate_branch(42*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_NowValue_SetValue);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_NowValue_SetValue1_6 = generate_branch(27*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_NowValue_SetValue1_6);
   
   //////////////////////////////////////////////
   
           //mov  dl,[ebx+dd_4]            //TimerRef3
   
           
   
   
       write_to_array( dd_4_low);
   
       write_to_array( dd_4_high);
       uint32_t dd_4_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_4]            //TimerRef3  
       write_to_array( dd_4_ldrb_code);
           // cmp  dl,[ebx+TIMREF3]         //FTimerRef3
   
       //              mov  dl,[ebx+TIMREF3]          //FTimerRef3
           //mov r7 TIMREF3
   
   
       write_to_array( Index_TIMREF3_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF3_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF3] 
       uint32_t ldrb_temp_TIMREF3_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF3]        //FTimerRef3
       write_to_array(ldrb_temp_TIMREF3_machine_code);
   
        
       uint32_t cmp_dd_4_TIMREF3 = generate_cmp_reg(4,7)  ;
       write_to_array( cmp_dd_4_TIMREF3);
           // jl Timer3_1
       uint32_t blt_jl_TIMREF3 = generate_branch(33*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_jl_TIMREF3);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_jg_TIMREF3 = generate_branch(18*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_jg_TIMREF3);
   
   
   
   //////////////////////////////////////////////
   ////////////////////////
   
           //mov  dl,[ebx+dd_5]            //TimerRef2
   
           
   
   
       write_to_array( dd_5_low);
   
       write_to_array( dd_5_high);
       uint32_t dd_5_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0); //mov  dl,[ebx+dd_5]            //TimerRef2  
       write_to_array( dd_5_ldrb_code);
           // cmp  dl,[ebx+TIMREF2]         //FTimerRef2
   
       //              mov  dl,[ebx+TIMREF2]          //FTimerRef2
           //mov r7 TIMREF2
   
   
       write_to_array( Index_TIMREF2_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF2_high_machine_code);   // 高16位
       
           //  mov  dl,[ebx+TIMREF2] 
       uint32_t ldrb_temp_TIMREF2_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  r7,[ebx+TIMREF2]        //FTimerRef2
       write_to_array(ldrb_temp_TIMREF2_machine_code);
   
        
       uint32_t cmp_dd_5_TIMREF2 = generate_cmp_reg(4,7)  ;
       write_to_array( cmp_dd_5_TIMREF2);
           // jl Timer3_1
       uint32_t blt_jl_TIMREF2 = generate_branch(24*4,0xb)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( blt_jl_TIMREF2);
   
           //jg   Timer3_6                 //超過設定值
   
       uint32_t bgt_jg_TIMREF2 = generate_branch(9*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_jg_TIMREF2);
   
   
   ////////////////////////
           //mov  dl,[ebx+dd_6]            //TimerRef3
   
   
       write_to_array( dd_6_low);
   
       write_to_array( dd_6_high);
       uint32_t dd_6_ldrb_code = generate_ldrb_reg_regoffset(4, 6, 7, 0);  //CountValue  mov  [ebx+dd_6],edx   
       write_to_array( dd_6_ldrb_code);
   
   
   
       //cmp  dl,[ebx+TIMREF1]         //FTimerRef1
   
   
           //mov r7 TIMBUF3
   
   
       write_to_array( Index_TIMREF1_low_machine_code);    // 低16位
   
       write_to_array( Index_TIMREF1_high_machine_code);   // 高16位
       
           //  mov  r7,[ebx+TIMBUF3] 
       uint32_t ldrb_temp_TIMREF1_machine_code = generate_ldrb_reg_regoffset(7, 6, 7, 0); // mov  dl,[ebx+TIMREF1]        //FTimerREF1
       write_to_array(ldrb_temp_TIMREF1_machine_code);
   
   
           //cmp ebx+dd_6,[ebx+TIMREF1] 
       uint32_t cmp_dd_6_TIMREF1 = generate_cmp_reg(4,7)  ;
       write_to_array(cmp_dd_6_TIMREF1);
   
   
       //jg   Timer3_1                 //還沒超過最小單位值
   
       uint32_t bgt_NowValue_SetValue_1_1 = generate_branch(15*4,0xC)   ;// Greater than 0xC(bgt)  //  Less Than 0xb(blt)  //Branch Not Equal 0X1  bnt
       write_to_array( bgt_NowValue_SetValue_1_1);
   
   
   
   
       // Timer3_6
           // MOV AL, #0xFF
       uint32_t mov_al_0xff_low = mov_imm(0xff, 5); // 設置低16位
       write_to_array( mov_al_0xff_low);
   
       uint32_t mov_al_0xff_high = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( mov_al_0xff_high);
   
           // MOV [ebx+dd_n_1], AL
       uint32_t mov_dd_n_1_low = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( mov_dd_n_1_low);
   
       uint32_t mov_dd_n_1_high = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( mov_dd_n_1_high);
   
       uint32_t str_al_dd_n_1 = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n_1);
   
           // B Timer3_1
       uint32_t g_timer3_1 = generate_branch(9*4, 0xe); // Always (0xE)
       write_to_array( g_timer3_1);
   
       // Timer3_4
           // MOV AL, #0x0
       uint32_t mov_al_0x0_low = mov_imm(0, 5); // 設置低16位
       write_to_array( mov_al_0x0_low);
   
       uint32_t mov_al_0x0_high = generate_movt_reg_imm(0, 5); // 設置高16位
       write_to_array( mov_al_0x0_high);
   
           // MOV [ebx+dd_n], AL
       dd_n_low_machine_code = mov_imm(dd_n, 7); // 設置低16位
       write_to_array( dd_n_low_machine_code);
   
       dd_n_high_machine_code = generate_movt_reg_imm(dd_n, 7); // 設置高16位
       write_to_array( dd_n_high_machine_code);
   
       uint32_t str_al_dd_n = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n);
   
           // MOV [ebx+dd_n_1], AL
       uint32_t mov_dd_n_1_low_again = mov_imm(dd_n_1, 7); // 設置低16位
       write_to_array( mov_dd_n_1_low_again);
   
       uint32_t mov_dd_n_1_high_again = generate_movt_reg_imm(dd_n_1, 7); // 設置高16位
       write_to_array( mov_dd_n_1_high_again);
   
       uint32_t str_al_dd_n_1_again = generate_strb_reg_regoffset(5, 6, 7, 0);
       write_to_array( str_al_dd_n_1_again);
   
   
       // Timer3_1
}

/*********************************************************************/
void Get_RTimer1S_Reg_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *Reg, *TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=26;index++)
        a[index]=*code_ptr++;

    Reg = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=26;index++)
    {
        while( a[index] > 0 )
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index )
        {
            case 0:
                Reg = (long*)(funct_ptr->arg+6);
                if(USR_REG_AREA_START <= *Reg)
                {
                    *Reg=*Reg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*Reg*4) + s_a0_reg_ofs;  //取Register值
                break;
            case 1:
            case 17:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 25:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 16:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
                break;
            case 5:
            case 14:
                *temp_ptr=tmrbuf+12;                  //FTimerBuffer3 /* get timer buffer 3 */
                break;
            case 6:
            case 15:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
                break;
            case 7:
            case 19:
                *temp_ptr=tmrbuf+18;                  //FTimerRef3 /* get timer reference 3 */
                break;
            case 8:
            case 18:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+16)+s_a0_reg_ofs;    //TimerRef1 /*get .1 sec update*/
                break;
            case 9:
            case 21:
                *temp_ptr=tmrbuf+17;                  //FTimerRef2 /* get timer reference 2 */
                break;
            case 10:
            case 20:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+17)+s_a0_reg_ofs;    //TimerRef2 /*get .01 sec update*/
                break;
            case 11:
            case 23:
                *temp_ptr=tmrbuf+16;                  //FTimerRef1 /* get timer reference 1 */
                break;
            case 12:
            case 22:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
                break;
            case 13:
            case 24:
            case 26:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
                break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }// for loop
}

/*********************************************************************/
void Get_RTimer1S_Imm_x86( char *code_ptr, struct funct *funct_ptr )   /* get machine - two argument */
{
    NUM_TYPE *temp_ptr;
    char index,a[27];
    long int j,k;
    long *immVal, *TimerReg;
    long s_a0_reg_ofs;
    long tmrbuf;

    j=(long)s_mlc_reg;           /* MLC_Data + 0x2400 */
    k=(long)MLC_Data;
    s_a0_reg_ofs=j-k;


    j=(long)TIMBUF0;            /* s_a1_ref + 0x2800 */
    tmrbuf=j-k;

    for(index=0;index<=26;index++)
        a[index]=*code_ptr++;

    immVal = (long*)(funct_ptr->arg+6);
    TimerReg = (long*)(funct_ptr->arg);

    for(index=0;index<=26;index++)
    {
        while( a[index] > 0 )
        {
            *pc_counter++=*code_ptr++;  /* get machine code */
            a[index]--;
        }

        temp_ptr = (NUM_TYPE *)pc_counter;
        switch( index )
        {
            case 0:
                immVal = (long*)(funct_ptr->arg+6);
                *temp_ptr = *immVal;  //取Immediate值
                break;
            case 1:
            case 17:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr = (*TimerReg*4+4) + s_a0_reg_ofs;  //SetValue
                break;
            case 2:
            case 3:
            case 25:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+1) + s_a0_reg_ofs;   //TimerStatus /*get one shot buff*/
                break;
            case 4:
            case 16:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+8)+s_a0_reg_ofs;    //NowValue
                break;
            case 5:
            case 14:
                *temp_ptr=tmrbuf+12;                  //FTimerBuffer3 /* get timer buffer 3 */
                break;
            case 6:
            case 15:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+12)+s_a0_reg_ofs;    //CountValue
                break;
            case 7:
            case 19:
                *temp_ptr=tmrbuf+18;                  //FTimerRef3 /* get timer reference 3 */
                break;
            case 8:
            case 18:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+16)+s_a0_reg_ofs;    //TimerRef1 /*get .1 sec update*/
                break;
            case 9:
            case 21:
                *temp_ptr=tmrbuf+17;                  //FTimerRef2 /* get timer reference 2 */
                break;
            case 10:
            case 20:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+17)+s_a0_reg_ofs;    //TimerRef2 /*get .01 sec update*/
                break;
            case 11:
            case 23:
                *temp_ptr=tmrbuf+16;                  //FTimerRef1 /* get timer reference 1 */
                break;
            case 12:
            case 22:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4+18)+s_a0_reg_ofs;    //TimerRef3 /*get .001 sec update*/
                break;
            case 13:
            case 24:
            case 26:
                TimerReg = (long*)(funct_ptr->arg);
                if(USR_REG_AREA_START <= *TimerReg)
                {
                    *TimerReg=*TimerReg-USR_REG_AREA_START+MAX_REGISTER_NO;
                    isAccessingUsrRegArea=1;
                }
                *temp_ptr=(*TimerReg*4)+s_a0_reg_ofs;    //TimerOneShotBits /* get one shot buff*/
                break;
        }//switch
        pc_counter+=NUM_SIZE;
        code_ptr+=NUM_SIZE;
    }// for loop
}


// ********************************************************
// 851019   MSG  by C.C. Liu
//
// Handling machine code for MSG element
//
// Mapping between the register bits and the message number
//
// Reg #     R#255          R#254         R#250
// Bit # 15 14 13 .. 0  15 14 .. 0  .. 15 .. 1  0
// ----------------------------------------------
// Msg # 96 95 94 .. 81 80 79 .. 65 .. 16 .. 2  1
//
// *********************************************************
/*
static Set_Msg(code_ptr,funct_ptr)
char *code_ptr;
struct funct *funct_ptr;
{
   short *temp_ptr;
   int a1,a2,MsgNo,ShiftAmount,RegNo;
   unsigned int BitPos;
   long int j,k;
   int s_reg0_ofs;

   j=(long)s_mlc_reg;        // Pointer to Reg#0
   k=(long)MLC_Data;
   s_reg0_ofs=j-k;           // Offset to MLC_Data

   a1=*code_ptr++;           // 1st argument
   a2=*code_ptr++;           // 2nd argument

   MsgNo=(funct_ptr->arg1)-1;
   RegNo=MsgNo/16;           // Each reg occupies 16 bits
   ShiftAmount=MsgNo%16;
   BitPos=1;
   BitPos<<=ShiftAmount;     // Shift Left

   while (a1>0) {
     *pc_counter++=*code_ptr++;    // Get machine code
     a1--;
   }

   temp_ptr=(short *) pc_counter;
   *temp_ptr = BitPos;  // Set the corresponding bit
   pc_counter+=2;
   code_ptr+=2;

   while(a2>0)
   {
      *pc_counter++=*code_ptr++;   // Get machine code
      a2--;
   }

   temp_ptr = (short *) pc_counter;
   *temp_ptr = ((250+RegNo)*2) + s_reg0_ofs;  // From Reg#250
   pc_counter+=2;
   code_ptr+=2;
}
*/
// ============= 851108 ==============
// new Set_Msg
// modification of 851019
// ===================================
static void Set_Msg( char *code_ptr, struct funct *funct_ptr )
{
    NUM_TYPE *temp_ptr;
    long a1,a2,a3,*MsgNo,ShiftAmount,RegNo;
    unsigned long BitPos;
    long s_a0_reg_ofs;

    s_a0_reg_ofs=(long)s_mlc_reg-(long)MLC_Data;

    a1=*code_ptr++;           // 1st argument
    a2=*code_ptr++;           // 2nd argument
    a3=*code_ptr++;

    MsgNo=(long*)(funct_ptr->arg)-1;
    RegNo=*MsgNo/32;           // Each reg occupies 16 bits
    ShiftAmount=*MsgNo%32;
    BitPos=1;
    BitPos<<=ShiftAmount;     // Shift Left

    while (a1>0)
    {
        *pc_counter++=*code_ptr++;    // Get machine code
        a1--;
    }
    temp_ptr=(NUM_TYPE *) pc_counter;
    *temp_ptr = BitPos;  // Set the corresponding bit
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a2>0)
    {
        *pc_counter++=*code_ptr++;   // Get machine code
        a2--;
    }
    temp_ptr = (NUM_TYPE *) pc_counter;
    *temp_ptr = ((250+RegNo)*2) + s_a0_reg_ofs;  // From Reg#250
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;

    while(a3>0)
    {
        *pc_counter++=*code_ptr++;   // Get machine code
        a3--;
    }
    temp_ptr = (NUM_TYPE *) pc_counter;
    *temp_ptr = ((250+RegNo)*2) + s_a0_reg_ofs;  // From Reg#250
    pc_counter+=NUM_SIZE;
    code_ptr+=NUM_SIZE;
}

/*******************************************************************/
/*
static void itoa( int n, char *s )          // convert n to characters in s
{
    int i,sign;

 s+=7;
  for (i=0;i<7;i++)
      *s--=' ';
      *s=' ';
    if ((sign=n)<0)    // record sign
       n=-n;           // make n positive
    i=0;

        do             // generate digits in reverse order
          {
           *(s+i)=n%10+'0';  // get next digit
           i++;
          }
         while ((n/=10)>0) ;  // delete it

        if (sign<0)
             {
              *(s+i)='-';
               i++;
             }
        *(s+i)=EOS;        // end of string
 }
*/
/*************************************************************************/

