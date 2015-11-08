//
//  x65.cpp
//  
//
//  Created by Carl-Henrik Skårstedt on 9/23/15.
//
//
//	A simple 6502 assembler
//
//
// The MIT License (MIT)
//
// Copyright (c) 2015 Carl-Henrik Skårstedt
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Details, source and documentation at https://github.com/Sakrac/x65.
//
// "struse.h" can be found at https://github.com/Sakrac/struse, only the header file is required.
//

#define _CRT_SECURE_NO_WARNINGS		// Windows shenanigans
#define STRUSE_IMPLEMENTATION		// include implementation of struse in this file
#include "struse.h"					// https://github.com/Sakrac/struse/blob/master/struse.h
#include <vector>
#include <stdio.h>
#include <stdlib.h>

// if the number of resolved labels exceed this in one late eval then skip
//	checking for relevance and just eval all unresolved expressions.
#define MAX_LABELS_EVAL_ALL 16

// Max number of nested scopes (within { and })
#define MAX_SCOPE_DEPTH 32

// Max number of nested conditional expressions
#define MAX_CONDITIONAL_DEPTH 64

// The maximum complexity of expressions to be evaluated
#define MAX_EVAL_VALUES 32
#define MAX_EVAL_OPER 64

// Max capacity of each label pool
#define MAX_POOL_RANGES 4
#define MAX_POOL_BYTES 128

// Max number of exported binary files from a single source
#define MAX_EXPORT_FILES 64

// Maximum number of opcodes, aliases and directives
#define MAX_OPCODES_DIRECTIVES 320

// minor variation of 6502
#define NUM_ILLEGAL_6502_OPS 21

// minor variation of 65C02
#define NUM_WDC_65C02_SPECIFIC_OPS 18


// To simplify some syntax disambiguation the preferred
// ruleset can be specified on the command line.
enum AsmSyntax {
	SYNTAX_SANE,
	SYNTAX_MERLIN
};

// Internal status and error type
enum StatusCode {
	STATUS_OK,			// everything is fine
	STATUS_RELATIVE_SECTION, // value is relative to a single section
	STATUS_NOT_READY,	// label could not be evaluated at this time
	STATUS_XREF_DEPENDENT,	// evaluated but relied on an XREF label to do so
	STATUS_NOT_STRUCT,	// return is not a struct.
	FIRST_ERROR,
	ERROR_UNDEFINED_CODE = FIRST_ERROR,
	ERROR_UNEXPECTED_CHARACTER_IN_EXPRESSION,
	ERROR_TOO_MANY_VALUES_IN_EXPRESSION,
	ERROR_TOO_MANY_OPERATORS_IN_EXPRESSION,
	ERROR_UNBALANCED_RIGHT_PARENTHESIS,
	ERROR_EXPRESSION_OPERATION,
	ERROR_EXPRESSION_MISSING_VALUES,
	ERROR_INSTRUCTION_NOT_ZP,
	ERROR_INVALID_ADDRESSING_MODE,
	ERROR_BRANCH_OUT_OF_RANGE,
	ERROR_LABEL_MISPLACED_INTERNAL,
	ERROR_BAD_ADDRESSING_MODE,
	ERROR_UNEXPECTED_CHARACTER_IN_ADDRESSING_MODE,
	ERROR_UNEXPECTED_LABEL_ASSIGMENT_FORMAT,
	ERROR_MODIFYING_CONST_LABEL,
	ERROR_OUT_OF_LABELS_IN_POOL,
	ERROR_INTERNAL_LABEL_POOL_ERROR,
	ERROR_POOL_RANGE_EXPRESSION_EVAL,
	ERROR_LABEL_POOL_REDECLARATION,
	ERROR_POOL_LABEL_ALREADY_DEFINED,
	ERROR_STRUCT_ALREADY_DEFINED,
	ERROR_REFERENCED_STRUCT_NOT_FOUND,
	ERROR_BAD_TYPE_FOR_DECLARE_CONSTANT,
	ERROR_REPT_COUNT_EXPRESSION,
	ERROR_HEX_WITH_ODD_NIBBLE_COUNT,
	ERROR_DS_MUST_EVALUATE_IMMEDIATELY,
	ERROR_NOT_AN_X65_OBJECT_FILE,
	ERROR_COULD_NOT_INCLUDE_FILE,

	ERROR_STOP_PROCESSING_ON_HIGHER,	// errors greater than this will stop execution

	ERROR_TARGET_ADDRESS_MUST_EVALUATE_IMMEDIATELY,
	ERROR_TOO_DEEP_SCOPE,
	ERROR_UNBALANCED_SCOPE_CLOSURE,
	ERROR_BAD_MACRO_FORMAT,
	ERROR_ALIGN_MUST_EVALUATE_IMMEDIATELY,
	ERROR_OUT_OF_MEMORY_FOR_MACRO_EXPANSION,
	ERROR_CONDITION_COULD_NOT_BE_RESOLVED,
	ERROR_ENDIF_WITHOUT_CONDITION,
	ERROR_ELSE_WITHOUT_IF,
	ERROR_STRUCT_CANT_BE_ASSEMBLED,
	ERROR_ENUM_CANT_BE_ASSEMBLED,
	ERROR_UNTERMINATED_CONDITION,
	ERROR_REPT_MISSING_SCOPE,
	ERROR_LINKER_MUST_BE_IN_FIXED_ADDRESS_SECTION,
	ERROR_LINKER_CANT_LINK_TO_DUMMY_SECTION,
	ERROR_UNABLE_TO_PROCESS,
	ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE,
	ERROR_CPU_NOT_SUPPORTED,
	ERROR_CANT_APPEND_SECTION_TO_TARGET,
	ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE,

	STATUSCODE_COUNT
};

// The following strings are in the same order as StatusCode
const char *aStatusStrings[STATUSCODE_COUNT] = {
	"ok",
	"relative section",
	"not ready",
	"XREF dependent result",
	"name is not a struct",
	"Undefined code",
	"Unexpected character in expression",
	"Too many values in expression",
	"Too many operators in expression",
	"Unbalanced right parenthesis in expression",
	"Expression operation",
	"Expression missing values",
	"Instruction can not be zero page",
	"Invalid addressing mode for instruction",
	"Branch out of range",
	"Internal label organization mishap",
	"Bad addressing mode",
	"Unexpected character in addressing mode",
	"Unexpected label assignment format",
	"Changing value of label that is constant",
	"Out of labels in pool",
	"Internal label pool release confusion",
	"Label pool range evaluation failed",
	"Label pool was redeclared within its scope",
	"Pool label already defined",
	"Struct already defined",
	"Referenced struct not found",
	"Declare constant type not recognized (dc.?)",
	"rept count expression could not be evaluated",
	"hex must be followed by an even number of hex numbers",
	"DS directive failed to evaluate immediately",
	"File is not a valid x65 object file",
	"Failed to read include file",

	"Errors after this point will stop execution",

	"Target address must evaluate immediately for this operation",
	"Scoping is too deep",
	"Unbalanced scope closure",
	"Unexpected macro formatting",
	"Align must evaluate immediately",
	"Out of memory for macro expansion",
	"Conditional could not be resolved",
	"#endif encountered outside conditional block",
	"#else or #elif outside conditional block",
	"Struct can not be assembled as is",
	"Enum can not be assembled as is",
	"Conditional assembly (#if/#ifdef) was not terminated in file or macro",
	"rept is missing a scope ('{ ... }')",
	"Link can only be used in a fixed address section",
	"Link can not be used in dummy sections",
	"Can not process this line",
	"Unexpected target offset for reloc or late evaluation",
	"CPU is not supported",
	"Can't append sections",
	"Zero page / Direct page section out of range",
};

// Assembler directives
enum AssemblerDirective {
	AD_CPU,			// CPU: Assemble for this target,
	AD_ORG,			// ORG: Assemble as if loaded at this address
	AD_EXPORT,		// EXPORT: export this section or disable export
	AD_LOAD,		// LOAD: If applicable, instruct to load at this address
	AD_SECTION,		// SECTION: Enable code that will be assigned a start address during a link step
	AD_LINK,		// LINK: Put sections with this name at this address (must be ORG / fixed address section)
	AD_XDEF,		// XDEF: Externally declare a symbol
	AD_XREF,		// XREF: Reference an external symbol
	AD_INCOBJ,		// INCOBJ: Read in an object file saved from a previous build
	AD_ALIGN,		// ALIGN: Add to address to make it evenly divisible by this
	AD_MACRO,		// MACRO: Create a macro
	AD_EVAL,		// EVAL: Print expression to stdout during assemble
	AD_BYTES,		// BYTES: Add 8 bit values to output
	AD_WORDS,		// WORDS: Add 16 bit values to output
	AD_DC,			// DC.B/DC.W: Declare constant (same as BYTES/WORDS)
	AD_TEXT,		// TEXT: Add text to output
	AD_INCLUDE,		// INCLUDE: Load and assemble another file at this address
	AD_INCBIN,		// INCBIN: Load and directly insert another file at this address
	AD_CONST,		// CONST: Prevent a label from mutating during assemble
	AD_IMPORT,		// IMPORT: Include or Incbin or Incobj or Incsym
	AD_LABEL,		// LABEL: Create a mutable label (optional)
	AD_INCSYM,		// INCSYM: Reference labels from another assemble
	AD_LABPOOL,		// POOL: Create a pool of addresses to assign as labels dynamically
	AD_IF,			// #IF: Conditional assembly follows based on expression
	AD_IFDEF,		// #IFDEF: Conditional assembly follows based on label defined or not
	AD_ELSE,		// #ELSE: Otherwise assembly
	AD_ELIF,		// #ELIF: Otherwise conditional assembly follows
	AD_ENDIF,		// #ENDIF: End a block of #IF/#IFDEF
	AD_STRUCT,		// STRUCT: Declare a set of labels offset from a base address
	AD_ENUM,		// ENUM: Declare a set of incremental labels
	AD_REPT,		// REPT: Repeat the assembly of the bracketed code a number of times
	AD_INCDIR,		// INCDIR: Add a folder to search for include files
	AD_A16,			// A16: Set 16 bit accumulator mode
	AD_A8,			// A8: Set 8 bit accumulator mode
	AD_XY16,		// A16: Set 16 bit index register mode
	AD_XY8,			// A8: Set 8 bit index register mode
	AD_HEX,			// HEX: LISA assembler data block
	AD_EJECT,		// EJECT: Page break for printing assembler code, ignore
	AD_LST,			// LST: Controls symbol listing
	AD_DUMMY,		// DUM: Start a dummy section (increment address but don't write anything???)
	AD_DUMMY_END,	// DEND: End a dummy section
	AD_DS,			// DS: Define section, zero out # bytes or rewind the address if negative
	AD_USR,			// USR: MERLIN user defined pseudo op, runs some code at a hard coded address on apple II, on PC does nothing.
	AD_SAV,			// SAV: MERLIN version of export but contains full filename, not an appendable name
	AD_XC,			// XC: MERLIN version of setting CPU
	AD_MX,			// MX: MERLIN control accumulator 16 bit mode
	AD_LNK,			// LNK: MERLIN load object and link
	AD_ADR,			// ADR: MERLIN store 3 byte word
	AD_ADRL,		// ADRL: MERLIN store 4 byte word
	AD_ENT,			// ENT: MERLIN extern this address label
	AD_EXT,			// EXT: MERLIN reference this address label from a different file
	AD_CYC,			// CYC: MERLIN start / stop cycle timer
};

// Operators are either instructions or directives
enum OperationType {
	OT_NONE,
	OT_MNEMONIC,
	OT_DIRECTIVE
};

// These are expression tokens in order of precedence (last is highest precedence)
enum EvalOperator {
	EVOP_NONE,
	EVOP_VAL = 'a',	// a, value => read from value queue
	EVOP_EQU,		// b, 1 if left equal to right otherwise 0
	EVOP_LT,		// c, 1 if left less than right otherwise 0
	EVOP_GT,		// d, 1 if left greater than right otherwise 0
	EVOP_LTE,		// e, 1 if left less than or equal to right otherwise 0
	EVOP_GTE,		// f, 1 if left greater than or equal to right otherwise 0
	EVOP_LOB,		// g, low byte of 16 bit value
	EVOP_HIB,		// h, high byte of 16 bit value
	EVOP_BAB,		// i, bank byte of 24 bit value
	EVOP_LPR,		// j, left parenthesis
	EVOP_RPR,		// k, right parenthesis
	EVOP_ADD,		// l, +
	EVOP_SUB,		// m, -
	EVOP_MUL,		// n, * (note: if not preceded by value or right paren this is current PC)
	EVOP_DIV,		// o, /
	EVOP_AND,		// p, &
	EVOP_OR,		// q, |
	EVOP_EOR,		// r, ^
	EVOP_SHL,		// s, <<
	EVOP_SHR,		// t, >>
	EVOP_STP,		// u, Unexpected input, should stop and evaluate what we have
	EVOP_NRY,		// v, Not ready yet
	EVOP_XRF,		// w, value from XREF label
	EVOP_ERR,		// x, Error
};

// Opcode encoding
typedef struct {
	unsigned int op_hash;
	unsigned char index;	// ground index
	unsigned char type;		// mnemonic or
} OPLookup;

enum AddrMode {
	// address mode bit index

	// 6502

	AMB_ZP_REL_X,		// 0 ($12,x)
	AMB_ZP,				// 1 $12
	AMB_IMM,			// 2 #$12
	AMB_ABS,			// 3 $1234
	AMB_ZP_Y_REL,		// 4 ($12),y
	AMB_ZP_X,			// 5 $12,x
	AMB_ABS_Y,			// 6 $1234,y
	AMB_ABS_X,			// 7 $1234,x
	AMB_REL,			// 8 ($1234)
	AMB_ACC,			// 9 A
	AMB_NON,			// a

	// 65C02

	AMB_ZP_REL,			// b ($12)
	AMB_REL_X,			// c ($1234,x)
	AMB_ZP_ABS,			// d $12, *+$12

	// 65816

	AMB_ZP_REL_L,		// e [$02]
	AMB_ZP_REL_Y_L,		// f [$00],y
	AMB_ABS_L,			// 10 $bahilo
	AMB_ABS_L_X,		// 11 $123456,x
	AMB_STK,			// 12 $12,s
	AMB_STK_REL_Y,		// 13 ($12,s),y
	AMB_REL_L,			// 14 [$1234]
	AMB_BLK_MOV,		// 15 $12,$34
	AMB_COUNT,

	AMB_FLIPXY = AMB_COUNT,	// 16 (indexing index using y treat as x address mode)
	AMB_BRANCH,				// 17 (relative address 8 bit)
	AMB_BRANCH_L,			// 18 (relative address 16 bit)
	AMB_IMM_DBL_A,			// 19 (immediate mode can be doubled in 16 bit mode)
	AMB_IMM_DBL_XY,			// 1a (immediate mode can be doubled in 16 bit mode)

	AMB_ILL,			// 1b illegal address mode

	// address mode masks
	AMM_NON = 1<<AMB_NON,
	AMM_IMM = 1<<AMB_IMM,
	AMM_ABS = 1<<AMB_ABS,
	AMM_REL = 1<<AMB_REL,
	AMM_ACC = 1<<AMB_ACC,
	AMM_ZP = 1<<AMB_ZP,
	AMM_ABS_X = 1<<AMB_ABS_X,
	AMM_ABS_Y = 1<<AMB_ABS_Y,
	AMM_ZP_X = 1<<AMB_ZP_X,
	AMM_ZP_REL_X = 1<<AMB_ZP_REL_X,
	AMM_ZP_Y_REL = 1<<AMB_ZP_Y_REL,
	AMM_ZP_REL = 1<<AMB_ZP_REL,			// b ($12)
	AMM_REL_X = 1<<AMB_REL_X,			// c ($1234,x)
	AMM_ZP_ABS = 1<<AMB_ZP_ABS,			// d $12, *+$12

	AMM_ZP_REL_L = 1<<AMB_ZP_REL_L,		// e [$02]
	AMM_ZP_REL_Y_L = 1<<AMB_ZP_REL_Y_L,	// f [$00],y
	AMM_ABS_L = 1<<AMB_ABS_L,			// 10 $bahilo
	AMM_ABS_L_X = 1<<AMB_ABS_L_X,		// 11 $123456,x
	AMM_STK = 1<<AMB_STK,				// 12 $12,s
	AMM_STK_REL_Y = 1<<AMB_STK_REL_Y,	// 13 ($12,s),y
	AMM_REL_L = 1<<AMB_REL_L,			// 14 [$1234]
	AMM_BLK_MOV = 1<<AMB_BLK_MOV,		// 15 $12,$34


	AMM_FLIPXY = 1<<AMB_FLIPXY,
	AMM_BRANCH = 1<<AMB_BRANCH,
	AMM_BRANCH_L = 1<<AMB_BRANCH_L,
	AMM_IMM_DBL_A = 1<<AMB_IMM_DBL_A,
	AMM_IMM_DBL_XY = 1<<AMB_IMM_DBL_XY,

	// instruction group specific masks
	AMM_BRA = AMM_BRANCH | AMM_ABS,
	AMM_ORA = AMM_IMM | AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_Y | AMM_ABS_X | AMM_ZP_REL_X | AMM_ZP_Y_REL,
	AMM_STA = AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_Y | AMM_ABS_X | AMM_ZP_REL_X | AMM_ZP_Y_REL,
	AMM_ASL = AMM_ACC | AMM_NON | AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_X,
	AMM_STX = AMM_FLIPXY | AMM_ZP | AMM_ZP_X | AMM_ABS, // note: for x ,x/,y flipped for this instr.
	AMM_LDX = AMM_FLIPXY | AMM_IMM | AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_X, // note: for x ,x/,y flipped for this instr.
	AMM_STY = AMM_ZP | AMM_ZP_X | AMM_ABS,
	AMM_LDY = AMM_IMM | AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_X,
	AMM_DEC = AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_X,
	AMM_BIT = AMM_ZP | AMM_ABS,
	AMM_JMP = AMM_ABS | AMM_REL,
	AMM_CPY = AMM_IMM | AMM_ZP | AMM_ABS,

	// 6502 illegal modes
	AMM_SLO = AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_Y | AMM_ABS_X | AMM_ZP_REL_X | AMM_ZP_Y_REL,
	AMM_SAX = AMM_FLIPXY | AMM_ZP | AMM_ZP_X | AMM_ZP_REL_X | AMM_ABS,
	AMM_LAX = AMM_FLIPXY | AMM_ZP | AMM_ZP_X | AMM_ZP_REL_X | AMM_ABS | AMM_ABS_X,
	AMM_AHX = AMM_FLIPXY | AMM_ZP_REL_X | AMM_ABS_X,
	AMM_SHY = AMM_ABS_X,
	AMM_SHX = AMM_ABS_Y,

	// 65C02 groups
	AMC_ORA = AMM_ORA | AMM_ZP_REL,
	AMC_STA = AMM_STA | AMM_ZP_REL,
	AMC_BIT = AMM_BIT | AMM_IMM | AMM_ZP_X | AMM_ABS_X,
	AMC_DEC = AMM_DEC | AMM_NON | AMM_ACC,
	AMC_JMP = AMM_JMP | AMM_REL_X,
	AMC_STZ = AMM_ZP | AMM_ZP_X | AMM_ABS | AMM_ABS_X,
	AMC_TRB = AMM_ZP | AMM_ABS,
	AMC_BBR = AMM_ZP_ABS,

	// 65816 groups
	AM8_JSR = AMM_ABS | AMM_ABS_L | AMM_REL_X,
	AM8_JSL = AMM_ABS_L,
	AM8_BIT = AMM_IMM_DBL_A | AMC_BIT,
	AM8_ORA = AMM_IMM_DBL_A | AMC_ORA | AMM_STK | AMM_ZP_REL_L | AMM_ABS_L | AMM_STK_REL_Y | AMM_ZP_REL_Y_L | AMM_ABS_L_X,
	AM8_STA = AMC_STA | AMM_STK | AMM_ZP_REL_L | AMM_ABS_L | AMM_STK_REL_Y | AMM_ZP_REL_Y_L | AMM_ABS_L_X,
	AM8_ORL = AMM_ABS_L | AMM_ABS_L_X,
	AM8_STL = AMM_ABS_L | AMM_ABS_L_X,
	AM8_LDX = AMM_IMM_DBL_XY | AMM_LDX,
	AM8_LDY = AMM_IMM_DBL_XY | AMM_LDY,
	AM8_CPY = AMM_IMM_DBL_XY | AMM_CPY,
	AM8_JMP = AMC_JMP | AMM_REL_L | AMM_ABS_L | AMM_REL_X,
	AM8_JML = AMM_REL_L | AMM_ABS_L,
	AM8_BRL = AMM_BRANCH_L | AMM_ABS,
	AM8_MVN = AMM_BLK_MOV,
	AM8_PEI = AMM_ZP_REL,
	AM8_PER = AMM_BRANCH_L | AMM_ABS,
	AM8_REP = AMM_IMM | AMM_ZP,	// Merlin allows this to look like a zp access
};

struct mnem {
	const char *instr;
	unsigned int modes;
	unsigned char aCodes[AMB_COUNT];
};

struct mnem opcodes_6502[] = {
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty
	{ "brk", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jsr", AMM_ABS, { 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rti", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40 } },
	{ "rts", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60 } },
	{ "ora", AMM_ORA, { 0x01, 0x05, 0x09, 0x0d, 0x11, 0x15, 0x19, 0x1d, 0x00, 0x00, 0x00 } },
	{ "and", AMM_ORA, { 0x21, 0x25, 0x29, 0x2d, 0x31, 0x35, 0x39, 0x3d, 0x00, 0x00, 0x00 } },
	{ "eor", AMM_ORA, { 0x41, 0x45, 0x49, 0x4d, 0x51, 0x55, 0x59, 0x5d, 0x00, 0x00, 0x00 } },
	{ "adc", AMM_ORA, { 0x61, 0x65, 0x69, 0x6d, 0x71, 0x75, 0x79, 0x7d, 0x00, 0x00, 0x00 } },
	{ "sta", AMM_STA, { 0x81, 0x85, 0x00, 0x8d, 0x91, 0x95, 0x99, 0x9d, 0x00, 0x00, 0x00 } },
	{ "lda", AMM_ORA, { 0xa1, 0xa5, 0xa9, 0xad, 0xb1, 0xb5, 0xb9, 0xbd, 0x00, 0x00, 0x00 } },
	{ "cmp", AMM_ORA, { 0xc1, 0xc5, 0xc9, 0xcd, 0xd1, 0xd5, 0xd9, 0xdd, 0x00, 0x00, 0x00 } },
	{ "sbc", AMM_ORA, { 0xe1, 0xe5, 0xe9, 0xed, 0xf1, 0xf5, 0xf9, 0xfd, 0x00, 0x00, 0x00 } },
	{ "asl", AMM_ASL, { 0x00, 0x06, 0x00, 0x0e, 0x00, 0x16, 0x00, 0x1e, 0x00, 0x0a, 0x0a } },
	{ "rol", AMM_ASL, { 0x00, 0x26, 0x00, 0x2e, 0x00, 0x36, 0x00, 0x3e, 0x00, 0x2a, 0x2a } },
	{ "lsr", AMM_ASL, { 0x00, 0x46, 0x00, 0x4e, 0x00, 0x56, 0x00, 0x5e, 0x00, 0x4a, 0x4a } },
	{ "ror", AMM_ASL, { 0x00, 0x66, 0x00, 0x6e, 0x00, 0x76, 0x00, 0x7e, 0x00, 0x6a, 0x6a } },
	{ "stx", AMM_STX, { 0x00, 0x86, 0x00, 0x8e, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldx", AMM_LDX, { 0x00, 0xa6, 0xa2, 0xae, 0x00, 0xb6, 0x00, 0xbe, 0x00, 0x00, 0x00 } },
	{ "dec", AMM_DEC, { 0x00, 0xc6, 0x00, 0xce, 0x00, 0xd6, 0x00, 0xde, 0x00, 0x00, 0x00 } },
	{ "inc", AMM_DEC, { 0x00, 0xe6, 0x00, 0xee, 0x00, 0xf6, 0x00, 0xfe, 0x00, 0x00, 0x00 } },
	{ "php", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } },
	{ "plp", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28 } },
	{ "pha", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48 } },
	{ "pla", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68 } },
	{ "dey", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88 } },
	{ "tay", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8 } },
	{ "iny", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8 } },
	{ "inx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8 } },
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty
	{ "bpl", AMM_BRA, { 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bmi", AMM_BRA, { 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvc", AMM_BRA, { 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvs", AMM_BRA, { 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcc", AMM_BRA, { 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcs", AMM_BRA, { 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bne", AMM_BRA, { 0x00, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "beq", AMM_BRA, { 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "clc", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18 } },
	{ "sec", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38 } },
	{ "cli", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58 } },
	{ "sei", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78 } },
	{ "tya", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98 } },
	{ "clv", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8 } },
	{ "cld", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8 } },
	{ "sed", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8 } },
	{ "bit", AMM_BIT, { 0x00, 0x24, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jmp", AMM_JMP, { 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00 } },
	{ "sty", AMM_STY, { 0x00, 0x84, 0x00, 0x8c, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldy", AMM_LDY, { 0x00, 0xa4, 0xa0, 0xac, 0x00, 0xb4, 0x00, 0xbc, 0x00, 0x00, 0x00 } },
	{ "cpy", AMM_CPY, { 0x00, 0xc4, 0xc0, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cpx", AMM_CPY, { 0x00, 0xe4, 0xe0, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "txa", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8a } },
	{ "txs", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9a } },
	{ "tax", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa } },
	{ "tsx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba } },
	{ "dex", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca } },
	{ "nop", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea } },

	// 21 ILLEGAL 6502 OPCODES (http://www.oxyron.de/html/opcodes02.html)
	// NOTE: If adding or removing, update NUM_ILLEGAL_6502_OPS
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty
	{ "slo", AMM_SLO, { 0x03, 0x07, 0x00, 0x0f, 0x13, 0x17, 0x1b, 0x1f, 0x00, 0x00, 0x00 } },
	{ "rla", AMM_SLO, { 0x23, 0x27, 0x00, 0x2f, 0x33, 0x37, 0x3b, 0x3f, 0x00, 0x00, 0x00 } },
	{ "sre", AMM_SLO, { 0x43, 0x47, 0x00, 0x4f, 0x53, 0x57, 0x5b, 0x5f, 0x00, 0x00, 0x00 } },
	{ "rra", AMM_SLO, { 0x63, 0x67, 0x00, 0x6f, 0x73, 0x77, 0x7b, 0x7f, 0x00, 0x00, 0x00 } },
	{ "sax", AMM_SAX, { 0x83, 0x87, 0x00, 0x8f, 0x00, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "lax", AMM_LAX, { 0xa3, 0xa7, 0x00, 0xaf, 0xb3, 0xb7, 0x00, 0xbf, 0x00, 0x00, 0x00 } },
	{ "dcp", AMM_SLO, { 0xc3, 0xc7, 0x00, 0xcf, 0xd3, 0xd7, 0xdb, 0xdf, 0x00, 0x00, 0x00 } },
	{ "isc", AMM_SLO, { 0xe3, 0xe7, 0x00, 0xef, 0xf3, 0xf7, 0xfb, 0xff, 0x00, 0x00, 0x00 } },
	{ "anc", AMM_IMM, { 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "aac", AMM_IMM, { 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "alr", AMM_IMM, { 0x00, 0x00, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "arr", AMM_IMM, { 0x00, 0x00, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "xaa", AMM_IMM, { 0x00, 0x00, 0x8b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{"lax2", AMM_IMM, { 0x00, 0x00, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "axs", AMM_IMM, { 0x00, 0x00, 0xcb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "sbi", AMM_IMM, { 0x00, 0x00, 0xeb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ahx", AMM_AHX, { 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9f, 0x00, 0x00, 0x00 } },
	{ "shy", AMM_SHY, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9c, 0x00, 0x00, 0x00 } },
	{ "shx", AMM_SHX, { 0x00, 0x00, 0x00, 0x00, 0x93, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x00 } },
	{ "tas", AMM_SHX, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9b, 0x00, 0x00, 0x00, 0x00 } },
	{ "las", AMM_SHX, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbb, 0x00, 0x00, 0x00, 0x00 } },
};

const char* aliases_6502[] = {
	"bcc", "blt",
	"bcs", "bge",
	nullptr, nullptr
};

unsigned char timing_6502[] = {
	0x0e, 0x0c, 0xff, 0xff, 0xff, 0x06, 0x0a, 0xff, 0x06, 0x04, 0x04, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff,
	0x0c, 0x0c, 0xff, 0xff, 0x06, 0x06, 0x0a, 0xff, 0x08, 0x04, 0x04, 0xff, 0x08, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff,
	0x0c, 0x0c, 0xff, 0xff, 0xff, 0x06, 0x0a, 0xff, 0x06, 0x04, 0x04, 0xff, 0x06, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff,
	0x0c, 0x0c, 0xff, 0xff, 0xff, 0x06, 0x0a, 0xff, 0x08, 0x04, 0x04, 0xff, 0x0a, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff,
	0xff, 0x0c, 0xff, 0xff, 0x06, 0x06, 0x06, 0xff, 0x04, 0xff, 0x04, 0xff, 0x08, 0x08, 0x08, 0xff, 0x05, 0x0c, 0xff, 0xff, 0x08, 0x08, 0x08, 0xff, 0x04, 0x0a, 0x04, 0xff, 0xff, 0x0a, 0xff, 0xff,
	0x04, 0x0c, 0x04, 0xff, 0x06, 0x06, 0x06, 0xff, 0x04, 0x04, 0x04, 0xff, 0x08, 0x08, 0x08, 0xff, 0x05, 0x0b, 0xff, 0xff, 0x08, 0x08, 0x08, 0xff, 0x04, 0x09, 0x04, 0xff, 0x09, 0x09, 0x09, 0xff,
	0x04, 0x0c, 0xff, 0xff, 0x06, 0x06, 0x0a, 0xff, 0x04, 0x04, 0x04, 0xff, 0x08, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff,
	0x04, 0x0c, 0xff, 0xff, 0x06, 0x06, 0x0a, 0xff, 0x04, 0x04, 0x04, 0xff, 0x08, 0x08, 0x0c, 0xff, 0x05, 0x0b, 0xff, 0xff, 0xff, 0x08, 0x0c, 0xff, 0x04, 0x09, 0xff, 0xff, 0xff, 0x09, 0x0e, 0xff
};

static const int num_opcodes_6502 = sizeof(opcodes_6502) / sizeof(opcodes_6502[0]);

struct mnem opcodes_65C02[] = {
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs
	{ "brk", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jsr", AMM_ABS, { 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rti", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 } },
	{ "rts", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00 } },
	{ "ora", AMC_ORA, { 0x01, 0x05, 0x09, 0x0d, 0x11, 0x15, 0x19, 0x1d, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00 } },
	{ "and", AMC_ORA, { 0x21, 0x25, 0x29, 0x2d, 0x31, 0x35, 0x39, 0x3d, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00 } },
	{ "eor", AMC_ORA, { 0x41, 0x45, 0x49, 0x4d, 0x51, 0x55, 0x59, 0x5d, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00 } },
	{ "adc", AMC_ORA, { 0x61, 0x65, 0x69, 0x6d, 0x71, 0x75, 0x79, 0x7d, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00 } },
	{ "sta", AMC_STA, { 0x81, 0x85, 0x00, 0x8d, 0x91, 0x95, 0x99, 0x9d, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00 } },
	{ "lda", AMC_ORA, { 0xa1, 0xa5, 0xa9, 0xad, 0xb1, 0xb5, 0xb9, 0xbd, 0x00, 0x00, 0x00, 0xb2, 0x00, 0x00 } },
	{ "cmp", AMC_ORA, { 0xc1, 0xc5, 0xc9, 0xcd, 0xd1, 0xd5, 0xd9, 0xdd, 0x00, 0x00, 0x00, 0xd2, 0x00, 0x00 } },
	{ "sbc", AMC_ORA, { 0xe1, 0xe5, 0xe9, 0xed, 0xf1, 0xf5, 0xf9, 0xfd, 0x00, 0x00, 0x00, 0xf2, 0x00, 0x00 } },
	{ "asl", AMM_ASL, { 0x00, 0x06, 0x00, 0x0e, 0x00, 0x16, 0x00, 0x1e, 0x00, 0x0a, 0x0a, 0x00, 0x00, 0x00 } },
	{ "rol", AMM_ASL, { 0x00, 0x26, 0x00, 0x2e, 0x00, 0x36, 0x00, 0x3e, 0x00, 0x2a, 0x2a, 0x00, 0x00, 0x00 } },
	{ "lsr", AMM_ASL, { 0x00, 0x46, 0x00, 0x4e, 0x00, 0x56, 0x00, 0x5e, 0x00, 0x4a, 0x4a, 0x00, 0x00, 0x00 } },
	{ "ror", AMM_ASL, { 0x00, 0x66, 0x00, 0x6e, 0x00, 0x76, 0x00, 0x7e, 0x00, 0x6a, 0x6a, 0x00, 0x00, 0x00 } },
	{ "stx", AMM_STX, { 0x00, 0x86, 0x00, 0x8e, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldx", AMM_LDX, { 0x00, 0xa6, 0xa2, 0xae, 0x00, 0xb6, 0x00, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "dec", AMC_DEC, { 0x00, 0xc6, 0x00, 0xce, 0x00, 0xd6, 0x00, 0xde, 0x00, 0x3a, 0x3a, 0x00, 0x00, 0x00 } },
	{ "inc", AMC_DEC, { 0x00, 0xe6, 0x00, 0xee, 0x00, 0xf6, 0x00, 0xfe, 0x00, 0x1a, 0x1a, 0x00, 0x00, 0x00 } },
	{ "dea", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0x00, 0x00, 0x3a, 0x00, 0x00, 0x00 } },
	{ "ina", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00 } },
	{ "php", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 } },
	{ "plp", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00 } },
	{ "pha", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00 } },
	{ "pla", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00 } },
	{ "phy", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00 } },
	{ "ply", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x00 } },
	{ "phx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xda, 0x00, 0x00, 0x00 } },
	{ "plx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00 } },
	{ "dey", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00 } },
	{ "tay", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00 } },
	{ "iny", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00 } },
	{ "inx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00 } },
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs
	{ "bpl", AMM_BRA, { 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bmi", AMM_BRA, { 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvc", AMM_BRA, { 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvs", AMM_BRA, { 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bra", AMM_BRA, { 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcc", AMM_BRA, { 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcs", AMM_BRA, { 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bne", AMM_BRA, { 0x00, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "beq", AMM_BRA, { 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "clc", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00 } },
	{ "sec", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00 } },
	{ "cli", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00 } },
	{ "sei", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00 } },
	{ "tya", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00 } },
	{ "clv", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0x00 } },
	{ "cld", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x00, 0x00 } },
	{ "sed", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00 } },
	{ "bit", AMC_BIT, { 0x00, 0x24, 0x89, 0x2c, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "stz", AMC_STZ, { 0x00, 0x64, 0x00, 0x9c, 0x00, 0x74, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "trb", AMC_TRB, { 0x00, 0x14, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tsb", AMC_TRB, { 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jmp", AMC_JMP, { 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x7c, 0x00 } },
	{ "sty", AMM_STY, { 0x00, 0x84, 0x00, 0x8c, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldy", AMM_LDY, { 0x00, 0xa4, 0xa0, 0xac, 0x00, 0xb4, 0x00, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cpy", AMM_CPY, { 0x00, 0xc4, 0xc0, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cpx", AMM_CPY, { 0x00, 0xe4, 0xe0, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "txa", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x00, 0x00, 0x00 } },
	{ "txs", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00 } },
	{ "tax", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0x00, 0x00, 0x00 } },
	{ "tsx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0x00, 0x00, 0x00 } },
	{ "dex", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0x00, 0x00, 0x00 } },
	{ "nop", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x00, 0x00, 0x00 } },

	// WDC specific (18 instructions)
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs

	{ "stp", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdb, 0x00, 0x00, 0x00 } },
	{ "wai", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcb, 0x00, 0x00, 0x00 } },
	{ "bbr0", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f } },
	{ "bbr1", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f } },
	{ "bbr2", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f } },
	{ "bbr3", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f } },
	{ "bbr4", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f } },
	{ "bbr5", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5f } },
	{ "bbr6", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6f } },
	{ "bbr7", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f } },
	{ "bbs0", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f } },
	{ "bbs1", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9f } },
	{ "bbs2", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaf } },
	{ "bbs3", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbf } },
	{ "bbs4", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcf } },
	{ "bbs5", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdf } },
	{ "bbs6", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef } },
	{ "bbs7", AMC_BBR, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x00, 0x00, 0xff } },
};

const char* aliases_65C02[] = {
	"bcc", "blt",
	"bcs", "bge",
	nullptr, nullptr
};

static const int num_opcodes_65C02 = sizeof(opcodes_65C02) / sizeof(opcodes_65C02[0]);

struct mnem opcodes_65816[] = {
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs [zp] [zp],y absl absl,x b,s (b,s),y[$000] b,b
	{ "brk", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jsr", AM8_JSR, { 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "jsl", AM8_JSL, { 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rti", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rts", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rtl", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ora", AM8_ORA, { 0x01, 0x05, 0x09, 0x0d, 0x11, 0x15, 0x19, 0x1d, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x07, 0x17, 0x0f, 0x1f, 0x03, 0x13, 0x00, 0x00 } },
	{ "and", AM8_ORA, { 0x21, 0x25, 0x29, 0x2d, 0x31, 0x35, 0x39, 0x3d, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x27, 0x37, 0x2f, 0x3f, 0x23, 0x33, 0x00, 0x00 } },
	{ "eor", AM8_ORA, { 0x41, 0x45, 0x49, 0x4d, 0x51, 0x55, 0x59, 0x5d, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x47, 0x57, 0x4f, 0x5f, 0x43, 0x53, 0x00, 0x00 } },
	{ "adc", AM8_ORA, { 0x61, 0x65, 0x69, 0x6d, 0x71, 0x75, 0x79, 0x7d, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00, 0x67, 0x77, 0x6f, 0x7f, 0x63, 0x73, 0x00, 0x00 } },
	{ "sta", AM8_STA, { 0x81, 0x85, 0x00, 0x8d, 0x91, 0x95, 0x99, 0x9d, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x87, 0x97, 0x8f, 0x9f, 0x83, 0x93, 0x00, 0x00 } },
	{ "lda", AM8_ORA, { 0xa1, 0xa5, 0xa9, 0xad, 0xb1, 0xb5, 0xb9, 0xbd, 0x00, 0x00, 0x00, 0xb2, 0x00, 0x00, 0xa7, 0xb7, 0xaf, 0xbf, 0xa3, 0xb3, 0x00, 0x00 } },
	{ "cmp", AM8_ORA, { 0xc1, 0xc5, 0xc9, 0xcd, 0xd1, 0xd5, 0xd9, 0xdd, 0x00, 0x00, 0x00, 0xd2, 0x00, 0x00, 0xc7, 0xd7, 0xcf, 0xdf, 0xc3, 0xd3, 0x00, 0x00 } },
	{ "sbc", AM8_ORA, { 0xe1, 0xe5, 0xe9, 0xed, 0xf1, 0xf5, 0xf9, 0xfd, 0x00, 0x00, 0x00, 0xf2, 0x00, 0x00, 0xe7, 0xf7, 0xef, 0xff, 0xe3, 0xf3, 0x00, 0x00 } },
	{"oral", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f, 0x00, 0x00, 0x00, 0x00 } },
	{"andl", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f, 0x3f, 0x00, 0x00, 0x00, 0x00 } },
	{"eorl", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x5f, 0x00, 0x00, 0x00, 0x00 } },
	{"adcl", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6f, 0x7f, 0x00, 0x00, 0x00, 0x00 } },
	{"stal", AM8_STL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x9f, 0x00, 0x00, 0x00, 0x00 } },
	{"ldal", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaf, 0xbf, 0x00, 0x00, 0x00, 0x00 } },
	{"cmpl", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcf, 0xdf, 0x00, 0x00, 0x00, 0x00 } },
	{"sbcl", AM8_ORL, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef, 0xff, 0x00, 0x00, 0x00, 0x00 } },
	{ "asl", AMM_ASL, { 0x00, 0x06, 0x00, 0x0e, 0x00, 0x16, 0x00, 0x1e, 0x00, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rol", AMM_ASL, { 0x00, 0x26, 0x00, 0x2e, 0x00, 0x36, 0x00, 0x3e, 0x00, 0x2a, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "lsr", AMM_ASL, { 0x00, 0x46, 0x00, 0x4e, 0x00, 0x56, 0x00, 0x5e, 0x00, 0x4a, 0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ror", AMM_ASL, { 0x00, 0x66, 0x00, 0x6e, 0x00, 0x76, 0x00, 0x7e, 0x00, 0x6a, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "stx", AMM_STX, { 0x00, 0x86, 0x00, 0x8e, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldx", AM8_LDX, { 0x00, 0xa6, 0xa2, 0xae, 0x00, 0xb6, 0x00, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "dec", AMC_DEC, { 0x00, 0xc6, 0x00, 0xce, 0x00, 0xd6, 0x00, 0xde, 0x00, 0x3a, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "inc", AMC_DEC, { 0x00, 0xe6, 0x00, 0xee, 0x00, 0xf6, 0x00, 0xfe, 0x00, 0x1a, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "dea", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0x00, 0x00, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ina", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "php", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "plp", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "pha", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs [zp] [zp],y absl absl,x b,s (b,s),y[$0000]b,b
	{ "pla", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "phy", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ply", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "phx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "plx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "dey", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tay", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "iny", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "inx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bpl", AMM_BRA, { 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bmi", AMM_BRA, { 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvc", AMM_BRA, { 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bvs", AMM_BRA, { 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bra", AMM_BRA, { 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "brl", AM8_BRL, { 0x00, 0x00, 0x00, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcc", AMM_BRA, { 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bcs", AMM_BRA, { 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bne", AMM_BRA, { 0x00, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "beq", AMM_BRA, { 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "clc", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "sec", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cli", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "sei", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tya", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "clv", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cld", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "sed", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "bit", AM8_BIT, { 0x00, 0x24, 0x89, 0x2c, 0x00, 0x34, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "stz", AMC_STZ, { 0x00, 0x64, 0x00, 0x9c, 0x00, 0x74, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "trb", AMC_TRB, { 0x00, 0x14, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tsb", AMC_TRB, { 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
//	   nam   modes     (zp,x)   zp     # $0000 (zp),y zp,x  abs,y abs,x (xx)     A  empty (zp)(abs,x)zp,abs [zp] [zp],y absl absl,x b,s (b,s),y[$0000]b,b
	{ "jmp", AM8_JMP, { 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x00, 0xdc, 0x00 } },
	{ "jml", AM8_JML, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x00, 0xdc, 0x00 } },
	{ "sty", AMM_STY, { 0x00, 0x84, 0x00, 0x8c, 0x00, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "ldy", AM8_LDY, { 0x00, 0xa4, 0xa0, 0xac, 0x00, 0xb4, 0x00, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cpy", AM8_CPY, { 0x00, 0xc4, 0xc0, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cpx", AM8_CPY, { 0x00, 0xe4, 0xe0, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "txa", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "txs", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tax", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tsx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "dex", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "nop", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "cop", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "wdm", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "mvp", AM8_MVN, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44 } },
	{ "mvn", AM8_MVN, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x54 } },
	{ "pea", AMM_ABS, { 0x00, 0x00, 0x00, 0xf4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "pei", AM8_PEI, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "per", AM8_PER, { 0x00, 0x00, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "rep", AM8_REP, { 0x00, 0xc2, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "sep", AM8_REP, { 0x00, 0xe2, 0xe2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "phd", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tcs", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "pld", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tsc", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "phk", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tcd", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tdc", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "phb", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "txy", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "plb", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "tyx", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "wai", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "stp", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xdb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "xba", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xeB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	{ "xce", AMM_NON, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
};

const char* aliases_65816[] = {
	"bcc", "blt",
	"bcs", "bge",
	"tcs", "tas",
	"tsc", "tsa",
	"xba", "swa",
	"tcd", "tad",
	"tdc", "tda",
	nullptr, nullptr
};

static const int num_opcodes_65816 = sizeof(opcodes_65816) / sizeof(opcodes_65816[0]);

unsigned char timing_65816[] = {
	0x4e, 0x1c, 0x4e, 0x28, 0x3a, 0x26, 0x3a, 0x1c, 0x46, 0x24, 0x44, 0x48, 0x4c, 0x28, 0x5c, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x3a, 0x18, 0x6c, 0x1c, 0x44, 0x28, 0x44, 0x44, 0x4c, 0x28, 0x5e, 0x2a,
	0x4c, 0x1c, 0x50, 0x28, 0x16, 0x26, 0x3a, 0x1c, 0x48, 0x24, 0x44, 0x4a, 0x28, 0x28, 0x4c, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x18, 0x18, 0x3c, 0x1c, 0x44, 0x28, 0x44, 0x44, 0x28, 0x28, 0x4e, 0x2a,
	0x4c, 0x1c, 0x42, 0x28, 0x42, 0x16, 0x6a, 0x1c, 0x26, 0x24, 0x44, 0x46, 0x46, 0x28, 0x5c, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x42, 0x18, 0x6c, 0x1c, 0x44, 0x28, 0x76, 0x44, 0x48, 0x28, 0x5e, 0x2a,
	0x4c, 0x1c, 0x4c, 0x28, 0x16, 0x26, 0x3a, 0x1c, 0x28, 0x24, 0x44, 0x4c, 0x4a, 0x28, 0x4c, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x28, 0x18, 0x3c, 0x1c, 0x44, 0x28, 0x78, 0x44, 0x4c, 0x28, 0x4e, 0x2a,
	0x46, 0x1c, 0x48, 0x28, 0x86, 0x16, 0x86, 0x1c, 0x44, 0x24, 0x44, 0x46, 0x78, 0x28, 0x78, 0x2a,
	0x44, 0x1c, 0x1a, 0x2e, 0x88, 0x18, 0x88, 0x1c, 0x44, 0x2a, 0x44, 0x44, 0x28, 0x2a, 0x2a, 0x2a,
	0x74, 0x1c, 0x74, 0x28, 0x86, 0x16, 0x86, 0x1c, 0x44, 0x24, 0x44, 0x48, 0x78, 0x28, 0x78, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x88, 0x18, 0x88, 0x1c, 0x44, 0x28, 0x44, 0x44, 0x78, 0x28, 0x78, 0x2a,
	0x74, 0x1c, 0x46, 0x28, 0x86, 0x16, 0x6a, 0x1c, 0x44, 0x24, 0x44, 0x26, 0x78, 0x28, 0x5c, 0x2a,
	0x44, 0x1a, 0x1a, 0x2e, 0x4c, 0x18, 0x6c, 0x1c, 0x44, 0x28, 0x76, 0x46, 0x4c, 0x28, 0x5e, 0x2a,
	0x74, 0x3c, 0x46, 0x48, 0x86, 0x36, 0x6a, 0x3c, 0x44, 0x44, 0x44, 0x46, 0x78, 0x48, 0x5c, 0x4a,
	0x44, 0x3a, 0x3a, 0x4e, 0x4a, 0x38, 0x6c, 0x3c, 0x44, 0x48, 0x78, 0x44, 0x50, 0x48, 0x5e, 0x4a
};

// m=0, i=0, dp!=0
unsigned char timing_65816_plus[9][3] = {
	{ 0, 0, 0 },	// 6502 plus timing check bit 0
	{ 1, 0, 1 },	// acc 16 bit + dp!=0
	{ 1, 0, 0 },	// acc 16 bit
	{ 0, 0, 1 },	// dp != 0
	{ 0, 0, 0 },	// no plus
	{ 2, 0, 0 },	// acc 16 bit yields 2+
	{ 2, 0, 1 },	// acc 16 bit yields 2+ + dp!=0
	{ 0, 1, 0 },	// idx 16 bit
	{ 0, 1, 1 }		// idx 16 bit + dp!=0
};

// 65C02
// http://6502.org/tutorials/65c02opcodes.html
// http://www.oxyron.de/html/opcodesc02.html

// 65816
// http://wiki.superfamicom.org/snes/show/65816+Reference#fn:14
// http://softpixel.com/~cwright/sianse/docs/65816NFO.HTM
// http://www.oxyron.de/html/opcodes816.html

// How instruction argument is encoded
enum CODE_ARG {
	CA_NONE,			// single byte instruction
	CA_ONE_BYTE,		// instruction carries one byte
	CA_TWO_BYTES,		// instruction carries two bytes
	CA_THREE_BYTES,		// instruction carries three bytes
	CA_BRANCH,			// instruction carries an 8 bit relative address
	CA_BRANCH_16,		// instruction carries a 16 bit relative address
	CA_BYTE_BRANCH,		// instruction carries one byte and one branch
	CA_TWO_ARG_BYTES,	// two separate values
};

enum CPUIndex {
	CPU_6502,
	CPU_6502_ILLEGAL,
	CPU_65C02,
	CPU_65C02_WDC,
	CPU_65816
};

// CPU by index
struct CPUDetails {
	mnem *opcodes;
	int num_opcodes;
	const char* name;
	const char** aliases;
	const unsigned char *timing;
} aCPUs[] = {
	{ opcodes_6502, num_opcodes_6502 - NUM_ILLEGAL_6502_OPS, "6502", aliases_6502, timing_6502 },
	{ opcodes_6502, num_opcodes_6502, "6502ill", aliases_6502, timing_6502 },
	{ opcodes_65C02, num_opcodes_65C02 - NUM_WDC_65C02_SPECIFIC_OPS, "65C02", aliases_65C02, nullptr },
	{ opcodes_65C02, num_opcodes_65C02, "65C02WDC", aliases_65C02, nullptr },
	{ opcodes_65816, num_opcodes_65816, "65816", aliases_65816, timing_65816 },
};
static const int nCPUs = sizeof(aCPUs) / sizeof(aCPUs[0]);


// hardtexted strings
static const strref c_comment("//");
static const strref word_char_range("!0-9a-zA-Z_@$!#");
static const strref label_end_char_range("!0-9a-zA-Z_@$!.");
static const strref label_end_char_range_merlin("!0-9a-zA-Z_@$]:?");
static const strref filename_end_char_range("!0-9a-zA-Z_!@#$%&()/\\-.");
static const strref keyword_equ("equ");
static const strref str_label("label");
static const strref str_const("const");
static const strref struct_byte("byte");
static const strref struct_word("word");
static const strref import_source("source");
static const strref import_binary("binary");
static const strref import_c64("c64");
static const strref import_text("text");
static const strref import_object("object");
static const strref import_symbols("symbols");
static const char* aAddrModeFmt[] = {
	"%s ($%02x,x)",			// 00
	"%s $%02x",				// 01
	"%s #$%02x",			// 02
	"%s $%04x",				// 03
	"%s ($%02x),y",			// 04
	"%s $%02x,x",			// 05
	"%s $%04x,y",			// 06
	"%s $%04x,x",			// 07
	"%s ($%04x)",			// 08
	"%s A",					// 09
	"%s ",					// 0a
	"%s ($%02x)",			// 0b
	"%s ($%04x,x)",			// 0c
	"%s $%02x, $%04x",		// 0d
	"%s [$%02x]",			// 0e
	"%s [$%02x],y",			// 0f
	"%s $%06x",				// 10
	"%s $%06x,x",			// 11
	"%s $%02x,s",			// 12
	"%s ($%02x,s),y",		// 13
	"%s [$%04x]",			// 14
	"%s $%02x,$%02x",		// 15
};


typedef struct {
	const char *name;
	AssemblerDirective directive;
} DirectiveName;

DirectiveName aDirectiveNames[] {
	{ "CPU", AD_CPU },
	{ "PROCESSOR", AD_CPU },
	{ "PC", AD_ORG },
	{ "ORG", AD_ORG },
	{ "LOAD", AD_LOAD },
	{ "EXPORT", AD_EXPORT },
	{ "SECTION", AD_SECTION },
	{ "SEG", AD_SECTION },		// DASM version of SECTION
	{ "SEGMENT", AD_SECTION },	// CA65 version of SECTION
	{ "LINK", AD_LINK },
	{ "XDEF", AD_XDEF },
	{ "XREF", AD_XREF },
	{ "INCOBJ", AD_INCOBJ },
	{ "ALIGN", AD_ALIGN },
	{ "MACRO", AD_MACRO },
	{ "EVAL", AD_EVAL },
	{ "PRINT", AD_EVAL },
	{ "BYTE", AD_BYTES },
	{ "BYTES", AD_BYTES },
	{ "WORD", AD_WORDS },
	{ "WORDS", AD_WORDS },
	{ "LONG", AD_ADRL },
	{ "DC", AD_DC },
	{ "TEXT", AD_TEXT },
	{ "INCLUDE", AD_INCLUDE },
	{ "INCBIN", AD_INCBIN },
	{ "IMPORT", AD_IMPORT },
	{ "CONST", AD_CONST },
	{ "LABEL", AD_LABEL },
	{ "INCSYM", AD_INCSYM },
	{ "LABPOOL", AD_LABPOOL },
	{ "POOL", AD_LABPOOL },
	{ "#IF", AD_IF },
	{ "#IFDEF", AD_IFDEF },
	{ "#ELSE", AD_ELSE },
	{ "#ELIF", AD_ELIF },
	{ "#ENDIF", AD_ENDIF },
	{ "IF", AD_IF },
	{ "IFDEF", AD_IFDEF },
	{ "ELSE", AD_ELSE },
	{ "ELIF", AD_ELIF },
	{ "ENDIF", AD_ENDIF },
	{ "STRUCT", AD_STRUCT },
	{ "ENUM", AD_ENUM },
	{ "REPT", AD_REPT },
	{ "REPEAT", AD_REPT },		// ca65 version of rept
	{ "INCDIR", AD_INCDIR },
	{ "A16", AD_A16 },			// A16: Set 16 bit accumulator mode
	{ "A8", AD_A8 },			// A8: Set 8 bit accumulator mode
	{ "XY16", AD_XY16 },		// XY16: Set 16 bit index register mode
	{ "XY8", AD_XY8 },			// XY8: Set 8 bit index register mode
	{ "I16", AD_XY16 },			// I16: Set 16 bit index register mode
	{ "I8", AD_XY8 },			// I8: Set 8 bit index register mode
	{ "DUMMY", AD_DUMMY },
	{ "DUMMY_END", AD_DUMMY_END },
	{ "DS", AD_DS },			// Define space
};

// Merlin specific directives separated from regular directives to avoid confusion
DirectiveName aDirectiveNamesMerlin[] {
	{ "MX", AD_MX },			// MERLIN
	{ "STR", AD_LNK },			// MERLIN
	{ "DA", AD_WORDS },			// MERLIN
	{ "DW", AD_WORDS },			// MERLIN
	{ "ASC", AD_TEXT },			// MERLIN
	{ "PUT", AD_INCLUDE },		// MERLIN
	{ "DDB", AD_WORDS },		// MERLIN
	{ "DB", AD_BYTES },			// MERLIN
	{ "DFB", AD_BYTES },		// MERLIN
	{ "HEX", AD_HEX },			// MERLIN
	{ "DO", AD_IF },			// MERLIN
	{ "FIN", AD_ENDIF },		// MERLIN
	{ "EJECT", AD_EJECT },		// MERLIN
	{ "OBJ", AD_EJECT },		// MERLIN
	{ "TR", AD_EJECT },			// MERLIN
	{ "END", AD_EJECT },		// MERLIN
	{ "REL", AD_EJECT },		// MERLIN
	{ "USR", AD_USR },			// MERLIN
	{ "DUM", AD_DUMMY },		// MERLIN
	{ "DEND", AD_DUMMY_END },	// MERLIN
	{ "LST", AD_LST },			// MERLIN
	{ "LSTDO", AD_LST },		// MERLIN
	{ "LUP", AD_REPT },			// MERLIN
	{ "MAC", AD_MACRO },		// MERLIN
	{ "SAV", AD_SAV },			// MERLIN
	{ "DSK", AD_SAV },			// MERLIN
	{ "LNK", AD_LNK },			// MERLIN
	{ "XC", AD_XC },			// MERLIN
	{ "ENT", AD_ENT },			// MERLIN (xdef, but label on same line)
	{ "EXT", AD_EXT },			// MERLIN (xref, which are implied in x65 object files)
	{ "ADR", AD_ADR },			// ADR: MERLIN store 3 byte word
	{ "ADRL", AD_ADRL },		// ADRL: MERLIN store 4 byte word
	{ "CYC", AD_CYC },			// MERLIN: Start and stop cycle counter
};

static const int nDirectiveNames = sizeof(aDirectiveNames) / sizeof(aDirectiveNames[0]);
static const int nDirectiveNamesMerlin = sizeof(aDirectiveNamesMerlin) / sizeof(aDirectiveNamesMerlin[0]);

// Binary search over an array of unsigned integers, may contain multiple instances of same key
unsigned int FindLabelIndex(unsigned int hash, unsigned int *table, unsigned int count)
{
	unsigned int max = count;
	unsigned int first = 0;
	while (count!=first) {
		int index = (first+count)/2;
		unsigned int read = table[index];
		if (hash==read) {
			while (index && table[index-1]==hash)
				index--;	// guarantee first identical index returned on match
			return index;
		} else if (hash>read)
			first = index+1;
		else
			count = index;
	}
	if (count<max && table[count]<hash)
		count++;
	else if (count && table[count-1]>hash)
		count--;
	return count;
}



//
//
// ASSEMBLER STATE
//
//



// pairArray is basically two vectors sharing a size without constructors on growth or insert
template <class H, class V> class pairArray {
protected:
	H *keys;
	V *values;
	unsigned int _count;
	unsigned int _capacity;
public:
	pairArray() : keys(nullptr), values(nullptr), _count(0), _capacity(0) {}
	void reserve(unsigned int size) {
		if (size>_capacity) {
			H *new_keys = (H*)malloc(sizeof(H) * size); if (!new_keys) { return; }
			V *new_values = (V*)malloc(sizeof(V) * size); if (!new_values) { free(new_keys); return; }
			if (keys && values) {
				memcpy(new_keys, keys, sizeof(H) * _count);
				memcpy(new_values, values, sizeof(V) * _count);
				free(keys); free(values);
			}
			keys = new_keys;
			values = new_values;
			_capacity = size;
		}
	}
	bool insert(unsigned int pos) {
		if (pos>_count)
			return false;
		if (_count==_capacity)
			reserve(_capacity+64);
		if (pos<_count) {
			memmove(keys+pos+1, keys+pos, sizeof(H) * (_count-pos));
			memmove(values+pos+1, values+pos, sizeof(V) * (_count-pos));
		}
		memset(keys+pos, 0, sizeof(H));
		memset(values+pos, 0, sizeof(V));
		_count++;
		return true;
	}
	bool insert(unsigned int pos, H key) {
		if (insert(pos) && keys) {
			keys[pos] = key;
			return true;
		}
		return false;
	}
	void remove(unsigned int pos) {
		if (pos<_count) {
			_count--;
			if (pos<_count) {
				memmove(keys+pos, keys+pos+1, sizeof(H) * (_count-pos));
				memmove(values+pos, values+pos+1, sizeof(V) * (_count-pos));
			}
		}
	}
	H* getKeys() { return keys; }
	H& getKey(unsigned int pos) { return keys[pos]; }
	V* getValues() { return values; }
	V& getValue(unsigned int pos) { return values[pos]; }
	unsigned int count() const { return _count; }
	unsigned int capacity() const { return _capacity; }
	void clear() {
		if (keys!=nullptr)
			free(keys);
		keys = nullptr;
		if (values!=nullptr)
			free(values);
		values = nullptr;
		_capacity = 0;
		_count = 0;
	}
};

// relocs are cheaper than full expressions and work with
// local labels for relative sections which would otherwise
// be out of scope at link time.

struct Reloc {
	int base_value;
	int section_offset;		// offset into this section
	int target_section;		// which section does this reloc target?
	char bytes;				// number of bytes to write
	char shift;				// number of bits to shift to get value

	Reloc() : base_value(0), section_offset(-1), target_section(-1), bytes(0), shift(0) {}
	Reloc(int base, int offs, int sect, char num_bytes, char bit_shift) :
		base_value(base), section_offset(offs), target_section(sect), bytes(num_bytes), shift(bit_shift) {}
};
typedef std::vector<struct Reloc> relocList;

// For assembly listing this remembers the location of each line
struct ListLine {
	enum Flags {
		MNEMONIC = 0x01,
		KEYWORD = 0x02,
		CYCLES_START = 0x04,
		CYCLES_STOP = 0x08,
	};
	strref source_name;		// source file index name
	strref code;			// line of code this represents
	int address;			// start address of this line
	int size;				// number of bytes generated for this line
	int line_offs;			// offset into code
	int flags;				// only output code if generated by code

	bool wasMnemonic() const { return !!(flags & MNEMONIC);  }
	bool startClock() const { return !!(flags & CYCLES_START); }
	bool stopClock() const { return !!(flags & CYCLES_STOP); }
};
typedef std::vector<struct ListLine> Listing;

enum SectionType : char {
	ST_UNDEFINED,			// not set
	ST_CODE,				// default type
	ST_DATA,				// data section (matters for GS/OS OMF)
	ST_BSS,					// uninitialized data section
	ST_ZEROPAGE				// ununitialized data section in zero page / direct page
};

// start of data section support
// Default is a relative section
// Whenever org or dum with address is encountered => new section
// If org is fixed and < $200 then it is a dummy section Otherwise clear dummy section
typedef struct Section {
	// section name, same named section => append
	strref name;			// name of section for comparison
	strref export_append;	// append this name to export of file

	// generated address status
	int load_address;		// if assigned a load address
	int start_address;
	int address;			// relative or absolute PC
	int align_address;		// for relative sections that needs alignment

	// merged sections
	int merged_offset;		// -1 if not merged
	int merged_section;		// which section merged with

	// data output
	unsigned char *output;	// memory for this section
	unsigned char *curr;	// current pointer for this section
	size_t output_capacity;	// current output capacity

	// reloc data
	relocList *pRelocs;		// link time resolve (not all sections need this)
	Listing *pListing;		// if list output

	bool address_assigned;	// address is absolute if assigned
	bool dummySection;		// true if section does not generate data, only labels
	SectionType type;		// distinguishing section type for relocatable output

	void reset() {			// explicitly cleaning up sections, not called from Section destructor
		name.clear(); export_append.clear();
		start_address = address = load_address = 0x0; type = ST_CODE;
		address_assigned = false; output = nullptr; curr = nullptr;
		dummySection = false; output_capacity = 0; merged_offset = -1; merged_section = -1;
		align_address = 1; if (pRelocs) delete pRelocs;
		pRelocs = nullptr;
		if (pListing) delete pListing;
		pListing = nullptr;
	}

	void Cleanup() { if (output) free(output); reset(); }
	bool empty() const { return merged_offset<0 && curr==output; }

	int DataOffset() const { return int(curr - output); }
	size_t size() const { return curr - output; }
	const unsigned char *get() { return output; }

	int GetPC() const { return address; }
	void AddAddress(int value) { address += value; }
	void SetLoadAddress(int addr) { load_address = addr; }
	int GetLoadAddress() const { return load_address; }

	void SetDummySection(bool enable) { dummySection = enable; }
	bool IsDummySection() const { return dummySection; }
	bool IsRelativeSection() const { return address_assigned == false; }
	bool IsMergedSection() const { return merged_offset >= 0; }
	void AddReloc(int base, int offset, int section, char bytes, char shift);// Reloc::Type type = Reloc::WORD);

	Section() : pRelocs(nullptr), pListing(nullptr) { reset(); }
	Section(strref _name, int _address) : pRelocs(nullptr), pListing(nullptr) {
		reset(); name = _name; start_address = load_address = address = _address;
		address_assigned = true;
	}
	Section(strref _name) : pRelocs(nullptr), pListing(nullptr) {
		reset(); name = _name;
		start_address = load_address = address = 0; address_assigned = false;
	}
	~Section() { }

	// Append data to a section
	void CheckOutputCapacity(unsigned int addSize);
	void AddByte(int b);
	void AddWord(int w);
	void AddTriple(int l);
	void AddBin(unsigned const char *p, int size);
	void AddText(strref line, strref text_prefix);
	void SetByte(size_t offs, int b) { output[offs] = b; }
	void SetWord(size_t offs, int w) { output[offs] = w; output[offs+1] = w>>8; }
	void SetTriple(size_t offs, int w) { output[offs] = w; output[offs+1] = w>>8; output[offs+2] = w>>16; }
	void SetQuad(size_t offs, int w) { output[offs] = w; output[offs+1] = w>>8; output[offs+2] = w>>16; output[offs+3] = w>>24; }
} Section;

// Symbol list entry (in order of parsing)
struct MapSymbol {
	strref name;    // string name
	int value;
	short section;
	bool local;     // local variables
};
typedef std::vector<struct MapSymbol> MapSymbolArray;

// Data related to a label
typedef struct {
public:
	strref label_name;		// the name of this label
	strref pool_name;		// name of the pool that this label is related to
	int value;
	int section;			// rel section address labels belong to a section, -1 if fixed address or assigned
	int mapIndex;           // index into map symbols in case of late resolve
	bool evaluated;			// a value may not yet be evaluated
	bool pc_relative;		// this is an inline label describing a point in the code
	bool constant;			// the value of this label can not change
	bool external;			// this label is globally accessible
	bool reference;			// this label is accessed from external and can't be used for evaluation locally
} Label;

// If an expression can't be evaluated immediately, this is required
// to reconstruct the result when it can be.
typedef struct {
	enum Type {				// When an expression is evaluated late, determine how to encode the result
		LET_LABEL,			// this evaluation applies to a label and not memory
		LET_ABS_REF,		// calculate an absolute address and store at 0, +1
		LET_ABS_L_REF,		// calculate a bank + absolute address and store at 0, +1, +2
		LET_ABS_4_REF,		// calculate a 32 bit number
		LET_BRANCH,			// calculate a branch offset and store at this address
		LET_BRANCH_16,		// calculate a branch offset of 16 bits and store at this address
		LET_BYTE,			// calculate a byte and store at this address
	};
	int target;				// offset into output buffer
	int address;			// current pc
	int scope;				// scope pc
	int scope_depth;		// relevant for scope end
	short section;			// which section to apply to.
	short rept;				// value of rept
	int file_ref;			// -1 if current or xdef'd otherwise index of file for label
	strref label;			// valid if this is not a target but another label
	strref expression;
	strref source_file;
	Type type;
} LateEval;

// A macro is a text reference to where it was defined
typedef struct {
	strref name;
	strref macro;
	strref source_name;		// source file name (error output)
	strref source_file;		// entire source file (req. for line #)
	bool params_first_line;	// the first line of this macro are parameters
} Macro;

// All local labels are removed when a global label is defined but some when a scope ends
typedef struct {
	strref label;
	int scope_depth;
	bool scope_reserve;		// not released for global label, only scope	
} LocalLabelRecord;

// Label pools allows C like stack frame label allocation
typedef struct {
	strref pool_name;
	short numRanges;		// normally 1 range, support multiple for ease of use
	short scopeDepth;		// Required for scope closure cleanup
	unsigned short ranges[MAX_POOL_RANGES*2];		// 2 shorts per range
	unsigned int usedMap[(MAX_POOL_BYTES+15)>>4];	// 2 bits per byte to store byte count of label
	StatusCode Reserve(int numBytes, unsigned int &addr);
	StatusCode Release(unsigned int addr);
} LabelPool;

// One member of a label struct
struct MemberOffset {
	unsigned short offset;
	unsigned int name_hash;
	strref name;
	strref sub_struct;
};

// Label struct
typedef struct {
	strref name;
	unsigned short first_member;
	unsigned short numMembers;
	unsigned short size;
} LabelStruct;

// object file labels that are not xdef'd end up here
struct ExtLabels {
	pairArray<unsigned int, Label> labels;
};

// EvalExpression needs a location reference to work out some addresses
struct EvalContext {
	int pc;					// current address at point of eval
	int scope_pc;			// current scope open at point of eval
	int scope_end_pc;		// late scope closure after eval
	int scope_depth;		// scope depth for eval (must match current for scope_end_pc to eval)
	int relative_section;	// return can be relative to this section
	int file_ref;			// can access private label from this file or -1
	int rept_cnt;			// current repeat counter
	EvalContext() {}
	EvalContext(int _pc, int _scope, int _close, int _sect, int _rept_cnt) :
		pc(_pc), scope_pc(_scope), scope_end_pc(_close), scope_depth(-1),
		relative_section(_sect), file_ref(-1), rept_cnt(_rept_cnt) {}
};

// Source context is current file (include file, etc.) or current macro.
typedef struct {
	strref source_name;		// source file name (error output)
	strref source_file;		// entire source file (req. for line #)
	strref code_segment;	// the segment of the file for this context
	strref read_source;		// current position/length in source file
	strref next_source;		// next position/length in source file
	short repeat;			// how many times to repeat this code segment
	short repeat_total;		// initial number of repeats for this code segment
	bool scoped_context;
	void restart() { read_source = code_segment; }
	bool complete() { repeat--; return repeat <= 0; }
} SourceContext;

// Context stack is a stack of currently processing text
class ContextStack {
private:
	std::vector<SourceContext> stack;
	SourceContext *currContext;
public:
	ContextStack() : currContext(nullptr) { stack.reserve(32); }
	SourceContext& curr() { return *currContext; }
	const SourceContext& curr() const { return *currContext; }
	void push(strref src_name, strref src_file, strref code_seg, int rept = 1) {
		if (currContext)
			currContext->read_source = currContext->next_source;
		SourceContext context;
		context.source_name = src_name;
		context.source_file = src_file;
		context.code_segment = code_seg;
		context.read_source = code_seg;
		context.next_source = code_seg;
		context.repeat = rept;
		context.repeat_total = rept;
		context.scoped_context = false;
		stack.push_back(context);
		currContext = &stack[stack.size()-1];
	}
	void pop() { stack.pop_back(); currContext = stack.size() ? &stack[stack.size()-1] : nullptr; }
	bool has_work() { return currContext!=nullptr; }
};

// The state of the assembler
class Asm {
public:
	pairArray<unsigned int, Label> labels;
	pairArray<unsigned int, Macro> macros;
	pairArray<unsigned int, LabelPool> labelPools;
	pairArray<unsigned int, LabelStruct> labelStructs;
	pairArray<unsigned int, strref> xdefs;	// labels matching xdef names will be marked as external

	std::vector<LateEval> lateEval;
	std::vector<LocalLabelRecord> localLabels;
	std::vector<char*> loadedData;			// free when assembler is completed
	std::vector<MemberOffset> structMembers; // labelStructs refer to sets of structMembers
	std::vector<strref> includePaths;
	std::vector<Section> allSections;
	std::vector<ExtLabels> externals;		// external labels organized by object file
	MapSymbolArray map;

	// CPU target
	struct mnem *opcode_table;
	int opcode_count;
	CPUIndex cpu, list_cpu;
	OPLookup aInstructions[MAX_OPCODES_DIRECTIVES];
	int num_instructions;

	// context for macros / include files
	ContextStack contextStack;

	// Current section
	Section *current_section;

	// Special syntax rules
	AsmSyntax syntax;

	// Conditional assembly vars
	int conditional_depth;
	strref conditional_source[MAX_CONDITIONAL_DEPTH];	// start of conditional for error report
	char conditional_nesting[MAX_CONDITIONAL_DEPTH];
	bool conditional_consumed[MAX_CONDITIONAL_DEPTH];

	// Scope info
	int scope_address[MAX_SCOPE_DEPTH];
	int scope_depth;

	// Eval relative result (only valid if EvalExpression returns STATUS_RELATIVE_SECTION)
	int lastEvalSection;
	int lastEvalValue;
	char lastEvalShift;

	strref export_base_name;	// binary output name if available
	strref last_label;			// most recently defined label for Merlin macro
	char list_flags;			// listing flags accumulating for each line
	bool accumulator_16bit;		// 65816 specific software dependent immediate mode
	bool index_reg_16bit;		// -"-
	char cycle_counter_level;	// merlin toggles the cycle counter rather than hierarchically evals
	bool error_encountered;		// if any error encountered, don't export binary
	bool list_assembly;			// generate assembler listing
	bool end_macro_directive;	// whether to use { } or macro / endmacro for macro scope
	bool link_all_section;		// link all known relative sections to this section at end

	// Convert source to binary
	void Assemble(strref source, strref filename, bool obj_target);

	// Generate assembler listing if requested
	bool List(strref filename);

	// Generate source for all valid instructions and addressing modes for current CPU
	bool AllOpcodes(strref filename);

	// Clean up memory allocations, reset assembler state
	void Cleanup();

	// Make sure there is room to write more code
	void CheckOutputCapacity(unsigned int addSize);

	// Operations on current section
	void SetSection(strref name, int address);	// fixed address section
	void SetSection(strref name);				// relative address section
	StatusCode AppendSection(Section &relSection, Section &trgSection);
	StatusCode LinkSections(strref name);		// link relative address sections with this name here
	void LinkLabelsToAddress(int section_id, int section_address);
	StatusCode LinkRelocs(int section_id, int section_address);
	void DummySection(int address);				// non-data section (fixed)
	void DummySection();						// non-data section (relative)
	void EndSection();							// pop current section
	void LinkAllToSection();					// link all loaded relative sections to this section at end
	Section& CurrSection() { return *current_section; }
	unsigned char* BuildExport(strref append, int &file_size, int &addr);
	int GetExportNames(strref *aNames, int maxNames);
	StatusCode LinkZP();
	int SectionId() { return int(current_section - &allSections[0]); }
	void AddByte(int b) { CurrSection().AddByte(b); }
	void AddWord(int w) { CurrSection().AddWord(w); }
	void AddTriple(int l) { CurrSection().AddTriple(l); }
	void AddBin(unsigned const char *p, int size) { CurrSection().AddBin(p, size); }

	// Object file handling
	StatusCode WriteObjectFile(strref filename);	// write x65 object file
	StatusCode ReadObjectFile(strref filename);		// read x65 object file

	// Scope management
	StatusCode EnterScope();
	StatusCode ExitScope();

	// Macro management
	StatusCode AddMacro(strref macro, strref source_name, strref source_file, strref &left);
	StatusCode BuildMacro(Macro &m, strref arg_list);

	// Structs
	StatusCode BuildStruct(strref name, strref declaration);
	StatusCode EvalStruct(strref name, int &value);
	StatusCode BuildEnum(strref name, strref declaration);

	// Calculate a value based on an expression.
	EvalOperator RPNToken_Merlin(strref &expression, const struct EvalContext &etx,
								 EvalOperator prev_op, short &section, int &value);
	EvalOperator RPNToken(strref &expression, const struct EvalContext &etx,
						  EvalOperator prev_op, short &section, int &value);
	StatusCode EvalExpression(strref expression, const struct EvalContext &etx, int &result);
	void SetEvalCtxDefaults(struct EvalContext &etx);
	int ReptCnt() const;

	// Access labels
	Label* GetLabel(strref label);
	Label* GetLabel(strref label, int file_ref);
	Label* AddLabel(unsigned int hash);
	bool MatchXDEF(strref label);
	StatusCode AssignLabel(strref label, strref line, bool make_constant = false);
	StatusCode AddressLabel(strref label);
	void LabelAdded(Label *pLabel, bool local = false);
	void IncludeSymbols(strref line);

	// Manage locals
	void MarkLabelLocal(strref label, bool scope_label = false);
	StatusCode FlushLocalLabels(int scope_exit = -1);

	// Label pools
	LabelPool* GetLabelPool(strref pool_name);
	StatusCode AddLabelPool(strref name, strref args);
	StatusCode AssignPoolLabel(LabelPool &pool, strref args);
	void FlushLabelPools(int scope_exit);

	// Late expression evaluation
	void AddLateEval(int target, int pc, int scope_pc, strref expression,
					 strref source_file, LateEval::Type type);
	void AddLateEval(strref label, int pc, int scope_pc,
					 strref expression, LateEval::Type type);
	StatusCode CheckLateEval(strref added_label = strref(), int scope_end = -1, bool missing_is_error = false);

	// Assembler Directives
	StatusCode ApplyDirective(AssemblerDirective dir, strref line, strref source_file);
	StatusCode Directive_Rept(strref line, strref source_file);
	StatusCode Directive_Macro(strref line, strref source_file);
	StatusCode Directive_Include(strref line);
	StatusCode Directive_Incbin(strref line, int skip=0, int len=0);
	StatusCode Directive_Import(strref line);
	StatusCode Directive_ORG(strref line);
	StatusCode Directive_LOAD(strref line);
	StatusCode Directive_LNK(strref line);
	StatusCode Directive_XDEF(strref line);
	StatusCode Directive_XREF(strref label);

	// Assembler steps
	StatusCode GetAddressMode(strref line, bool flipXY, unsigned int validModes,
							  AddrMode &addrMode, int &len, strref &expression);
	StatusCode AddOpcode(strref line, int index, strref source_file);
	StatusCode BuildLine(strref line);
	StatusCode BuildSegment();

	// Display error in stderr
	void PrintError(strref line, StatusCode error);

	// Conditional Status
	bool ConditionalAsm();			// Assembly is currently enabled
	bool NewConditional();			// Start a new conditional block
	void CloseConditional();		// Close a conditional block
	void CheckConditionalDepth();	// Check if this conditional will nest the assembly (a conditional is already consumed)
	void ConsumeConditional();		// This conditional block is going to be assembled, mark it as consumed
	bool ConditionalConsumed();		// Has a block of this conditional already been assembled?
	void SetConditional();			// This conditional block is not going to be assembled so mark that it is nesting
	bool ConditionalAvail();		// Returns true if this conditional can be consumed
	void ConditionalElse();	// Conditional else that does not enable block
	void EnableConditional(bool enable); // This conditional block is enabled and the prior wasn't

	// Conditional statement evaluation (A==B? A?)
	StatusCode EvalStatement(strref line, bool &result);

	// Add include folder
	void AddIncludeFolder(strref path);
	char* LoadText(strref filename, size_t &size);
	char* LoadBinary(strref filename, size_t &size);

	// Change CPU
	void SetCPU(CPUIndex CPU);

	// constructor
	Asm() : opcode_table(opcodes_6502), opcode_count(num_opcodes_6502), num_instructions(0),
		cpu(CPU_6502), list_cpu(CPU_6502) {
		Cleanup(); localLabels.reserve(256); loadedData.reserve(16); lateEval.reserve(64); }
};

// Clean up work allocations
void Asm::Cleanup() {
	for (std::vector<char*>::iterator i = loadedData.begin(); i != loadedData.end(); ++i) {
		if (char *data = *i)
			free(data);
	}
	map.clear();
	labelPools.clear();
	loadedData.clear();
	labels.clear();
	macros.clear();
	allSections.clear();
	for (std::vector<ExtLabels>::iterator exti = externals.begin(); exti !=externals.end(); ++exti)
		exti->labels.clear();
	externals.clear();
	link_all_section = false;
	// this section is relocatable but is assigned address $1000 if exporting without directives
	SetSection(strref("default"));
	current_section = &allSections[0];
	syntax = SYNTAX_SANE;
	scope_depth = 0;
	conditional_depth = 0;
	conditional_nesting[0] = 0;
	conditional_consumed[0] = false;
	error_encountered = false;
	list_assembly = false;
	end_macro_directive = false;
	accumulator_16bit = false;	// default 65816 8 bit immediate mode
	index_reg_16bit = false;	// other CPUs won't be affected.
	cycle_counter_level = 0;
}

int sortHashLookup(const void *A, const void *B) {
	const OPLookup *_A = (const OPLookup*)A;
	const OPLookup *_B = (const OPLookup*)B;
	return _A->op_hash > _B->op_hash ? 1 : -1;
}

int BuildInstructionTable(OPLookup *pInstr, int maxInstructions, struct mnem *opcodes,
						  int count, const char **aliases, bool merlin)
{
	// create an instruction table (mnemonic hash lookup)
	int numInstructions = 0;
	for (int i = 0; i < count; i++) {
		OPLookup &op = pInstr[numInstructions++];
		op.op_hash = strref(opcodes[i].instr).fnv1a_lower();
		op.index = i;
		op.type = OT_MNEMONIC;
	}

	// add instruction aliases
	if (aliases) {
		while (*aliases) {
			strref orig(*aliases++);
			strref alias(*aliases++);
			for (int o=0; o<count; o++) {
				if (orig.same_str_case(opcodes[o].instr)) {
					OPLookup &op = pInstr[numInstructions++];
					op.op_hash = alias.fnv1a_lower();
					op.index = o;
					op.type = OT_MNEMONIC;
					break;
				}
			}
		}
	}
	
	// add assembler directives
	for (int d = 0; d<nDirectiveNames; d++) {
		OPLookup &op_hash = pInstr[numInstructions++];
		op_hash.op_hash = strref(aDirectiveNames[d].name).fnv1a_lower();
		op_hash.index = (unsigned char)aDirectiveNames[d].directive;
		op_hash.type = OT_DIRECTIVE;
	}
	
	if (merlin) {
		for (int d = 0; d<nDirectiveNamesMerlin; d++) {
			OPLookup &op_hash = pInstr[numInstructions++];
			op_hash.op_hash = strref(aDirectiveNamesMerlin[d].name).fnv1a_lower();
			op_hash.index = (unsigned char)aDirectiveNamesMerlin[d].directive;
			op_hash.type = OT_DIRECTIVE;
		}
	}
	
	// sort table by hash for binary search lookup
	qsort(pInstr, numInstructions, sizeof(OPLookup), sortHashLookup);
	return numInstructions;
}

// Change the instruction set
void Asm::SetCPU(CPUIndex CPU) {
	cpu = CPU;
	if (cpu > list_cpu)
		list_cpu = cpu;
	opcode_table = aCPUs[CPU].opcodes;
	opcode_count = aCPUs[CPU].num_opcodes;
	num_instructions = BuildInstructionTable(aInstructions, MAX_OPCODES_DIRECTIVES, opcode_table,
											 opcode_count, aCPUs[CPU].aliases, syntax == SYNTAX_MERLIN);
}

// Read in text data (main source, include, etc.)
char* Asm::LoadText(strref filename, size_t &size) {
	strown<512> file(filename);
	std::vector<strref>::iterator i = includePaths.begin();
	for (;;) {
		if (FILE *f = fopen(file.c_str(), "rb")) {	// rb is intended here since OS
			fseek(f, 0, SEEK_END);					// eol conversion can do ugly things
			size_t _size = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (char *buf = (char*)calloc(_size, 1)) {
				fread(buf, _size, 1, f);
				fclose(f);
				size = _size;
				return buf;
			}
			fclose(f);
		}
		if (i==includePaths.end())
			break;
		file.copy(*i);
		if (file.get_last()!='/' && file.get_last()!='\\')
			file.append('/');
		file.append(filename);
		++i;
	}
	size = 0;
	return nullptr;
}

// Read in binary data (incbin)
char* Asm::LoadBinary(strref filename, size_t &size) {
	strown<512> file(filename);
	std::vector<strref>::iterator i = includePaths.begin();
	for (;;) {
		if (FILE *f = fopen(file.c_str(), "rb")) {
			fseek(f, 0, SEEK_END);
			size_t _size = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (char *buf = (char*)malloc(_size)) {
				fread(buf, _size, 1, f);
				fclose(f);
				size = _size;
				return buf;
			}
			fclose(f);
		}
		if (i==includePaths.end())
			break;
		file.copy(*i);
		if (file.get_last()!='/' && file.get_last()!='\\')
			file.append('/');
		file.append(filename);
#ifdef WIN32
		file.replace('/', '\\');
#endif
		++i;
	}
	size = 0;
	return nullptr;
}

// Create a new section with a fixed address
void Asm::SetSection(strref name, int address)
{
	if (name) {
		for (std::vector<Section>::iterator i = allSections.begin(); i!=allSections.end(); ++i) {
			if (i->name && name.same_str(i->name)) {
				current_section = &*i;
				return;
			}
		}
	}
	if (link_all_section)
		LinkAllToSection();
	if (allSections.size()==allSections.capacity())
		allSections.reserve(allSections.size() + 16);
	Section newSection(name, address);
	if (address < 0x200)	// don't compile over zero page and stack frame (may be bad assumption)
		newSection.SetDummySection(true);
	allSections.push_back(newSection);
	current_section = &allSections[allSections.size()-1];
}

void Asm::SetSection(strref line)
{
	if (link_all_section)
		LinkAllToSection();
	if (allSections.size()==allSections.capacity())
		allSections.reserve(allSections.size() + 16);

	SectionType type = ST_UNDEFINED;

	// SEG.U etc.
	if (line.get_first() == '.') {
		++line;
		switch (strref::tolower(line.get_first())) {
			case 'u': type = ST_BSS; break;
			case 'z': type = ST_ZEROPAGE; break;
			case 'd': type = ST_DATA; break;
			case 'c': type = ST_CODE; break;
		}
	}
	line.trim_whitespace();

	int align = 1;
	strref name;
	while (strref arg = line.split_token_any_trim(",:")) {
		if (arg.get_first() == '$') { ++arg; align = arg.ahextoui(); }
		else if (arg.is_number()) align = arg.atoi();
		else if (arg.get_first() == '"') name = (arg + 1).before_or_full('"');
		else if (!name) name = arg;
		else if (arg.same_str("code")) type = ST_CODE;
		else if (arg.same_str("data")) type = ST_DATA;
		else if (arg.same_str("bss")) type = ST_BSS;
		else if (arg.same_str("zp") || arg.same_str("dp") ||
			arg.same_str("zeropage") || arg.same_str("direct")) type = ST_ZEROPAGE;
	}
	if (type == ST_UNDEFINED) {
		if (name.find("code") >= 0) type = ST_CODE;
		else if (name.find("data") >= 0) type = ST_DATA;
		else if (name.find("bss") >= 0) 	type = ST_BSS;
		else if (name.find("zp") >= 0 || name.find("zeropage") >= 0 || name.find("direct") >= 0)
			type = ST_ZEROPAGE;
		else type = ST_CODE;
	}

	Section newSection(name);
	newSection.align_address = align;
	newSection.type = type;
	allSections.push_back(newSection);
	current_section = &allSections[allSections.size()-1];
}

// Merlin linking includes one file at a time ignoring the section naming
// so just wait until the end of the current section to link all unlinked
// sections.
void Asm::LinkAllToSection() {
	if (CurrSection().IsDummySection())
		return;
	bool gotRelSect = true;
	while (gotRelSect) {
		gotRelSect = false;
		for (std::vector<Section>::iterator s = allSections.begin(); s!=allSections.end(); ++s) {
			if (s->IsRelativeSection()) {
				LinkSections(s->name);
				gotRelSect = true;
				break;
			}
		}
	}
	link_all_section = false;
}

// Fixed address dummy section
void Asm::DummySection(int address) {
	if (link_all_section)
		LinkAllToSection();
	if (allSections.size()==allSections.capacity())
		allSections.reserve(allSections.size() + 16);
	Section newSection(strref(), address);
	newSection.SetDummySection(true);
	allSections.push_back(newSection);
	current_section = &allSections[allSections.size()-1];
}

// Current address dummy section
void Asm::DummySection() {
	DummySection(CurrSection().GetPC());
}

void Asm::EndSection() {
	if (link_all_section)
		LinkAllToSection();
	int section = (int)(current_section - &allSections[0]);
	if (section)
		current_section = &allSections[section-1];
}

// list all export append names
// for each valid export append name build a binary fixed address code
//	- find lowest and highest address
//	- alloc & 0 memory
//	- any matching relative sections gets linked in after
//	- go through all section that matches export_append in order and copy over memory
unsigned char* Asm::BuildExport(strref append, int &file_size, int &addr)
{
	int start_address = 0x7fffffff;
	int end_address = 0;

	bool has_relative_section = false;
	bool has_fixed_section = false;
	int last_fixed_section = -1;

	// find address range
	while (!has_relative_section && !has_fixed_section) {
		int section_id = 0;
		for (std::vector<Section>::iterator i = allSections.begin(); i != allSections.end(); ++i) {
			if (((!append && !i->export_append) || append.same_str_case(i->export_append)) && i->type != ST_ZEROPAGE) {
				if (!i->IsMergedSection()) {
					if (i->IsRelativeSection())
						has_relative_section = true;
					else if (i->start_address >= 0x100 && i->size() > 0) {
						has_fixed_section = true;
						if (i->start_address < start_address)
							start_address = i->start_address;
						if ((i->start_address + (int)i->size()) > end_address) {
							end_address = i->start_address + (int)i->size();
							last_fixed_section = section_id;
						}
					}
				}
			}
			section_id++;
		}
		if (!has_relative_section && !has_fixed_section)
			return nullptr;
		if (has_relative_section) {
			if (!has_fixed_section) {
				start_address = 0x1000;
				SetSection(strref(), start_address);
				CurrSection().export_append = append;
				last_fixed_section = SectionId();
			}
			for (std::vector<Section>::iterator i = allSections.begin(); i != allSections.end(); ++i) {
				if (((!append && !i->export_append) || append.same_str_case(i->export_append)) && i->type != ST_ZEROPAGE) {
					if (i->IsRelativeSection()) {
						StatusCode status = AppendSection(*i, allSections[last_fixed_section]);
						if (status != STATUS_OK)
							return nullptr;
						Section &s = allSections[last_fixed_section];
						end_address = s.start_address +
							(int)allSections[last_fixed_section].size();
					}
				}
			}
		}
	}

	// check if valid
	if (end_address <= start_address)
		return nullptr;

	// get memory for output buffer
	unsigned char *output = (unsigned char*)calloc(1, end_address - start_address);

	// copy over in order
	for (std::vector<Section>::iterator i = allSections.begin(); i != allSections.end(); ++i) {
		if ((!append && !i->export_append) || append.same_str_case(i->export_append)) {
			if (i->merged_offset == -1 && i->start_address >= 0x200 && i->size() > 0)
				memcpy(output + i->start_address - start_address, i->output, i->size());
		}
	}

	// return the result
	file_size = end_address - start_address;
	addr = start_address;
	return output;
}

// Collect all the export names
int Asm::GetExportNames(strref *aNames, int maxNames)
{
	int count = 0;
	for (std::vector<Section>::iterator i = allSections.begin(); i != allSections.end(); ++i) {
		if (!i->IsMergedSection()) {
			bool found = false;
			unsigned int hash = i->export_append.fnv1a_lower();
			for (int n = 0; n < count; n++) {
				if (aNames[n].fnv1a_lower() == hash) {
					found = true;
					break;
				}
			}
			if (!found && count < maxNames)
				aNames[count++] = i->export_append;
		}
	}
	return count;
}

// Collect all unassigned ZP sections and link them
StatusCode Asm::LinkZP()
{
	unsigned char min_addr = 0xff, max_addr = 0x00;
	int num_addr = 0;
	bool has_assigned = false, has_unassigned = false;
	int first_unassigned = -1;

	// determine if any zeropage section has been asseigned
	for (std::vector<Section>::iterator s = allSections.begin(); s != allSections.end(); ++s) {
		if (s->type == ST_ZEROPAGE && !s->IsMergedSection()) {
			if (s->address_assigned) {
				has_assigned = true;
				if (s->start_address < min_addr)
					min_addr = s->start_address;
				else if (s->address > max_addr)
					max_addr = s->address;
			} else {
				has_unassigned = true;
				first_unassigned = first_unassigned >=0 ? first_unassigned : (int)(&*s - &allSections[0]);
			}
			num_addr += s->address - s->start_address;
		}
	}

	if (num_addr > 0x100)
		return ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE;

	// no unassigned zp section, nothing to fix
	if (!has_unassigned)
		return STATUS_OK;

	// no section assigned => fit together at end
	if (!has_assigned) {
		int address = 0x100 - num_addr;
		for (std::vector<Section>::iterator s = allSections.begin(); s != allSections.end(); ++s) {
			if (s->type == ST_ZEROPAGE && !s->IsMergedSection()) {
				s->start_address = address;
				s->address += address;
				s->address_assigned = true;
				int section_id = (int)(&*s - &allSections[0]);
				LinkLabelsToAddress(section_id, s->start_address);
				StatusCode ret = LinkRelocs(section_id, s->start_address);
				if (ret >= FIRST_ERROR)
					return ret;
				address += s->address - s->start_address;
			}
		}
	} else {	// find first fit neighbouring an address assigned zero page section
		for (std::vector<Section>::iterator s = allSections.begin(); s != allSections.end(); ++s) {
			if (s->type == ST_ZEROPAGE && !s->IsMergedSection() && !s->address_assigned) {
				int size = s->address - s->start_address;
				bool found = false;
				// find any assigned address section and try to place before or after
				for (std::vector<Section>::iterator sa = allSections.begin(); sa != allSections.end(); ++sa) {
					if (sa->type == ST_ZEROPAGE && !sa->IsMergedSection() && sa->address_assigned) {
						for (int e = 0; e < 2; ++e) {
							int start = e ? sa->start_address - size : sa->address;
							int end = start + size;
							if (start >= 0 && end <= 0x100) {
								for (std::vector<Section>::iterator sc = allSections.begin(); !found && sc != allSections.end(); ++sc) {
									found = true;
									if (&*sa != &*sc && sc->type == ST_ZEROPAGE && !sc->IsMergedSection() && sc->address_assigned) {
										if (start <= sc->address && sc->start_address <= end)
											found = false;
									}
								}
							}
							if (found) {
								s->start_address = start;
								s->address += end;
								s->address_assigned = true;
								int section_id = (int)(&*s - &allSections[0]);
								LinkLabelsToAddress(section_id, s->start_address);
								StatusCode ret = LinkRelocs(section_id, s->start_address);
								if (ret >= FIRST_ERROR)
									return ret;
							}
						}
					}
				}
				if (!found)
					return ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE;
			}
		}
	}
	return STATUS_OK;
}


// Apply labels assigned to addresses in a relative section a fixed address
void Asm::LinkLabelsToAddress(int section_id, int section_address)
{
	Label *pLabels = labels.getValues();
	int numLabels = labels.count();
	for (int l = 0; l < numLabels; l++) {
		if (pLabels->section == section_id) {
			pLabels->value += section_address;
			pLabels->section = -1;
			if (pLabels->mapIndex>=0 && pLabels->mapIndex<(int)map.size()) {
				struct MapSymbol &msym = map[pLabels->mapIndex];
				msym.value = pLabels->value;
				msym.section = -1;
			}
			CheckLateEval(pLabels->label_name);
		}
		++pLabels;
	}
}

// go through relocs in all sections to see if any targets this section
// relocate section to address!
StatusCode Asm::LinkRelocs(int section_id, int section_address)
{
	for (std::vector<Section>::iterator j = allSections.begin(); j != allSections.end(); ++j) {
		Section &s2 = *j;
		if (s2.pRelocs) {
			relocList *pList = s2.pRelocs;
			relocList::iterator i = pList->end();
			while (i != pList->begin()) {
				--i;
				if (i->target_section == section_id) {
					Section *trg_sect = &s2;
					size_t output_offs = 0;
					while (trg_sect->merged_offset>=0) {
						output_offs += trg_sect->merged_offset;
						trg_sect = &allSections[trg_sect->merged_section];
					}
					unsigned char *trg = trg_sect->output + output_offs + i->section_offset;
					int value = i->base_value + section_address;
					if (i->shift<0)
						value >>= -i->shift;
					else if (i->shift)
						value <<= i->shift;
					
					for (int b = 0; b<i->bytes; b++)
						*trg++ = (unsigned char)(value>>(b*8));
					i = pList->erase(i);
					if (i != pList->end())
						++i;
				}
			}
			if (pList->empty()) {
				free(pList);
				s2.pRelocs = nullptr;
			}
		}
	}
	return STATUS_OK;
}

// Append one section to the end of another
StatusCode Asm::AppendSection(Section &s, Section &curr)
{
	int section_id = int(&s - &allSections[0]);
	if (s.IsRelativeSection() && !s.IsMergedSection()) {
		int section_size = (int)s.size();

		int section_address = curr.GetPC();

		// calculate space for alignment
		int align_size = s.align_address <= 1 ? 0 :
			(s.align_address-(section_address % s.align_address)) % s.align_address;

		// Get base addresses
		curr.CheckOutputCapacity(section_size + align_size);

		// add 0's
		for (int a = 0; a<align_size; a++)
			curr.AddByte(0);

		section_address += align_size;

		curr.CheckOutputCapacity((int)s.size());
		unsigned char *section_out = curr.curr;
		if (s.output)
			memcpy(section_out, s.output, s.size());
		curr.address += (int)s.size();
		curr.curr += s.size();
		free(s.output);
		s.output = 0;
		s.curr = 0;
		s.output_capacity = 0;

		// Update address range and mark section as merged
		s.start_address = section_address;
		s.address += section_address;
		s.address_assigned = true;
		s.merged_section = (int)(&curr - &allSections[0]);
		s.merged_offset = (int)(section_out - CurrSection().output);

		// Merge in the listing at this point
		if (s.pListing) {
			if (!curr.pListing)
				curr.pListing = new Listing;
			if ((curr.pListing->size() + s.pListing->size()) > curr.pListing->capacity())
				curr.pListing->reserve(curr.pListing->size() + s.pListing->size() + 256);
			for (Listing::iterator si = s.pListing->begin(); si != s.pListing->end(); ++si) {
				struct ListLine lst = *si;
				lst.address += s.merged_offset;
				curr.pListing->push_back(lst);
			}
			delete s.pListing;
			s.pListing = nullptr;
		}


		// All labels in this section can now be assigned
		LinkLabelsToAddress(section_id, section_address);

		return LinkRelocs(section_id, section_address);
	}
	return ERROR_CANT_APPEND_SECTION_TO_TARGET;
}

// Link sections with a specific name at this point
StatusCode Asm::LinkSections(strref name) {
	if (CurrSection().IsRelativeSection())
		return ERROR_LINKER_MUST_BE_IN_FIXED_ADDRESS_SECTION;
	if (CurrSection().IsDummySection())
		return ERROR_LINKER_CANT_LINK_TO_DUMMY_SECTION;

	for (std::vector<Section>::iterator i = allSections.begin(); i != allSections.end(); ++i) {
		if ((!name || i->name.same_str_case(name)) && i->IsRelativeSection() && !i->IsMergedSection()) {
			// Zero page sections can only be linked with zero page sections
			if (i->type != ST_ZEROPAGE || CurrSection().type == ST_ZEROPAGE) {
				StatusCode status = AppendSection(*i, CurrSection());
				if (status != STATUS_OK)
					return status;
			}
		}
	}
	return STATUS_OK;
}

// Section based output capacity
// Make sure there is room to assemble in
void Section::CheckOutputCapacity(unsigned int addSize) {
	if (dummySection || type == ST_ZEROPAGE || type == ST_BSS)
		return;
	size_t currSize = curr - output;
	if ((addSize + currSize) >= output_capacity) {
		size_t newSize = currSize * 2;
		if (newSize < 64*1024)
			newSize = 64*1024;
		if ((addSize+currSize) > newSize)
			newSize += newSize;
		unsigned char *new_output = (unsigned char*)malloc(newSize);
		curr = new_output + (curr-output);
		free(output);
		output = new_output;
		output_capacity = newSize;
	}
}

// Add one byte to a section
void Section::AddByte(int b) {
	if (!dummySection && type != ST_ZEROPAGE && type != ST_BSS) {
		CheckOutputCapacity(1);
		*curr++ = (unsigned char)b;
	}
	address++;
}

// Add a 16 bit word to a section
void Section::AddWord(int w) {
	if (!dummySection && type != ST_ZEROPAGE && type != ST_BSS) {
		CheckOutputCapacity(2);
		*curr++ = (unsigned char)(w&0xff);
		*curr++ = (unsigned char)(w>>8);
	}
	address += 2;
}

// Add a 24 bit word to a section
void Section::AddTriple(int l) {
	if (!dummySection && type != ST_ZEROPAGE && type != ST_BSS) {
		CheckOutputCapacity(3);
		*curr++ = (unsigned char)(l&0xff);
		*curr++ = (unsigned char)(l>>8);
		*curr++ = (unsigned char)(l>>16);
	}
	address += 3;
}
// Add arbitrary length data to a section
void Section::AddBin(unsigned const char *p, int size) {
	if (!dummySection && type != ST_ZEROPAGE && type != ST_BSS) {
		CheckOutputCapacity(size);
		memcpy(curr, p, size);
		curr += size;
	}
	address += size;
}

// Add text data to a section
void Section::AddText(strref line, strref text_prefix) {
	// https://en.wikipedia.org/wiki/PETSCII
	// ascii: no change
	// shifted: a-z => $41.. A-Z => $61..
	// unshifted: a-z, A-Z => $41

	CheckOutputCapacity(line.get_len());
	{
		if (!text_prefix || text_prefix.same_str("ascii")) {
			AddBin((unsigned const char*)line.get(), line.get_len());
		} else if (text_prefix.same_str("petscii")) {
			while (line) {
				char c = line[0];
				AddByte((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : (c > 0x60 ? ' ' : line[0]));
				++line;
			}
		} else if (text_prefix.same_str("petscii_shifted")) {
			while (line) {
				char c = line[0];
				AddByte((c >= 'a' && c <= 'z') ? (c - 'a' + 0x61) :
						((c >= 'A' && c <= 'Z') ? (c - 'A' + 0x61) : (c > 0x60 ? ' ' : line[0])));
				++line;
			}
		}
	}
}

// Add a relocation marker to a section
void Section::AddReloc(int base, int offset, int section, char bytes, char shift)
{
	if (!pRelocs)
		pRelocs = new relocList;
	if (pRelocs->size() == pRelocs->capacity())
		pRelocs->reserve(pRelocs->size() + 32);
	pRelocs->push_back(Reloc(base, offset, section, bytes, shift));
}

// Make sure there is room to assemble in
void Asm::CheckOutputCapacity(unsigned int addSize) {
	CurrSection().CheckOutputCapacity(addSize);
}


//
//
// SCOPE MANAGEMENT
//
//


StatusCode Asm::EnterScope()
{
	if (scope_depth >= (MAX_SCOPE_DEPTH - 1))
		return ERROR_TOO_DEEP_SCOPE;
	scope_address[++scope_depth] = CurrSection().GetPC();
	return STATUS_OK;
}

StatusCode Asm::ExitScope()
{
	CheckLateEval(strref(), CurrSection().GetPC());
	FlushLocalLabels(scope_depth);
	FlushLabelPools(scope_depth);
	--scope_depth;
	if (scope_depth<0)
		return ERROR_UNBALANCED_SCOPE_CLOSURE;
	return STATUS_OK;
}



//
//
// MACROS
//
//



// add a custom macro
StatusCode Asm::AddMacro(strref macro, strref source_name, strref source_file, strref &left)
{	//
	// Recommended macro syntax:
	// macro name(optional params) { actual macro }
	//
	// -endm option macro syntax:
	// macro name arg\nactual macro\nendmacro
	//
	// Merlin macro syntax: (TODO: ignore arguments and use ]1, ]2, etc.)
	// name mac arg1 arg2\nactual macro\n[<<<]/[EOM]
	//
	strref name;
	bool params_first_line = false;
	if (syntax == SYNTAX_MERLIN) {
		if (Label *pLastLabel = GetLabel(last_label)) {
			labels.remove((unsigned int)(pLastLabel - labels.getValues()));
			name = last_label;
			last_label.clear();
			macro.skip_whitespace();
			if (macro.get_first()==';' || macro.has_prefix(c_comment))
				macro.line();
			else
				params_first_line = true;
		} else
			return ERROR_BAD_MACRO_FORMAT;
	} else {
		name = macro.split_label();
		strref left_line = macro.get_line();
		left_line.skip_whitespace();
		left_line = left_line.before_or_full(';').before_or_full(c_comment);
		if (left_line && left_line[0] != '(' && left_line[0] != '{')
			params_first_line = true;
	}
	unsigned int hash = name.fnv1a();
	unsigned int ins = FindLabelIndex(hash, macros.getKeys(), macros.count());
	Macro *pMacro = nullptr;
	while (ins < macros.count() && macros.getKey(ins)==hash) {
		if (name.same_str_case(macros.getValue(ins).name)) {
			pMacro = macros.getValues() + ins;
			break;
		}
		++ins;
	}
	if (!pMacro) {
		macros.insert(ins, hash);
		pMacro = macros.getValues() + ins;
	}
	pMacro->name = name;
	if (syntax == SYNTAX_MERLIN) {
		strref source = macro;
		while (strref next_line = macro.line()) {
			next_line = next_line.before_or_full(';');
			next_line = next_line.before_or_full(c_comment);
			int term = next_line.find("<<<");
			if (term < 0)
				term = next_line.find("EOM");
			if (term >= 0) {
				strl_t macro_len = strl_t(next_line.get() + term - source.get());
				source = source.get_substr(0, macro_len);
				break;
			}
		}
		left = macro;
		pMacro->macro = source;
		source.skip_whitespace();
	} else if (end_macro_directive) {
		int f = -1;
		const strref endm("endm");
		for (;;) {
			f = macro.find(endm, f+1);
			if (f<0)
				return ERROR_BAD_MACRO_FORMAT;
			if (f == 0 || strref::is_ws(macro[f - 1]))
				break;
		}
		pMacro->macro = macro.get_substr(0, f);
		macro += f;
		macro.line();
		left = macro;
	} else {
		int pos_bracket = macro.find('{');
		if (pos_bracket < 0) {
			pMacro->macro = strref();
			return ERROR_BAD_MACRO_FORMAT;
		}
		strref source = macro + pos_bracket;
		strref macro_body = source.scoped_block_skip();
		pMacro->macro = strref(macro.get(), pos_bracket + macro_body.get_len() + 2);
		source.skip_whitespace();
		left = source;
	}
	pMacro->source_name = source_name;
	pMacro->source_file = source_file;
	pMacro->params_first_line = params_first_line;
	return STATUS_OK;
}

// Compile in a macro
StatusCode Asm::BuildMacro(Macro &m, strref arg_list)
{
	strref macro_src = m.macro, params;
	if (m.params_first_line) {
		if (end_macro_directive)
			params = macro_src.line();
		else {
			params = macro_src.before('{');
			macro_src += params.get_len();
		}
	}
	else
		params = (macro_src[0] == '(' ? macro_src.scoped_block_skip() : strref());
	params.trim_whitespace();
	arg_list.trim_whitespace();
	if (syntax == SYNTAX_MERLIN) {
		// need to include comment field because separator is ;
		if (contextStack.curr().read_source.is_substr(arg_list.get()))
			arg_list = (contextStack.curr().read_source +
						strl_t(arg_list.get()-contextStack.curr().read_source.get())
						).line();
		arg_list = arg_list.before_or_full(c_comment).get_trimmed_ws();
		strref arg = arg_list;
		strown<16> tag;
		int t_max = 16;
		int dSize = 0;
		for (int t=1; t<t_max; t++) {
			tag.sprintf("]%d", t);
			strref a = arg.split_token_trim(';');
			if (!a) {
				t_max = t;
				break;
			}
			int count = macro_src.substr_case_count(tag.get_strref());
			dSize += count * ((int)a.get_len() - (int)tag.get_len());
		}
		int mac_size = macro_src.get_len() + dSize + 32;
		if (char *buffer = (char*)malloc(mac_size)) {
			loadedData.push_back(buffer);
			strovl macexp(buffer, mac_size);
			macexp.copy(macro_src);
			arg = arg_list;
			for (int t=1; t<t_max; t++) {
				tag.sprintf("]%d", t);
				strref a = arg.split_token_trim(';');
				macexp.replace_bookend(tag.get_strref(), a, label_end_char_range_merlin);
			}
			contextStack.push(m.source_name, macexp.get_strref(), macexp.get_strref());
			if (scope_depth>=(MAX_SCOPE_DEPTH-1))
				return ERROR_TOO_DEEP_SCOPE;
			else
				scope_address[++scope_depth] = CurrSection().GetPC();
			contextStack.curr().scoped_context = true;
			return STATUS_OK;
		} else
			return ERROR_OUT_OF_MEMORY_FOR_MACRO_EXPANSION;
	} else if (params) {
		if (arg_list[0]=='(')
			arg_list = arg_list.scoped_block_skip();
		strref pchk = params;
		strref arg = arg_list;
		int dSize = 0;
		char token = arg_list.find(',')>=0 ? ',' : ' ';
		char token_macro = m.params_first_line && params.find(',') < 0 ? ' ' : ',';
		while (strref param = pchk.split_token_trim(token_macro)) {
			strref a = arg.split_token_trim(token);
			if (param.get_len() < a.get_len()) {
				int count = macro_src.substr_case_count(param);
				dSize += count * ((int)a.get_len() - (int)param.get_len());
			}
		}
		int mac_size = macro_src.get_len() + dSize + 32;
		if (char *buffer = (char*)malloc(mac_size)) {
			loadedData.push_back(buffer);
			strovl macexp(buffer, mac_size);
			macexp.copy(macro_src);
			while (strref param = params.split_token_trim(token_macro)) {
				strref a = arg_list.split_token_trim(token);
				macexp.replace_bookend(param, a, label_end_char_range);
			}
			contextStack.push(m.source_name, macexp.get_strref(), macexp.get_strref());
			if (end_macro_directive) {
				contextStack.push(m.source_name, macexp.get_strref(), macexp.get_strref());
				if (scope_depth>=(MAX_SCOPE_DEPTH-1))
					return ERROR_TOO_DEEP_SCOPE;
				else
					scope_address[++scope_depth] = CurrSection().GetPC();
				contextStack.curr().scoped_context = true;
			}
			return STATUS_OK;
		} else
			return ERROR_OUT_OF_MEMORY_FOR_MACRO_EXPANSION;
	}
	contextStack.push(m.source_name, m.source_file, macro_src);
	return STATUS_OK;
}



//
//
// STRUCTS AND ENUMS
//
//



// Enums are Structs in disguise
StatusCode Asm::BuildEnum(strref name, strref declaration)
{
	unsigned int hash = name.fnv1a();
	unsigned int ins = FindLabelIndex(hash, labelStructs.getKeys(), labelStructs.count());
	LabelStruct *pEnum = nullptr;
	while (ins < labelStructs.count() && labelStructs.getKey(ins)==hash) {
		if (name.same_str_case(labelStructs.getValue(ins).name)) {
			pEnum = labelStructs.getValues() + ins;
			break;
		}
		++ins;
	}
	if (pEnum)
		return ERROR_STRUCT_ALREADY_DEFINED;
	labelStructs.insert(ins, hash);
	pEnum = labelStructs.getValues() + ins;
	pEnum->name = name;
	pEnum->first_member = (unsigned short)structMembers.size();
	pEnum->numMembers = 0;
	pEnum->size = 0;		// enums are 0 sized
	int value = 0;

	struct EvalContext etx;
	SetEvalCtxDefaults(etx);

	while (strref line = declaration.line()) {
		line = line.before_or_full(',');
		line.trim_whitespace();
		strref name = line.split_token_trim('=');
		line = line.before_or_full(';').before_or_full(c_comment).get_trimmed_ws();
		if (line) {
			StatusCode error = EvalExpression(line, etx, value);
			if (error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
				return ERROR_ENUM_CANT_BE_ASSEMBLED;
			else if (error != STATUS_OK)
				return error;
		}
		struct MemberOffset member;
		member.offset = value;
		member.name = name;
		member.name_hash = member.name.fnv1a();
		member.sub_struct = strref();
		structMembers.push_back(member);
		++value;
		pEnum->numMembers++;
	}
	return STATUS_OK;
}

StatusCode Asm::BuildStruct(strref name, strref declaration)
{
	unsigned int hash = name.fnv1a();
	unsigned int ins = FindLabelIndex(hash, labelStructs.getKeys(), labelStructs.count());
	LabelStruct *pStruct = nullptr;
	while (ins < labelStructs.count() && labelStructs.getKey(ins)==hash) {
		if (name.same_str_case(labelStructs.getValue(ins).name)) {
			pStruct = labelStructs.getValues() + ins;
			break;
		}
		++ins;
	}
	if (pStruct)
		return ERROR_STRUCT_ALREADY_DEFINED;
	labelStructs.insert(ins, hash);
	pStruct = labelStructs.getValues() + ins;
	pStruct->name = name;
	pStruct->first_member = (unsigned short)structMembers.size();

	unsigned int byte_hash = struct_byte.fnv1a();
	unsigned int word_hash = struct_word.fnv1a();
	unsigned short size = 0;
	unsigned short member_count = 0;

	while (strref line = declaration.line()) {
		line.trim_whitespace();
		strref type = line.split_label();
		line.skip_whitespace();
		unsigned int type_hash = type.fnv1a();
		unsigned short type_size = 0;
		LabelStruct *pSubStruct = nullptr;
		if (type_hash==byte_hash && struct_byte.same_str_case(type))
			type_size = 1;
		else if (type_hash==word_hash && struct_word.same_str_case(type))
			type_size = 2;
		else {
			unsigned int index = FindLabelIndex(type_hash, labelStructs.getKeys(), labelStructs.count());
			while (index < labelStructs.count() && labelStructs.getKey(index)==type_hash) {
				if (type.same_str_case(labelStructs.getValue(index).name)) {
					pSubStruct = labelStructs.getValues() + index;
					break;
				}
				++index;
			}
			if (!pSubStruct) {
				labelStructs.remove(ins);
				return ERROR_REFERENCED_STRUCT_NOT_FOUND;
			}
			type_size = pSubStruct->size;
		}

		// add the new member, don't grow vectors one at a time.
		if (structMembers.size() == structMembers.capacity())
			structMembers.reserve(structMembers.size() + 64);
		struct MemberOffset member;
		member.offset = size;
		member.name = line.get_label();
		member.name_hash = member.name.fnv1a();
		member.sub_struct = pSubStruct ? pSubStruct->name : strref();
		structMembers.push_back(member);

		size += type_size;
		member_count++;
	}

	pStruct->numMembers = member_count;
	pStruct->size = size;

	return STATUS_OK;
}

// Evaluate a struct offset as if it was a label
StatusCode Asm::EvalStruct(strref name, int &value)
{
	LabelStruct *pStruct = nullptr;
	unsigned short offset = 0;
	while (strref struct_seg = name.split_token('.')) {
		strref sub_struct = struct_seg;
		unsigned int seg_hash = struct_seg.fnv1a();
		if (pStruct) {
			struct MemberOffset *member = &structMembers[pStruct->first_member];
			bool found = false;
			for (int i = 0; i<pStruct->numMembers; i++) {
				if (member->name_hash == seg_hash && member->name.same_str_case(struct_seg)) {
					offset += member->offset;
					sub_struct = member->sub_struct;
					found = true;
					break;
				}
				++member;
			}
			if (!found)
				return ERROR_REFERENCED_STRUCT_NOT_FOUND;
		}
		if (sub_struct) {
			unsigned int hash = sub_struct.fnv1a();
			unsigned int index = FindLabelIndex(hash, labelStructs.getKeys(), labelStructs.count());
			while (index < labelStructs.count() && labelStructs.getKey(index)==hash) {
				if (sub_struct.same_str_case(labelStructs.getValue(index).name)) {
					pStruct = labelStructs.getValues() + index;
					break;
				}
				++index;
			}
		} else if (name)
			return STATUS_NOT_STRUCT;
	}
	if (pStruct == nullptr)
		return STATUS_NOT_STRUCT;
	value = offset;
	return STATUS_OK;
}


//
//
// EXPRESSIONS AND LATE EVALUATION
//
//



int Asm::ReptCnt() const
{
	return contextStack.curr().repeat_total - contextStack.curr().repeat;
}

void Asm::SetEvalCtxDefaults(struct EvalContext &etx)
{
	etx.pc = CurrSection().GetPC();				// current address at point of eval
	etx.scope_pc = scope_address[scope_depth];	// current scope open at point of eval
	etx.scope_end_pc = -1;						// late scope closure after eval
	etx.scope_depth = scope_depth;				// scope depth for eval (must match current for scope_end_pc to eval)
	etx.relative_section = -1;					// return can be relative to this section
	etx.file_ref = -1;							// can access private label from this file or -1
	etx.rept_cnt = ReptCnt();					// current repeat counter
}

// Get a single token from a merlin expression
EvalOperator Asm::RPNToken_Merlin(strref &expression, const struct EvalContext &etx, EvalOperator prev_op, short &section, int &value)
{
	char c = expression.get_first();
	switch (c) {
		case '$': ++expression; value = expression.ahextoui_skip(); return EVOP_VAL;
		case '-': ++expression; return EVOP_SUB;
		case '+': ++expression;	return EVOP_ADD;
		case '*': // asterisk means both multiply and current PC, disambiguate!
			++expression;
			if (expression[0] == '*') return EVOP_STP; // double asterisks indicates comment
			else if (prev_op==EVOP_VAL || prev_op==EVOP_RPR) return EVOP_MUL;
			value = etx.pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
		case '/': ++expression; return EVOP_DIV;
		case '>': if (expression.get_len() >= 2 && expression[1] == '>') { expression += 2; return EVOP_SHR; }
				  ++expression; return EVOP_HIB;
		case '<': if (expression.get_len() >= 2 && expression[1] == '<') { expression += 2; return EVOP_SHL; }
				  ++expression; return EVOP_LOB;
		case '%': // % means both binary and scope closure, disambiguate!
			if (expression[1]=='0' || expression[1]=='1') {
				++expression; value = expression.abinarytoui_skip(); return EVOP_VAL; }
			if (etx.scope_end_pc<0 || scope_depth != etx.scope_depth) return EVOP_NRY;
			++expression; value = etx.scope_end_pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
		case '|':
		case '.': ++expression; return EVOP_OR;	// MERLIN: . is or, | is not used
		case '^': if (prev_op == EVOP_VAL || prev_op == EVOP_RPR) { ++expression; return EVOP_EOR; }
				  ++expression;  return EVOP_BAB;
		case '&': ++expression; return EVOP_AND;
		case '(': if (prev_op!=EVOP_VAL) { ++expression; return EVOP_LPR; } return EVOP_STP;
		case ')': ++expression; return EVOP_RPR;
		case '"': if (expression[2] == '"') { value = expression[1];  expression += 3; return EVOP_VAL; } return EVOP_STP;
		case '\'': if (expression[2] == '\'') { value = expression[1];  expression += 3; return EVOP_VAL; } return EVOP_STP;
		case ',':
		case '?':
		default: {	// MERLIN: ! is eor
			if (c == '!' && (prev_op == EVOP_VAL || prev_op == EVOP_RPR)) { ++expression; return EVOP_EOR; }
			else if (c == '!' && !(expression + 1).len_label()) {
				if (etx.scope_pc < 0) return EVOP_NRY;	// ! by itself is current scope, !+label char is a local label
				++expression; value = etx.scope_pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
			} else if (strref::is_number(c)) {
				if (prev_op == EVOP_VAL) return EVOP_STP;	// value followed by value doesn't make sense, stop
				value = expression.atoi_skip(); return EVOP_VAL;
			} else if (c == '!' || c == ']' || c==':' || strref::is_valid_label(c)) {
				if (prev_op == EVOP_VAL) return EVOP_STP; // a value followed by a value does not make sense, probably start of a comment (ORCA/LISA?)
				char e0 = expression[0];
				int start_pos = (e0==']' || e0==':' || e0=='!' || e0=='.') ? 1 : 0;
				strref label = expression.split_range_trim(label_end_char_range_merlin, start_pos);
				Label *pLabel = pLabel = GetLabel(label, etx.file_ref);
				if (!pLabel) {
					StatusCode ret = EvalStruct(label, value);
					if (ret == STATUS_OK) return EVOP_VAL;
					if (ret != STATUS_NOT_STRUCT) return EVOP_ERR;	// partial struct
				}
				if (!pLabel && label.same_str("rept")) { value = etx.rept_cnt; return EVOP_VAL; }
				if (!pLabel || !pLabel->evaluated) return EVOP_NRY;	// this label could not be found (yet)
				value = pLabel->value; section = pLabel->section; return EVOP_VAL;
			} else
				return EVOP_ERR;
			break;
		}
	}
	return EVOP_NONE; // shouldn't get here normally
}

// Get a single token from most non-apple II assemblers
EvalOperator Asm::RPNToken(strref &exp, const struct EvalContext &etx, EvalOperator prev_op, short &section, int &value)
{
	char c = exp.get_first();
	switch (c) {
		case '$': ++exp; value = exp.ahextoui_skip(); return EVOP_VAL;
		case '-': ++exp; return EVOP_SUB;
		case '+': ++exp;	return EVOP_ADD;
		case '*': // asterisk means both multiply and current PC, disambiguate!
			++exp;
			if (exp[0] == '*') return EVOP_STP; // double asterisks indicates comment
			else if (prev_op == EVOP_VAL || prev_op == EVOP_RPR) return EVOP_MUL;
			value = etx.pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
		case '/': ++exp; return EVOP_DIV;
		case '=': if (exp[1] == '=') { exp += 2; return EVOP_EQU; } return EVOP_STP;
		case '>': if (exp.get_len() >= 2 && exp[1] == '>') { exp += 2; return EVOP_SHR; }
				  if (prev_op == EVOP_VAL || prev_op == EVOP_RPR) { ++exp;
					if (exp[0] == '=') { ++exp; return EVOP_GTE; } return EVOP_GT; }
				  ++exp; return EVOP_HIB;
		case '<': if (exp.get_len() >= 2 && exp[1] == '<') { exp += 2; return EVOP_SHL; }
				  if (prev_op == EVOP_VAL || prev_op == EVOP_RPR) { ++exp;
					if (exp[0] == '=') { ++exp; return EVOP_LTE; } return EVOP_LT; }
				  ++exp; return EVOP_LOB;
		case '%': // % means both binary and scope closure, disambiguate!
			if (exp[1] == '0' || exp[1] == '1') { ++exp; value = exp.abinarytoui_skip(); return EVOP_VAL; }
			if (etx.scope_end_pc<0 || scope_depth != etx.scope_depth) return EVOP_NRY;
			++exp; value = etx.scope_end_pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
		case '|': ++exp; return EVOP_OR;
		case '^': if (prev_op == EVOP_VAL || prev_op == EVOP_RPR) { ++exp; return EVOP_EOR; }
				  ++exp;  return EVOP_BAB;
		case '&': ++exp; return EVOP_AND;
		case '(': if (prev_op != EVOP_VAL) { ++exp; return EVOP_LPR; } return EVOP_STP;
		case ')': ++exp; return EVOP_RPR;
		case ',':
		case '?':
		case '\'': return EVOP_STP;
		default: {	// ! by itself is current scope, !+label char is a local label
			if (c == '!' && !(exp + 1).len_label()) {
				if (etx.scope_pc < 0) return EVOP_NRY;
				++exp; value = etx.scope_pc; section = CurrSection().IsRelativeSection() ? SectionId() : -1; return EVOP_VAL;
			} else if (strref::is_number(c)) {
				if (prev_op == EVOP_VAL) return EVOP_STP;	// value followed by value doesn't make sense, stop
				value = exp.atoi_skip(); return EVOP_VAL;
			} else if (c == '!' || c == ':' || c=='.' || c=='@' || strref::is_valid_label(c)) {
				if (prev_op == EVOP_VAL) return EVOP_STP; // a value followed by a value does not make sense, probably start of a comment (ORCA/LISA?)
				char e0 = exp[0];
				int start_pos = (e0 == ':' || e0 == '!' || e0 == '.') ? 1 : 0;
				strref label = exp.split_range_trim(label_end_char_range, start_pos);
				Label *pLabel = pLabel = GetLabel(label, etx.file_ref);
				if (!pLabel) {
					StatusCode ret = EvalStruct(label, value);
					if (ret == STATUS_OK) return EVOP_VAL;
					if (ret != STATUS_NOT_STRUCT) return EVOP_ERR;	// partial struct
				}
				if (!pLabel && label.same_str("rept")) { value = etx.rept_cnt; return EVOP_VAL; }
				if (!pLabel || !pLabel->evaluated) return EVOP_NRY;	// this label could not be found (yet)
				value = pLabel->value; section = pLabel->section; return pLabel->reference ? EVOP_XRF : EVOP_VAL;
			}
			return EVOP_ERR;
		}
	}
	return EVOP_NONE; // shouldn't get here normally
}

//
// EvalExpression
//	Uses the Shunting Yard algorithm to convert to RPN first
//	which makes the actual calculation trivial and avoids recursion.
//	https://en.wikipedia.org/wiki/Shunting-yard_algorithm
//
// Return:
//	STATUS_OK means value is completely evaluated
//	STATUS_NOT_READY means value could not be evaluated right now
//	ERROR_* means there is an error in the expression
//

// Max number of unresolved sections to evaluate in a single expression
#define MAX_EVAL_SECTIONS 4

// determine if a scalar can be a shift
static int mul_as_shift(int scalar)
{
	int shift = 0;
	while (scalar > 1 && (scalar & 1) == 0) {
		shift++;
		scalar >>= 1;
	}
	return scalar == 1 ? shift : 0;
}

StatusCode Asm::EvalExpression(strref expression, const struct EvalContext &etx, int &result)
{
	int numValues = 0;
	int numOps = 0;

	char ops[MAX_EVAL_OPER];		// RPN expression
	int values[MAX_EVAL_VALUES];	// RPN values (in order of RPN EVOP_VAL operations)
	short section_ids[MAX_EVAL_SECTIONS];	// local index of each referenced section
	short section_val[MAX_EVAL_VALUES] = { 0 };		// each value can be assigned to one section, or -1 if fixed
	short num_sections = 0;			// number of sections in section_ids (normally 0 or 1, can be up to MAX_EVAL_SECTIONS)
	bool xrefd = false;
	values[0] = 0;					// Initialize RPN if no expression
	{
		int sp = 0;
		char op_stack[MAX_EVAL_OPER];
		EvalOperator prev_op = EVOP_NONE;
		expression.trim_whitespace();
		while (expression) {
			int value = 0;
			short section = -1, index_section = -1;
			EvalOperator op = EVOP_NONE;
			if (syntax == SYNTAX_MERLIN)
				op = RPNToken_Merlin(expression, etx, prev_op, section, value);
			else
				op = RPNToken(expression, etx, prev_op, section, value);
			if (op == EVOP_ERR)
				return ERROR_UNEXPECTED_CHARACTER_IN_EXPRESSION;
			else if (op == EVOP_NRY)
				return STATUS_NOT_READY;
			else if (op == EVOP_XRF) {
				xrefd = true;
				op = EVOP_VAL;
			}
			if (section >= 0) {
				for (int s = 0; s<num_sections && index_section<0; s++) {
					if (section_ids[s] == section) index_section = s;
				}
				if (index_section<0) {
					if (num_sections <= MAX_EVAL_SECTIONS)
						section_ids[index_section = num_sections++] = section;
					else
						return STATUS_NOT_READY;
				}
			}

			// this is the body of the shunting yard algorithm
			if (op == EVOP_VAL) {
				section_val[numValues] = index_section;	// only value operators can be section specific
				values[numValues++] = value;
				ops[numOps++] = op;
			} else if (op == EVOP_LPR) {
				op_stack[sp++] = op;
			} else if (op == EVOP_RPR) {
				while (sp && op_stack[sp-1]!=EVOP_LPR) {
					sp--;
					ops[numOps++] = op_stack[sp];
				}
				// check that there actually was a left parenthesis
				if (!sp || op_stack[sp-1]!=EVOP_LPR)
					return ERROR_UNBALANCED_RIGHT_PARENTHESIS;
				sp--; // skip open paren
			} else if (op == EVOP_STP) {
				break;
			} else {
				while (sp) {
					EvalOperator p = (EvalOperator)op_stack[sp-1];
					if (p==EVOP_LPR || op>p)
						break;
					ops[numOps++] = p;
					sp--;
				}
				op_stack[sp++] = op;
			}
			// check for out of bounds or unexpected input
			if (numValues==MAX_EVAL_VALUES)
				return ERROR_TOO_MANY_VALUES_IN_EXPRESSION;
			else if (numOps==MAX_EVAL_OPER || sp==MAX_EVAL_OPER)
				return ERROR_TOO_MANY_OPERATORS_IN_EXPRESSION;

			prev_op = op;
			expression.skip_whitespace();
		}
		while (sp) {
			sp--;
			ops[numOps++] = op_stack[sp];
		}
	}

	// Check if dependent on XREF'd symbol
	if (xrefd)
		return STATUS_XREF_DEPENDENT;

	// processing the result RPN will put the completed expression into values[0].
	// values is used as both the queue and the stack of values since reads/writes won't
	// exceed itself.
	{
		int valIdx = 0;
		int ri = 0;		// RPN index (value)
		int prev_val = values[0];
		int shift_bits = 0; // special case for relative reference to low byte / high byte
		short section_counts[MAX_EVAL_SECTIONS][MAX_EVAL_VALUES] = { 0 };
		for (int o = 0; o<numOps; o++) {
			EvalOperator op = (EvalOperator)ops[o];
			shift_bits = 0;
			prev_val = ri ? values[ri-1] : prev_val;
			if (op!=EVOP_VAL && op!=EVOP_LOB && op!=EVOP_HIB && op!=EVOP_BAB && op!=EVOP_SUB && ri<2)
				break; // ignore suffix operations that are lacking values
			switch (op) {
				case EVOP_VAL:	// value
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri] = i==section_val[ri] ? 1 : 0;
					values[ri++] = values[valIdx++]; break;
				case EVOP_EQU:	// ==
					ri--;
					values[ri - 1] = values[ri - 1] == values[ri];
					break;
				case EVOP_GT:	// >
					ri--;
					values[ri - 1] = values[ri - 1] > values[ri];
					break;
				case EVOP_LT:	// <
					ri--;
					values[ri - 1] = values[ri - 1] < values[ri];
					break;
				case EVOP_GTE:	// >=
					ri--;
					values[ri - 1] = values[ri - 1] >= values[ri];
					break;
				case EVOP_LTE:	// >=
					ri--;
					values[ri - 1] = values[ri - 1] <= values[ri];
					break;
				case EVOP_ADD:	// +
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] += section_counts[i][ri];
					values[ri-1] += values[ri]; break;
				case EVOP_SUB:	// -
					if (ri==1)
						values[ri-1] = -values[ri-1];
					else if (ri>1) {
						ri--;
						for (int i = 0; i<num_sections; i++)
							section_counts[i][ri-1] -= section_counts[i][ri];
						values[ri-1] -= values[ri];
					} break;
				case EVOP_MUL:	// *
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					shift_bits = mul_as_shift(values[ri]);
					prev_val = values[ri - 1];
					values[ri-1] *= values[ri]; break;
				case EVOP_DIV:	// /
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					shift_bits = -mul_as_shift(values[ri]);
					prev_val = values[ri - 1];
					values[ri - 1] /= values[ri]; break;
				case EVOP_AND:	// &
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					values[ri-1] &= values[ri]; break;
				case EVOP_OR:	// |
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					values[ri-1] |= values[ri]; break;
				case EVOP_EOR:	// ^
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					values[ri-1] ^= values[ri]; break;
				case EVOP_SHL:	// <<
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					shift_bits = values[ri];
					prev_val = values[ri - 1];
					values[ri - 1] <<= values[ri]; break;
				case EVOP_SHR:	// >>
					ri--;
					for (int i = 0; i<num_sections; i++)
						section_counts[i][ri-1] |= section_counts[i][ri];
					shift_bits = -values[ri];
					prev_val = values[ri - 1];
					values[ri - 1] >>= values[ri]; break;
				case EVOP_LOB:	// low byte
					if (ri)
						values[ri-1] &= 0xff;
					break;
				case EVOP_HIB:
					if (ri) {
						shift_bits = -8;
						values[ri - 1] = values[ri - 1] >> 8;
					} break;
				case EVOP_BAB:
					if (ri) {
						shift_bits = -16;
						values[ri - 1] = (values[ri - 1] >> 16);
					}
					break;
				default:
					return ERROR_EXPRESSION_OPERATION;
					break;
			}
			if (shift_bits==0 && ri)
				prev_val = values[ri-1];
		}
		int section_index = -1;
		bool curr_relative = false;
		// If relative to any section unless specifically interested in a relative value then return not ready
		for (int i = 0; i<num_sections; i++) {
			if (section_counts[i][0]) {
				if (section_counts[i][0]!=1 || section_index>=0)
					return STATUS_NOT_READY;
				else if (etx.relative_section==section_ids[i])
					curr_relative = true;
				else if (etx.relative_section>=0)
					return STATUS_NOT_READY;
				section_index = i;
			}
		}
		result = values[0];
		if (section_index>=0 && !curr_relative) {
			lastEvalSection = section_ids[section_index];
			lastEvalValue = prev_val;
			lastEvalShift = shift_bits;
			return STATUS_RELATIVE_SECTION;
		}
	}

	return STATUS_OK;
}


// if an expression could not be evaluated, add it along with
// the action to perform if it can be evaluated later.
void Asm::AddLateEval(int target, int pc, int scope_pc, strref expression, strref source_file, LateEval::Type type)
{
	LateEval le;
	le.address = pc;
	le.scope = scope_pc;
	le.scope_depth = scope_depth;
	le.target = target;
	le.section = (int)(&CurrSection() - &allSections[0]);
	le.rept = contextStack.curr().repeat_total - contextStack.curr().repeat;
	le.file_ref = -1; // current or xdef'd
	le.label.clear();
	le.expression = expression;
	le.source_file = source_file;
	le.type = type;

	lateEval.push_back(le);
}

void Asm::AddLateEval(strref label, int pc, int scope_pc, strref expression, LateEval::Type type)
{
	LateEval le;
	le.address = pc;
	le.scope = scope_pc;
	le.scope_depth = scope_depth;
	le.target = 0;
	le.label = label;
	le.section = (int)(&CurrSection() - &allSections[0]);
	le.rept = contextStack.curr().repeat_total - contextStack.curr().repeat;
	le.file_ref = -1; // current or xdef'd
	le.expression = expression;
	le.source_file.clear();
	le.type = type;

	lateEval.push_back(le);
}

// When a label is defined or a scope ends check if there are
// any related late label evaluators that can now be evaluated.
StatusCode Asm::CheckLateEval(strref added_label, int scope_end, bool print_missing_reference_errors)
{
	bool evaluated_label = true;
	strref new_labels[MAX_LABELS_EVAL_ALL];
	int num_new_labels = 0;
	if (added_label)
		new_labels[num_new_labels++] = added_label;

	bool all = !added_label;
	std::vector<LateEval>::iterator i = lateEval.begin();
	while (evaluated_label) {
		evaluated_label = false;
		while (i != lateEval.end()) {
			int value = 0;
			// check if this expression is related to the late change (new label or end of scope)
			bool check = all || num_new_labels==MAX_LABELS_EVAL_ALL;
			for (int l = 0; l<num_new_labels && !check; l++)
				check = i->expression.find(new_labels[l]) >= 0;
			if (!check && scope_end>0) {
				int gt_pos = 0;
				while (gt_pos>=0 && !check) {
					gt_pos = i->expression.find_at('%', gt_pos);
					if (gt_pos>=0) {
						if (i->expression[gt_pos+1]=='%')
							gt_pos++;
						else
							check = true;
						gt_pos++;
					}
				}
			}
			if (check) {
				struct EvalContext etx(i->address, i->scope, scope_end,
						i->type == LateEval::LET_BRANCH ? SectionId() : -1, i->rept);
				etx.scope_depth = i->scope_depth;
				etx.file_ref = i->file_ref;
				StatusCode ret = EvalExpression(i->expression, etx, value);
				if (ret == STATUS_OK || ret==STATUS_RELATIVE_SECTION) {
					// Check if target section merged with another section
					int trg = i->target;
					int sec = i->section;
					if (i->type != LateEval::LET_LABEL) {
						if (allSections[sec].IsMergedSection()) {
							trg += allSections[sec].merged_offset;
							sec = allSections[sec].merged_section;
						}
					}
					bool resolved = true;
					switch (i->type) {
						case LateEval::LET_BYTE:
							if (ret==STATUS_RELATIVE_SECTION) {
								if (i->section<0)
									resolved = false;
								else {
									allSections[sec].AddReloc(lastEvalValue, trg, lastEvalSection, 1, lastEvalShift);
									value = 0;
								}
							}
							if (trg >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetByte(trg, value);
							break;

						case LateEval::LET_ABS_REF:
							if (ret==STATUS_RELATIVE_SECTION) {
								if (i->section<0)
									resolved = false;
								else {
									allSections[sec].AddReloc(lastEvalValue, trg, lastEvalSection, 2, lastEvalShift);
									value = 0;
								}
							}
							if ((trg+1) >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetWord(trg, value);
							break;

						case LateEval::LET_ABS_L_REF:
							if (ret==STATUS_RELATIVE_SECTION) {
								if (i->section<0)
									resolved = false;
								else {
									allSections[sec].AddReloc(lastEvalValue, trg, lastEvalSection, 3, lastEvalShift);
									value = 0;
								}
							}
							if ((trg+2) >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetTriple(trg, value);
							break;

						case LateEval::LET_ABS_4_REF:
							if (ret==STATUS_RELATIVE_SECTION) {
								if (i->section<0)
									resolved = false;
								else {
									allSections[sec].AddReloc(lastEvalValue, trg, lastEvalSection, 4, lastEvalShift);
									value = 0;
								}
							}
							if ((trg+3) >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetQuad(trg, value);
							break;

						case LateEval::LET_BRANCH:
							value -= i->address+1;
							if (value<-128 || value>127) {
								i = lateEval.erase(i);
								return ERROR_BRANCH_OUT_OF_RANGE;
							} if (trg >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetByte(trg, value);
							break;

						case LateEval::LET_BRANCH_16:
							value -= i->address+2;
							if (trg >= allSections[sec].size())
								return ERROR_SECTION_TARGET_OFFSET_OUT_OF_RANGE;
							allSections[sec].SetWord(trg, value);
							break;

						case LateEval::LET_LABEL: {
							Label *label = GetLabel(i->label, i->file_ref);
							if (!label)
								return ERROR_LABEL_MISPLACED_INTERNAL;
							label->value = value;
							label->evaluated = true;
							label->section = ret==STATUS_RELATIVE_SECTION ? i->section : -1;
							if (num_new_labels<MAX_LABELS_EVAL_ALL)
								new_labels[num_new_labels++] = label->label_name;
							evaluated_label = true;
							char f = i->label[0], l = i->label.get_last();
							LabelAdded(label, f=='.' || f=='!' || f=='@' || f==':' || l=='$');
							break;
						}
						default:
							break;
					}
					if (resolved)
						i = lateEval.erase(i);
				} else {
					if (print_missing_reference_errors && ret!=STATUS_XREF_DEPENDENT) {
						PrintError(i->expression, ret);
						error_encountered = true;
					}
					++i;
				}
			} else
				++i;
		}
		all = false;
		added_label.clear();
	}
	return STATUS_OK;
}



//
//
// LABELS
//
//



// Get a label record if it exists
Label *Asm::GetLabel(strref label)
{
	unsigned int label_hash = label.fnv1a();
	unsigned int index = FindLabelIndex(label_hash, labels.getKeys(), labels.count());
	while (index < labels.count() && label_hash == labels.getKey(index)) {
		if (label.same_str(labels.getValue(index).label_name))
			return labels.getValues() + index;
		index++;
	}
	return nullptr;
}

// Get a protected label record from a file if it exists
Label *Asm::GetLabel(strref label, int file_ref)
{
	if (file_ref>=0 && file_ref<(int)externals.size()) {
		ExtLabels &labs = externals[file_ref];
		unsigned int label_hash = label.fnv1a();
		unsigned int index = FindLabelIndex(label_hash, labs.labels.getKeys(), labs.labels.count());
		while (index < labs.labels.count() && label_hash == labs.labels.getKey(index)) {
			if (label.same_str(labs.labels.getValue(index).label_name))
				return labs.labels.getValues() + index;
			index++;
		}
	}
	return GetLabel(label);
}

// If exporting labels, append this label to the list
void Asm::LabelAdded(Label *pLabel, bool local)
{
	if (pLabel && pLabel->evaluated) {
		if (map.size() == map.capacity())
			map.reserve(map.size() + 256);
		MapSymbol sym;
		sym.name = pLabel->label_name;
		sym.section = pLabel->section;
		sym.value = pLabel->value;
		sym.local = local;
		pLabel->mapIndex = pLabel->evaluated ? -1 : (int)map.size();
		map.push_back(sym);
	}
}

// Add a label entry
Label* Asm::AddLabel(unsigned int hash) {
	unsigned int index = FindLabelIndex(hash, labels.getKeys(), labels.count());
	labels.insert(index, hash);
	return labels.getValues() + index;
}

// mark a label as a local label
void Asm::MarkLabelLocal(strref label, bool scope_reserve)
{
	LocalLabelRecord rec;
	rec.label = label;
	rec.scope_depth = scope_depth;
	rec.scope_reserve = scope_reserve;
	localLabels.push_back(rec);
}

// find all local labels or up to given scope level and remove them
StatusCode Asm::FlushLocalLabels(int scope_exit)
{
	StatusCode status = STATUS_OK;
	// iterate from end of local label records and early out if the label scope is lower than the current.
	std::vector<LocalLabelRecord>::iterator i = localLabels.end();
	while (i!=localLabels.begin()) {
		--i;
		if (i->scope_depth < scope_depth)
			break;
		strref label = i->label;
		StatusCode this_status = CheckLateEval(label);
		if (this_status>FIRST_ERROR)
			status = this_status;
		if (!i->scope_reserve || i->scope_depth<=scope_exit) {
			unsigned int index = FindLabelIndex(label.fnv1a(), labels.getKeys(), labels.count());
			while (index<labels.count()) {
				if (label.same_str_case(labels.getValue(index).label_name)) {
					if (i->scope_reserve) {
						if (LabelPool *pool = GetLabelPool(labels.getValue(index).pool_name)) {
							pool->Release(labels.getValue(index).value);
							break;
						}
					}
					labels.remove(index);
					break;
				}
				++index;
			}
			i = localLabels.erase(i);
		}
	}
	return status;
}

// Get a label pool by name
LabelPool* Asm::GetLabelPool(strref pool_name)
{
	unsigned int pool_hash = pool_name.fnv1a();
	unsigned int ins = FindLabelIndex(pool_hash, labelPools.getKeys(), labelPools.count());
	while (ins < labelPools.count() && pool_hash == labelPools.getKey(ins)) {
		if (pool_name.same_str(labelPools.getValue(ins).pool_name)) {
			return &labelPools.getValue(ins);
		}
		ins++;
	}
	return nullptr;
}

// When going out of scope, label pools are deleted.
void Asm::FlushLabelPools(int scope_exit)
{
	unsigned int i = 0;
	while (i<labelPools.count()) {
		if (labelPools.getValue(i).scopeDepth >= scope_exit)
			labelPools.remove(i);
		else
			++i;
	}
}

// Add a label pool
StatusCode Asm::AddLabelPool(strref name, strref args)
{
	unsigned int pool_hash = name.fnv1a();
	unsigned int ins = FindLabelIndex(pool_hash, labelPools.getKeys(), labelPools.count());
	unsigned int index = ins;
	while (index < labelPools.count() && pool_hash == labelPools.getKey(index)) {
		if (name.same_str(labelPools.getValue(index).pool_name))
			return ERROR_LABEL_POOL_REDECLARATION;
		index++;
	}

	// check that there is at least one valid address
	int ranges = 0;
	int num32 = 0;
	unsigned short aRng[256];
	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	while (strref arg = args.split_token_trim(',')) {
		strref start = arg[0]=='(' ? arg.scoped_block_skip() : arg.split_token_trim('-');
		int addr0 = 0, addr1 = 0;
		if (STATUS_OK != EvalExpression(start, etx, addr0))
			return ERROR_POOL_RANGE_EXPRESSION_EVAL;
		if (STATUS_OK != EvalExpression(arg, etx, addr1))
			return ERROR_POOL_RANGE_EXPRESSION_EVAL;
		if (addr1<=addr0 || addr0<0)
			return ERROR_POOL_RANGE_EXPRESSION_EVAL;

		aRng[ranges++] = addr0;
		aRng[ranges++] = addr1;
		num32 += (addr1-addr0+15)>>4;

		if (ranges >(MAX_POOL_RANGES*2) ||
			num32 > ((MAX_POOL_BYTES+15)>>4))
			return ERROR_POOL_RANGE_EXPRESSION_EVAL;
	}

	if (!ranges)
		return ERROR_POOL_RANGE_EXPRESSION_EVAL;

	LabelPool pool;
	pool.pool_name = name;
	pool.numRanges = ranges>>1;
	pool.scopeDepth = scope_depth;

	memset(pool.usedMap, 0, sizeof(unsigned int) * num32);
	for (int r = 0; r<ranges; r++)
		pool.ranges[r] = aRng[r];

	labelPools.insert(ins, pool_hash);
	LabelPool &poolValue = labelPools.getValue(ins);

	poolValue = pool;

	return STATUS_OK;
}

StatusCode Asm::AssignPoolLabel(LabelPool &pool, strref label)
{
	strref type = label;
	label = type.split_token('.');
	int bytes = 1;
	switch (strref::tolower(type.get_first())) {
		case 'l': bytes = 4; break;
		case 't': bytes = 3; break;
		case 'd':
		case 'w': bytes = 2; break;
	}
	if (GetLabel(label))
		return ERROR_POOL_LABEL_ALREADY_DEFINED;
	unsigned int addr;
	StatusCode error = pool.Reserve(bytes, addr);
	if (error != STATUS_OK)
		return error;

	Label *pLabel = AddLabel(label.fnv1a());

	pLabel->label_name = label;
	pLabel->pool_name = pool.pool_name;
	pLabel->evaluated = true;
	pLabel->section = -1;	// pool labels are section-less
	pLabel->value = addr;
	pLabel->pc_relative = true;
	pLabel->constant = true;
	pLabel->external = false;
	pLabel->reference = false;

	MarkLabelLocal(label, true);
	return error;
}

// Request a label from a pool
StatusCode LabelPool::Reserve(int numBytes, unsigned int &ret_addr)
{
	unsigned int *map = usedMap;
	unsigned short *pRanges = ranges;
	for (int r = 0; r<numRanges; r++) {
		int sequence = 0;
		unsigned int a0 = *pRanges++, a1 = *pRanges++;
		unsigned int addr = a1-1, *range_map = map;
		while (addr>=a0 && sequence<numBytes) {
			unsigned int chk = *map++, m = 3;
			while (m && addr >= a0) {
				if ((m & chk)==0) {
					sequence++;
					if (sequence == numBytes)
						break;
				} else
					sequence = 0;
				--addr;
				m <<= 2;
			}
		}
		if (sequence == numBytes) {
			unsigned int index = (a1-addr-numBytes);
			unsigned int *addr_map = range_map + (index>>4);
			unsigned int m = numBytes << (index << 1);
			for (int b = 0; b<numBytes; b++) {
				*addr_map |= m;
				unsigned int _m = m << 2;
				if (!_m) { m <<= 30; addr_map++; } else { m = _m; }
			}
			ret_addr = addr;
			return STATUS_OK;
		}
	}
	return ERROR_OUT_OF_LABELS_IN_POOL;
}

// Release a label from a pool (at scope closure)
StatusCode LabelPool::Release(unsigned int addr) {
	unsigned int *map = usedMap;
	unsigned short *pRanges = ranges;
	for (int r = 0; r<numRanges; r++) {
		unsigned short a0 = *pRanges++, a1 = *pRanges++;
		if (addr>=a0 && addr<a1) {
			unsigned int index = (a1-addr-1);
			map += index>>4;
			index &= 0xf;
			unsigned int u = *map, m = 3 << (index << 1);
			unsigned int b = u & m, bytes = b >> (index << 1);
			if (bytes) {
				for (unsigned int f = 0; f<bytes; f++) {
					u &= ~m;
					unsigned int _m = m>>2;
					if (!_m) { m <<= 30; *map-- = u; } else { m = _m; }
				}
				*map = u;
				return STATUS_OK;
			} else
				return ERROR_INTERNAL_LABEL_POOL_ERROR;
		} else
			map += (a1-a0+15)>>4;
	}
	return STATUS_OK;
}

// Check if a label is marked as an xdef
bool Asm::MatchXDEF(strref label)
{
	unsigned int hash = label.fnv1a();
	unsigned int pos = FindLabelIndex(hash, xdefs.getKeys(), xdefs.count());
	while (pos < xdefs.count() && xdefs.getKey(pos) == hash) {
		if (label.same_str_case(xdefs.getValue(pos)))
			return true;
		++pos;
	}
	return false;
}

// assignment of label (<label> = <expression>)
StatusCode Asm::AssignLabel(strref label, strref line, bool make_constant)
{
	line.trim_whitespace();
	int val = 0;
	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	StatusCode status = EvalExpression(line, etx, val);
	if (status != STATUS_NOT_READY && status != STATUS_OK)
		return status;

	Label *pLabel = GetLabel(label);
	if (pLabel) {
		if (pLabel->constant && pLabel->evaluated && val != pLabel->value)
			return (status == STATUS_NOT_READY) ? STATUS_OK : ERROR_MODIFYING_CONST_LABEL;
	} else
		pLabel = AddLabel(label.fnv1a());

	pLabel->label_name = label;
	pLabel->pool_name.clear();
	pLabel->evaluated = status==STATUS_OK;
	pLabel->section = -1;	// assigned labels are section-less
	pLabel->value = val;
	pLabel->mapIndex = -1;
	pLabel->pc_relative = false;
	pLabel->constant = make_constant;
	pLabel->external = MatchXDEF(label);
	pLabel->reference = false;

	bool local = label[0]=='.' || label[0]=='@' || label[0]=='!' || label[0]==':' || label.get_last()=='$';
	if (!pLabel->evaluated)
		AddLateEval(label, CurrSection().GetPC(), scope_address[scope_depth], line, LateEval::LET_LABEL);
	else {
		if (local)
			MarkLabelLocal(label);
		LabelAdded(pLabel, local);
		return CheckLateEval(label);
	}
	return STATUS_OK;
}

// Adding a fixed address label
StatusCode Asm::AddressLabel(strref label)
{
	StatusCode status = STATUS_OK;
	Label *pLabel = GetLabel(label);
	bool constLabel = false;
	if (!pLabel)
		pLabel = AddLabel(label.fnv1a());
	else if (pLabel->constant && pLabel->value != CurrSection().GetPC())
		return ERROR_MODIFYING_CONST_LABEL;
	else
		constLabel = pLabel->constant;

	pLabel->label_name = label;
	pLabel->pool_name.clear();
	pLabel->section = CurrSection().IsRelativeSection() ? SectionId() : -1;	// address labels are based on section
	pLabel->value = CurrSection().GetPC();
	pLabel->evaluated = true;
	pLabel->pc_relative = true;
	pLabel->external = MatchXDEF(label);
	pLabel->reference = false;
	pLabel->constant = constLabel;
	last_label = label;
	bool local = label[0]=='.' || label[0]=='@' || label[0]=='!' || label[0]==':' || label.get_last()=='$';
	LabelAdded(pLabel, local);
	if (local)
		MarkLabelLocal(label);
	status = CheckLateEval(label);
	if (!local && label[0]!=']') { // MERLIN: Variable label does not invalidate local labels
		StatusCode this_status = FlushLocalLabels();
		if (status<FIRST_ERROR && this_status>=FIRST_ERROR)
			status = this_status;
	}
	return status;
}

// include symbols listed from a .sym file or all if no listing
void Asm::IncludeSymbols(strref line)
{
	strref symlist = line.before('"').get_trimmed_ws();
	line = line.between('"', '"');
	size_t size;
	if (char *buffer = LoadText(line, size)) {
		strref symfile(buffer, strl_t(size));
		while (symfile) {
			symfile.skip_whitespace();
			if (symfile[0]=='{')			// don't include local labels
				symfile.scoped_block_skip();
			if (strref symdef = symfile.line()) {
				strref symtype = symdef.split_token(' ');
				strref label = symdef.split_token_trim('=');
				bool constant = symtype.same_str(".const");	// first word is either .label or .const
				if (symlist) {
					strref symchk = symlist;
					while (strref symwant = symchk.split_token_trim(',')) {
						if (symwant.same_str_case(label)) {
							AssignLabel(label, symdef, constant);
							break;
						}
					}
				} else
					AssignLabel(label, symdef, constant);
			}
		}
		loadedData.push_back(buffer);
	}
}



//
//
// CONDITIONAL ASSEMBLY
//
//



// Encountered #if or #ifdef, return true if assembly is enabled
bool Asm::NewConditional() {
	if (conditional_nesting[conditional_depth]) {
		conditional_nesting[conditional_depth]++;
		return false;
	}
	return true;
}

// Encountered #endif, close out the current conditional
void Asm::CloseConditional() {
	if (conditional_depth)
		conditional_depth--;
	else
		conditional_consumed[conditional_depth] = false;
}

// Check if this conditional will nest the assembly (a conditional is already consumed)
void Asm::CheckConditionalDepth() {
	if (conditional_consumed[conditional_depth]) {
		conditional_depth++;
		conditional_source[conditional_depth] = contextStack.curr().read_source.get_line();
		conditional_consumed[conditional_depth] = false;
		conditional_nesting[conditional_depth] = 0;
	}
}

// This conditional block is going to be assembled, mark it as consumed
void Asm::ConsumeConditional()
{
	conditional_source[conditional_depth] = contextStack.curr().read_source.get_line();
	conditional_consumed[conditional_depth] = true;
}

// This conditional block is not going to be assembled so mark that it is nesting
void Asm::SetConditional()
{
	conditional_source[conditional_depth] = contextStack.curr().read_source.get_line();
	conditional_nesting[conditional_depth] = 1;
}

// Returns true if assembly is currently enabled
bool Asm::ConditionalAsm() {
	return conditional_nesting[conditional_depth]==0;
}

// Returns true if this conditional has a block that has already been assembled
bool Asm::ConditionalConsumed() {
	return conditional_consumed[conditional_depth];
}

// Returns true if this conditional can be consumed
bool Asm::ConditionalAvail() {
	return conditional_nesting[conditional_depth]==1 &&
		!conditional_consumed[conditional_depth];
}

// This conditional block is enabled and the prior wasn't
void Asm::EnableConditional(bool enable) {
	if (enable) {
		conditional_nesting[conditional_depth] = 0;
		conditional_consumed[conditional_depth] = true;
	}
}

// Conditional else that does not enable block
void Asm::ConditionalElse() {
	if (conditional_consumed[conditional_depth])
		conditional_nesting[conditional_depth]++;
}

// Conditional statement evaluation (true/false)
StatusCode Asm::EvalStatement(strref line, bool &result)
{
	int equ = line.find('=');
	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	if (equ >= 0) {
		// (EXP) == (EXP)
		strref left = line.get_clipped(equ);
		bool equal = left.get_last()!='!';
		left.trim_whitespace();
		strref right = line + equ + 1;
		if (right.get_first()=='=')
			++right;
		right.trim_whitespace();
		int value_left, value_right;
		if (STATUS_OK != EvalExpression(left, etx, value_left))
			return ERROR_CONDITION_COULD_NOT_BE_RESOLVED;
		if (STATUS_OK != EvalExpression(right, etx, value_right))
			return ERROR_CONDITION_COULD_NOT_BE_RESOLVED;
		result = (value_left==value_right && equal) || (value_left!=value_right && !equal);
	} else {
		bool invert = line.get_first()=='!';
		if (invert)
			++line;
		int value;
		if (STATUS_OK != EvalExpression(line, etx, value))
			return ERROR_CONDITION_COULD_NOT_BE_RESOLVED;
		result = (value!=0 && !invert) || (value==0 && invert);
	}
	return STATUS_OK;
}

// Add a folder for including files
void Asm::AddIncludeFolder(strref path)
{
	if (!path)
		return;
	for (std::vector<strref>::const_iterator i = includePaths.begin(); i!=includePaths.end(); ++i) {
		if (path.same_str(*i))
			return;
	}
	if (includePaths.size()==includePaths.capacity())
		includePaths.reserve(includePaths.size() + 16);
	includePaths.push_back(path);
}

// unique key binary search
int LookupOpCodeIndex(unsigned int hash, OPLookup *lookup, int count)
{
	int first = 0;
	while (count!=first) {
		int index = (first+count)/2;
		unsigned int read = lookup[index].op_hash;
		if (hash==read) {
			return index;
		} else if (hash>read)
			first = index+1;
		else
			count = index;
	}
	return -1;	// index not found
}

// Encountered a REPT or LUP
StatusCode Asm::Directive_Rept(strref line, strref source_file)
{
	SourceContext &ctx = contextStack.curr();
	strref read_source = ctx.read_source;
	if (read_source.is_substr(line.get())) {
		read_source.skip(strl_t(line.get() - read_source.get()));
		strref expression;
		if (syntax == SYNTAX_MERLIN || end_macro_directive) {
			expression = line;			// Merlin repeat body begins next line
			read_source.line();
		} else {
			int block = read_source.find('{');
			if (block<0)
				return ERROR_REPT_MISSING_SCOPE;
			expression = read_source.get_substr(0, block);
			read_source += block;
			read_source.skip_whitespace();
		}
		expression.trim_whitespace();
		int count;
		struct EvalContext etx;
		SetEvalCtxDefaults(etx);
		if (STATUS_OK != EvalExpression(expression, etx, count))
			return ERROR_REPT_COUNT_EXPRESSION;
		strref recur;
		if (syntax == SYNTAX_MERLIN || end_macro_directive) {
			recur = read_source;		// Merlin repeat body ends at "--^"
			while (strref next_line = read_source.line()) {
				next_line = next_line.before_or_full(';');
				next_line = next_line.before_or_full(c_comment);
				int term = next_line.find(end_macro_directive ? "endr" : "--^");
				if (term >= 0) {
					recur = recur.get_substr(0, strl_t(next_line.get() + term - recur.get()));
					break;
				}
			}
		} else
			recur = read_source.scoped_block_skip();
		ctx.next_source = read_source;
		contextStack.push(ctx.source_name, ctx.source_file, recur, count);
	}
	return STATUS_OK;
}

// macro: create an assembler macro
StatusCode Asm::Directive_Macro(strref line, strref source_file)
{
	strref read_source = contextStack.curr().read_source;
	if (read_source.is_substr(line.get())) {
		read_source.skip(strl_t(line.get()-read_source.get()));
		StatusCode error = AddMacro(read_source, contextStack.curr().source_name,
									contextStack.curr().source_file, read_source);
		contextStack.curr().next_source = read_source;
		return error;
	}
	return STATUS_OK;
}

// include: read in a source file and assemble at this point
StatusCode Asm::Directive_Include(strref line)
{
	strref file = line.between('"', '"');
	if (!file)								// MERLIN: No quotes around PUT filenames
		file = line.split_range(filename_end_char_range);
	size_t size = 0;
	char *buffer = nullptr;
	if ((buffer = LoadText(file, size))) {
		loadedData.push_back(buffer);
		strref src(buffer, strl_t(size));
		contextStack.push(file, src, src);
	} else if (syntax == SYNTAX_MERLIN) {
		// MERLIN include file name rules
		if (file[0]>='!' && file[0]<='&' && (buffer = LoadText(file+1, size))) {
			loadedData.push_back(buffer);		// MERLIN: prepend with !-& to not auto-prepend with T.
			strref src(buffer, strl_t(size));
			contextStack.push(file+1, src, src);
		} else {
			strown<512> fileadd(file[0]>='!' && file[0]<='&' ? (file+1) : file);
			fileadd.append(".s");
			if ((buffer = LoadText(fileadd.get_strref(), size))) {
				loadedData.push_back(buffer);	// MERLIN: !+filename appends .S to filenames
				strref src(buffer, strl_t(size));
				contextStack.push(file, src, src);
			} else {
				fileadd.copy("T.");				// MERLIN: just filename prepends T. to filenames
				fileadd.append(file[0]>='!' && file[0]<='&' ? (file+1) : file);
				if ((buffer = LoadText(fileadd.get_strref(), size))) {
					loadedData.push_back(buffer);
					strref src(buffer, strl_t(size));
					contextStack.push(file, src, src);
				}
			}
		}
	}
	if (!size)
		return ERROR_COULD_NOT_INCLUDE_FILE;
	return STATUS_OK;
}

// incbin: import binary data in place
StatusCode Asm::Directive_Incbin(strref line, int skip, int len)
{
	line = line.between('"', '"');
	strown<512> filename(line);
	size_t size = 0;
	if (char *buffer = LoadBinary(line, size)) {
		int bin_size = (int)size - skip;
		if (bin_size>len)
			bin_size = len;
		if (bin_size>0)
			AddBin((const unsigned char*)buffer+skip, bin_size);
		free(buffer);
		return STATUS_OK;
	}

	return ERROR_COULD_NOT_INCLUDE_FILE;
}

// import is a catch-all file reference
StatusCode Asm::Directive_Import(strref line)
{
	line.skip_whitespace();
	
	int skip = 0;	// binary import skip this amount
	int len = 0;	// binary import load up to this amount
	strref param;	// read out skip & max len parameters
	int q = line.find('"');
	if (q>=0) {
		param = line + q;
		param.scoped_block_skip();
		param.trim_whitespace();
		if (param[0]==',') {
			++param;
			param.skip_whitespace();
			if (param) {
				struct EvalContext etx;
				SetEvalCtxDefaults(etx);
				EvalExpression(param.split_token_trim(','), etx, skip);
				if (param)
					EvalExpression(param, etx, len);
			}
		}
	}
	
	if (line[0]=='"')
		return Directive_Incbin(line);
	else if (import_source.is_prefix_word(line)) {
		line += import_source.get_len();
		line.skip_whitespace();
		return Directive_Include(line);
	} else if (import_binary.is_prefix_word(line)) {
		line += import_binary.get_len();
		line.skip_whitespace();
		return Directive_Incbin(line, skip, len);
	} else if (import_c64.is_prefix_word(line)) {
		line += import_c64.get_len();
		line.skip_whitespace();
		return Directive_Incbin(line, 2+skip, len); // 2 = load address skip size
	} else if (import_text.is_prefix_word(line)) {
		line += import_text.get_len();
		line.skip_whitespace();
		strref text_type = "petscii";
		if (line[0]!='"') {
			text_type = line.get_word_ws();
			line += text_type.get_len();
			line.skip_whitespace();
		}
		CurrSection().AddText(line, text_type);
		return STATUS_OK;
	} else if (import_object.is_prefix_word(line)) {
		line += import_object.get_len();
		line.trim_whitespace();
		return ReadObjectFile(line[0]=='"' ? line.between('"', '"') : line);
	} else if (import_symbols.is_prefix_word(line)) {
		line += import_symbols.get_len();
		line.skip_whitespace();
		IncludeSymbols(line);
		return STATUS_OK;
	}
	
	return STATUS_OK;
}

// org / pc: current address of code
StatusCode Asm::Directive_ORG(strref line)
{
	int addr;
	if (line[0]=='=')
		++line;
	else if (keyword_equ.is_prefix_word(line))	// optional '=' or equ
		line.next_word_ws();
	line.skip_whitespace();
	
	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	StatusCode error = EvalExpression(line, etx, addr);
	if (error != STATUS_OK)
		return (error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT) ?
			ERROR_TARGET_ADDRESS_MUST_EVALUATE_IMMEDIATELY : error;

	// Section immediately followed by ORG reassigns that section to be fixed
	if (CurrSection().size()==0 && !CurrSection().IsDummySection()) {
		if (CurrSection().type == ST_ZEROPAGE && addr >= 0x100)
			return ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE;
		CurrSection().start_address = addr;
		CurrSection().load_address = addr;
		CurrSection().address = addr;
		CurrSection().address_assigned = true;
		LinkLabelsToAddress(SectionId(), addr);	// in case any labels were defined prior to org & data
	} else
		SetSection(strref(), addr);
	return STATUS_OK;
}

// load: address for target to load code at
StatusCode Asm::Directive_LOAD(strref line)
{
	int addr;
	if (line[0]=='=' || keyword_equ.is_prefix_word(line))
		line.next_word_ws();

	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	StatusCode error = EvalExpression(line, etx, addr);
	if (error != STATUS_OK)
		return (error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT) ?
			ERROR_TARGET_ADDRESS_MUST_EVALUATE_IMMEDIATELY : error;

	CurrSection().SetLoadAddress(addr);
	return STATUS_OK;
}

// MERLIN version of AD_LINK, which is more like AD_INCOBJ + link to current section
StatusCode Asm::Directive_LNK(strref line)
{
	strref file = line.between('"', '"');
	if (!file)		// MERLIN: No quotes around include filenames
		file = line.split_range(filename_end_char_range);
	StatusCode error = ReadObjectFile(file);
	if (!error && !CurrSection().IsRelativeSection())
		link_all_section = true;
	return error;
}

// this stores a string that when matched with a label will make that label external
StatusCode Asm::Directive_XDEF(strref line)
{
	line.trim_whitespace();
	if (strref xdef = line.split_range(syntax == SYNTAX_MERLIN ?
			label_end_char_range_merlin : label_end_char_range)) {
		char f = xdef.get_first();
		char e = xdef.get_last();
		if (f != '.' && f != '!' && f != '@' && e != '$') {
			unsigned int hash = xdef.fnv1a();
			unsigned int pos = FindLabelIndex(hash, xdefs.getKeys(), xdefs.count());
			while (pos < xdefs.count() && xdefs.getKey(pos) == hash) {
				if (xdefs.getValue(pos).same_str_case(xdef))
					return STATUS_OK;
				++pos;
			}
			xdefs.insert(pos, hash);
			xdefs.getValues()[pos] = xdef;
		}
	}
	return STATUS_OK;
}

StatusCode Asm::Directive_XREF(strref label)
{
	// XREF already defined label => no action
	if (!GetLabel(label)) {
		Label *pLabelXREF = AddLabel(label.fnv1a());
		pLabelXREF->label_name = label;
		pLabelXREF->pool_name.clear();
		pLabelXREF->section = -1;	// address labels are based on section
		pLabelXREF->value = 0;
		pLabelXREF->evaluated = true;
		pLabelXREF->pc_relative = true;
		pLabelXREF->external = false;
		pLabelXREF->constant = false;
		pLabelXREF->reference = true;
	}
	return STATUS_OK;
}

// Action based on assembler directive
StatusCode Asm::ApplyDirective(AssemblerDirective dir, strref line, strref source_file)
{
	StatusCode error = STATUS_OK;
	if (!ConditionalAsm()) {	// If conditionally blocked from assembling only check conditional directives
		if (dir!=AD_IF && dir!=AD_IFDEF && dir!=AD_ELSE && dir!=AD_ELIF && dir!=AD_ELSE && dir!=AD_ENDIF)
			return STATUS_OK;
	}
	struct EvalContext etx;
	SetEvalCtxDefaults(etx);
	switch (dir) {
		case AD_CPU:
			for (int c = 0; c < nCPUs; c++) {
				if (line.same_str(aCPUs[c].name)) {
					if (c != cpu)
						SetCPU((CPUIndex)c);
					return STATUS_OK;
				}
			}
			return ERROR_CPU_NOT_SUPPORTED;

		case AD_EXPORT:
			line.trim_whitespace();
			CurrSection().export_append = line.split_label();
			break;

		case AD_ORG:
			return Directive_ORG(line);

		case AD_LOAD:
			return Directive_LOAD(line);

		case AD_SECTION:
			SetSection(line);
			break;

		case AD_LINK:
			return LinkSections(line.get_trimmed_ws());

		case AD_LNK:
			return Directive_LNK(line);

		case AD_INCOBJ: {
			strref file = line.between('"', '"');
			if (!file)		// MERLIN: No quotes around include filenames
				file = line.split_range(filename_end_char_range);
			error = ReadObjectFile(file);
			break;
		}

		case AD_XDEF:
			return Directive_XDEF(line.get_trimmed_ws());

		case AD_XREF:
			Directive_XREF(line.split_range_trim(
				syntax == SYNTAX_MERLIN ? label_end_char_range_merlin : label_end_char_range));
			break;

		case AD_ENT:	// MERLIN version of xdef, makes most recently defined label external
			if (Label *pLastLabel = GetLabel(last_label))
				pLastLabel->external = true;
			break;

		case AD_EXT:
			Directive_XREF(last_label);
			break;

		case AD_ALIGN:		// align: align address to multiple of value, fill space with 0
			if (line) {
				if (line[0]=='=' || keyword_equ.is_prefix_word(line))
					line.next_word_ws();
				int value;
				int status = EvalExpression(line, etx, value);
				if (status == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
					error = ERROR_ALIGN_MUST_EVALUATE_IMMEDIATELY;
				else if (status == STATUS_OK && value>0) {
					if (CurrSection().address_assigned) {
						int add = (CurrSection().GetPC() + value-1) % value;
						for (int a = 0; a < add; a++)
							AddByte(0);
					} else
						CurrSection().align_address = value;
				}
			}
			break;
		case AD_EVAL: {		// eval: display the result of an expression in stdout
			int value = 0;
			strref description = line.find(':')>=0 ? line.split_token_trim(':') : strref();
			line.trim_whitespace();
			if (line && EvalExpression(line, etx, value) == STATUS_OK) {
				if (description) {
					printf("EVAL(%d): " STRREF_FMT ": \"" STRREF_FMT "\" = $%x\n",
						   contextStack.curr().source_file.count_lines(description)+1, STRREF_ARG(description), STRREF_ARG(line), value);
				} else {
					printf("EVAL(%d): \"" STRREF_FMT "\" = $%x\n",
						   contextStack.curr().source_file.count_lines(line)+1, STRREF_ARG(line), value);
				}
			} else if (description) {
				printf("EVAL(%d): \"" STRREF_FMT ": " STRREF_FMT"\"\n",
					   contextStack.curr().source_file.count_lines(description)+1, STRREF_ARG(description), STRREF_ARG(line));
			} else {
				printf("EVAL(%d): \"" STRREF_FMT "\"\n",
					   contextStack.curr().source_file.count_lines(line)+1, STRREF_ARG(line));
			}
			break;
		}
		case AD_BYTES:		// bytes: add bytes by comma separated values/expressions
			if (syntax==SYNTAX_MERLIN && line.get_first()=='#')	// MERLIN allows for an immediate declaration on data
				++line;
			while (strref exp = line.split_token_trim(',')) {
				int value;
				if (syntax==SYNTAX_MERLIN && exp.get_first()=='#')	// MERLIN allows for an immediate declaration on data
					++exp;
				error = EvalExpression(exp, etx, value);
				if (error>STATUS_XREF_DEPENDENT)
					break;
				else if (error==STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], exp, source_file, LateEval::LET_BYTE);
				else if (error == STATUS_RELATIVE_SECTION)
					CurrSection().AddReloc(lastEvalValue, CurrSection().DataOffset(), lastEvalSection, 1, lastEvalShift);
				AddByte(value);
			}
			break;
		case AD_WORDS:		// words: add words (16 bit values) by comma separated values
			while (strref exp_w = line.split_token_trim(',')) {
				int value = 0;
				if (!CurrSection().IsDummySection()) {
					if (syntax==SYNTAX_MERLIN && exp_w.get_first()=='#')	// MERLIN allows for an immediate declaration on data
						++exp_w;
					error = EvalExpression(exp_w, etx, value);
					if (error>STATUS_XREF_DEPENDENT)
						break;
					else if (error==STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
						AddLateEval(CurrSection().DataOffset(), CurrSection().DataOffset(), scope_address[scope_depth], exp_w, source_file, LateEval::LET_ABS_REF);
					else if (error == STATUS_RELATIVE_SECTION) {
						CurrSection().AddReloc(lastEvalValue, CurrSection().DataOffset(), lastEvalSection, 2, lastEvalShift);
						value = 0;
					}
				}
				AddWord(value);
			}
			break;
			
		case AD_ADR:		// ADR: MERLIN store 3 byte word
		case AD_ADRL: {		// ADRL: MERLIN store 4 byte word
			while (strref exp_w = line.split_token_trim(',')) {
				int value = 0;
				if (!CurrSection().IsDummySection()) {
					if (syntax==SYNTAX_MERLIN && exp_w.get_first()=='#')	// MERLIN allows for an immediate declaration on data
						++exp_w;
					error = EvalExpression(exp_w, etx, value);
					if (error>STATUS_XREF_DEPENDENT)
						break;
					else if (error==STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
						AddLateEval(CurrSection().DataOffset(), CurrSection().DataOffset(), scope_address[scope_depth], exp_w, source_file, dir==AD_ADR ? LateEval::LET_ABS_L_REF : LateEval::LET_ABS_4_REF);
					else if (error == STATUS_RELATIVE_SECTION) {
						CurrSection().AddReloc(lastEvalValue, CurrSection().DataOffset(), lastEvalSection, dir==AD_ADRL ? 4 : 3, lastEvalShift);
						value = 0;
					}
				}
				unsigned char bytes[4] = {
					(unsigned char)value, (unsigned char)(value>>8),
					(unsigned char)(value>>16), (unsigned char)(value>>24) };
				AddBin(bytes, dir==AD_ADRL ? 4 : 3);
			}
			break;
		}

		case AD_DC: {
			bool words = false;
			if (line[0]=='.') {
				++line;
				if (line[0]=='b' || line[0]=='B') {
				} else if (line[0]=='w' || line[0]=='W')
					words = true;
				else
					return ERROR_BAD_TYPE_FOR_DECLARE_CONSTANT;
				++line;
				line.skip_whitespace();
			}
			while (strref exp_dc = line.split_token_trim(',')) {
				int value = 0;
				if (!CurrSection().IsDummySection()) {
					if (syntax==SYNTAX_MERLIN && exp_dc.get_first()=='#')	// MERLIN allows for an immediate declaration on data
						++exp_dc;
					error = EvalExpression(exp_dc, etx, value);
					if (error > STATUS_XREF_DEPENDENT)
						break;
					else if (error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
						AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], exp_dc, source_file, words ? LateEval::LET_ABS_REF : LateEval::LET_BYTE);
					else if (error == STATUS_RELATIVE_SECTION) {
						value = 0;
						if (words)
							CurrSection().AddReloc(lastEvalValue, CurrSection().DataOffset(), lastEvalSection, 2, lastEvalShift);
						else
							CurrSection().AddReloc(lastEvalValue, CurrSection().DataOffset(), lastEvalSection, 1, lastEvalShift);
					}
				}
				AddByte(value);
				if (words)
					AddByte(value>>8);
			}
			break;
		}
			
		case AD_HEX: {
			unsigned char b = 0, v = 0;
			while (line) {	// indeterminable length, can't read hex to int
				char c = *line.get();
				++line;
				if (c == ',') {
					if (b) // probably an error but seems safe
						AddByte(v);
					b = 0;
					line.skip_whitespace();
				} else {
					if (c >= '0' && c <= '9') v = (v << 4) + (c - '0');
					else if (c >= 'A' && c <= 'Z') v = (v << 4) + (c - 'A' + 10);
					else if (c >= 'a' && c <= 'z') v = (v << 4) + (c - 'a' + 10);
					else break;
					b ^= 1;
					if (!b)
						AddByte(v);
				}
			}
			if (b)
				error = ERROR_HEX_WITH_ODD_NIBBLE_COUNT;
			break;
		}
		case AD_EJECT:
			line.clear();
			break;
		case AD_USR:
			line.clear();
			break;

		case AD_CYC:
			list_flags |= cycle_counter_level ? ListLine::CYCLES_STOP : ListLine::CYCLES_START;
			cycle_counter_level = !!cycle_counter_level;
			break;
			
		case AD_SAV:
			line.trim_whitespace();
			if (line.has_prefix(export_base_name))
				line.skip(export_base_name.get_len());
			if (line)
				CurrSection().export_append = line.split_label();
			break;
			
		case AD_XC:			// XC: MERLIN version of setting CPU
			if (strref("off").is_prefix_word(line))
				SetCPU(CPU_6502);
			else if (strref("xc").is_prefix_word(line))
				SetCPU(CPU_65816);
			else if (cpu==CPU_65C02)
				SetCPU(CPU_65816);
			else
				SetCPU(CPU_65C02);
			break;
			
		case AD_TEXT: {		// text: add text within quotes
			strref text_prefix = line.before('"').get_trimmed_ws();
			line = line.between('"', '"');
			CurrSection().AddText(line, text_prefix);
			break;
		}
		case AD_MACRO:
			error = Directive_Macro(line, source_file);
			break;

		case AD_INCLUDE:	// assemble another file in place
			return Directive_Include(line);
			
		case AD_INCBIN:
			return Directive_Incbin(line);
			
		case AD_IMPORT:
			return Directive_Import(line);

		case AD_LABEL:
		case AD_CONST: {
			line.trim_whitespace();
			strref label = line.split_range_trim(word_char_range, line[0]=='.' ? 1 : 0);
			if (line[0]=='=' || keyword_equ.is_prefix_word(line)) {
				line.next_word_ws();
				AssignLabel(label, line, dir==AD_CONST);
			} else
				error = ERROR_UNEXPECTED_LABEL_ASSIGMENT_FORMAT;
			break;
		}
			
		case AD_INCSYM:
			IncludeSymbols(line);
			break;

		case AD_LABPOOL: {
			strref name = line.split_range_trim(word_char_range, line[0]=='.' ? 1 : 0);
			AddLabelPool(name, line);
			break;
		}

		case AD_IF:
			if (NewConditional()) {			// Start new conditional block
				CheckConditionalDepth();	// Check if nesting
				bool conditional_result;
				error = EvalStatement(line, conditional_result);
				if (conditional_result)
					ConsumeConditional();
				else
					SetConditional();
			}
			break;
			
		case AD_IFDEF:
			if (NewConditional()) {			// Start new conditional block
				CheckConditionalDepth();	// Check if nesting
				bool conditional_result;
				error = EvalStatement(line, conditional_result);
				if (GetLabel(line.get_trimmed_ws()) != nullptr)
					ConsumeConditional();
				else
					SetConditional();
			}
			break;
			
		case AD_ELSE:
			if (ConditionalAsm()) {
				if (ConditionalConsumed())
					ConditionalElse();
				else
					error = ERROR_ELSE_WITHOUT_IF;
			} else if (ConditionalAvail())
				EnableConditional(true);
			break;
			
		case AD_ELIF:
			if (ConditionalAsm()) {
				if (ConditionalConsumed())
					ConditionalElse();
				else
					error = ERROR_ELSE_WITHOUT_IF;
			} else if (ConditionalAvail()) {
				bool conditional_result;
				error = EvalStatement(line, conditional_result);
				EnableConditional(conditional_result);
			}
			break;
			
		case AD_ENDIF:
			if (ConditionalAsm()) {
				if (ConditionalConsumed())
					CloseConditional();
				else
					error = ERROR_ENDIF_WITHOUT_CONDITION;
			} else {
				conditional_nesting[conditional_depth]--;
				if (ConditionalAsm())
					CloseConditional();
			}
			break;
			
		case AD_ENUM:
		case AD_STRUCT: {
			strref read_source = contextStack.curr().read_source;
			if (read_source.is_substr(line.get())) {
				strref struct_name = line.get_word();
				line.skip(struct_name.get_len());
				line.skip_whitespace();
				read_source.skip(strl_t(line.get() - read_source.get()));
				if (read_source[0]=='{') {
					if (dir == AD_STRUCT)
						BuildStruct(struct_name, read_source.scoped_block_skip());
					else
						BuildEnum(struct_name, read_source.scoped_block_skip());
				} else
					error = dir == AD_STRUCT ? ERROR_STRUCT_CANT_BE_ASSEMBLED :
					ERROR_ENUM_CANT_BE_ASSEMBLED;
				contextStack.curr().next_source = read_source;
			} else
				error = ERROR_STRUCT_CANT_BE_ASSEMBLED;
			break;
		}
			
		case AD_REPT:
			return Directive_Rept(line, source_file);

		case AD_INCDIR:
			AddIncludeFolder(line.between('"', '"'));
			break;
			
		case AD_A16:			// A16: Set 16 bit accumulator mode
			accumulator_16bit = true;
			break;
			
		case AD_A8:			// A8: Set 8 bit accumulator mode
			accumulator_16bit = false;
			break;
			
		case AD_XY16:			// A16: Set 16 bit accumulator mode
			index_reg_16bit = true;
			break;
			
		case AD_XY8:			// A8: Set 8 bit accumulator mode
			index_reg_16bit = false;
			break;
			
		case AD_MX:
			if (line) {
				line.trim_whitespace();
				int value = 0;
				error = EvalExpression(line, etx, value);
				index_reg_16bit = !(value&1);
				accumulator_16bit = !(value&2);
			}
			break;
			
		case AD_LST:
			line.clear();
			break;
			
		case AD_DUMMY:
			line.trim_whitespace();
			if (line) {
				int reorg;
				if (STATUS_OK == EvalExpression(line, etx, reorg)) {
					DummySection(reorg);
					break;
				}
			}
			DummySection();
			break;
			
		case AD_DUMMY_END:
			while (CurrSection().IsDummySection()) {
				EndSection();
				if (SectionId()==0)
					break;
			}
			break;
			
		case AD_DS: {
			int value;
			strref size = line.split_token_trim(',');
			if (STATUS_OK != EvalExpression(size, etx, value))
				return ERROR_DS_MUST_EVALUATE_IMMEDIATELY;
			int fill = 0;
			if (line && STATUS_OK != EvalExpression(line, etx, fill))
				return ERROR_DS_MUST_EVALUATE_IMMEDIATELY;
			if (value > 0) {
				for (int n = 0; n < value; n++)
					AddByte(fill);
			} else if (value) {
				CurrSection().AddAddress(value);
				if (CurrSection().type == ST_ZEROPAGE && CurrSection().address > 0x100)
					return ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE;
			}
			break;
		}
	}
	return error;
}

// Make an educated guess at the intended address mode from an opcode argument
StatusCode Asm::GetAddressMode(strref line, bool flipXY, unsigned int validModes, AddrMode &addrMode, int &len, strref &expression)
{
	bool force_zp = false;
	bool force_24 = false;
	bool force_abs = false;
	bool need_more = true;
	strref arg, deco;

	len = 0;
	while (need_more) {
		need_more = false;
		unsigned char c = line.get_first();
		if (!c)
			addrMode = AMB_NON;
		else if (!force_abs && (c == '[' || (c == '(' &&
				(validModes&(AMM_REL | AMM_REL_X | AMM_ZP_REL_X | AMM_ZP_Y_REL))))) {
			deco = line.scoped_block_skip();
			line.skip_whitespace();
			expression = deco.split_token_trim(',');
			addrMode = c == '[' ? (force_zp ? AMB_ZP_REL_L : AMB_REL_L) : (force_zp ? AMB_ZP_REL : AMB_REL);
			if (strref::tolower(deco[0]) == 'x')
				addrMode = c == '[' ? AMB_ILL : AMB_ZP_REL_X;
			else if (line[0] == ',') {
				++line;
				line.skip_whitespace();
				if (strref::tolower(line[0]) == 'y') {
					if (strref::tolower(deco[0]) == 's')
						addrMode = AMB_STK_REL_Y;
					else
						addrMode = c == '[' ? AMB_ZP_REL_Y_L : AMB_ZP_Y_REL;
					++line;
				}
			}
		} else if (c == '#') {
			++line;
			addrMode = AMB_IMM;
			expression = line;
		} else if (line) {
			if (line[0]=='.' && strref::is_ws(line[2])) {
				switch (strref::tolower(line[1])) {
					case 'z': force_zp = true; line += 3; need_more = true; len = 1; break;
					case 'b': line += 3; need_more = true; len = 1; break;
					case 'w': line += 3; need_more = true; len = 2; break;
					case 'l': force_24 = true;  line += 3; need_more = true; len = 3; break;
					case 'a': force_abs = true;  line += 3; need_more = true; break;
				}
			}
			if (!need_more) {
				if (strref("A").is_prefix_word(line)) {
					addrMode = AMB_ACC;
				} else {	// absolute (zp, offs x, offs y)
					addrMode = force_24 ? AMB_ABS_L : (force_zp ? AMB_ZP : AMB_ABS);
					expression = line.split_token_trim(',');
					if (line && (line[0]=='s' || line[0]=='S'))
						addrMode = AMB_STK;
					else {
						bool relX = line && (line[0]=='x' || line[0]=='X');
						bool relY = line && (line[0]=='y' || line[0]=='Y');
						if ((flipXY && relY) || (!flipXY && relX))
							addrMode = force_24 ? AMB_ABS_L_X : (force_zp ? AMB_ZP_X : AMB_ABS_X);
						else if ((flipXY && relX) || (!flipXY && relY)) {
							if (force_zp)
								return ERROR_INSTRUCTION_NOT_ZP;
							addrMode = AMB_ABS_Y;
						}
					}
				}
			}
		}
	}
	return STATUS_OK;
}

// Push an opcode to the output buffer in the current section
StatusCode Asm::AddOpcode(strref line, int index, strref source_file)
{
	StatusCode error = STATUS_OK;
	strref expression;

	// allowed modes
	unsigned int validModes = opcode_table[index].modes;

	// instruction parameter length override
	int op_param = 0;

	// Get the addressing mode and the expression it refers to
	AddrMode addrMode;
	switch (validModes) {
		case AMC_BBR:
			addrMode = AMB_ZP_ABS;
			expression = line.split_token_trim(',');
			if (!expression || !line)
				return ERROR_INVALID_ADDRESSING_MODE;
			break;
		case AMM_BRA:
			addrMode = AMB_ABS;
			expression = line;
			break;
		case AMM_ACC:
		case (AMM_ACC|AMM_NON):
		case AMM_NON:
			addrMode = AMB_NON;
			break;
		case AMM_BLK_MOV:
			addrMode = AMB_BLK_MOV;
			expression = line.before_or_full(',');
			break;
		default:
			error = GetAddressMode(line, !!(validModes & AMM_FLIPXY), validModes, addrMode, op_param, expression);
			break;
	}

	int value = 0;
	int target_section = -1;
	int target_section_offs = -1;
	char target_section_shift = 0;
	bool evalLater = false;
	if (expression) {
		struct EvalContext etx;
		SetEvalCtxDefaults(etx);
		if (validModes & (AMM_BRANCH | AMM_BRANCH_L))
			etx.relative_section = SectionId();
		error = EvalExpression(expression, etx, value);
		if (error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT) {
			evalLater = true;
			error = STATUS_OK;
		} else if (error == STATUS_RELATIVE_SECTION) {
			target_section = lastEvalSection;
			target_section_offs = lastEvalValue;
			target_section_shift = lastEvalShift;
		} else if (error != STATUS_OK)
			return error;
	}

	// check if address is in zero page range and should use a ZP mode instead of absolute
	if (!evalLater && value>=0 && value<0x100 && (error != STATUS_RELATIVE_SECTION ||
		(target_section>=0 && allSections[target_section].type==ST_ZEROPAGE))) {
		switch (addrMode) {
			case AMB_ABS:
				if (validModes & AMM_ZP)
					addrMode = AMB_ZP;
				else if (validModes & AMM_ABS_L)
					addrMode = AMB_ABS_L;
				break;
			case AMB_ABS_X:
				if (validModes & AMM_ZP_X)
					addrMode = AMB_ZP_X;
				else if (validModes & AMM_ABS_L_X)
					addrMode = AMB_ABS_L_X;
				break;
			default:
				break;
		}
	}

	// Check if an explicit 24 bit address
	if (expression[0] == '$' && (expression + 1).len_hex()>4) {
		if (addrMode == AMB_ABS && (validModes & AMM_ABS_L))
			addrMode = AMB_ABS_L;
		else if (addrMode == AMB_ABS_X && (validModes & AMM_ABS_L_X))
			addrMode = AMB_ABS_L_X;
	}

	if (!(validModes & (1 << addrMode))) {
		if (addrMode==AMB_ZP_REL_X && (validModes & AMM_REL_X))
			addrMode = AMB_REL_X;
		else if (addrMode==AMB_REL && (validModes & AMM_ZP_REL))
			addrMode = AMB_ZP_REL;
		else if (addrMode==AMB_ABS && (validModes & AMM_ABS_L))
			addrMode = AMB_ABS_L;
		else if (addrMode==AMB_ABS_X && (validModes & AMM_ABS_L_X))
			addrMode = AMB_ABS_L_X;
		else if (addrMode==AMB_REL_L && (validModes & AMM_ZP_REL_L))
			addrMode = AMB_ZP_REL_L;
		else if (syntax == SYNTAX_MERLIN && addrMode==AMB_IMM && validModes==AMM_ABS)
			addrMode = AMB_ABS;	// Merlin seems to allow this
		else if (syntax == SYNTAX_MERLIN && addrMode==AMB_ABS && validModes==AMM_ZP_REL)
			addrMode = AMB_ZP_REL;	// Merlin seems to allow this
		else
			return ERROR_INVALID_ADDRESSING_MODE;
	}

	// Add the instruction and argument to the code
	if (error == STATUS_OK || error == STATUS_RELATIVE_SECTION) {
		unsigned char opcode = opcode_table[index].aCodes[addrMode];
		CheckOutputCapacity(4);
		AddByte(opcode);

		CODE_ARG codeArg = CA_NONE;
		if (validModes & AMM_BRANCH_L)
			codeArg = CA_BRANCH_16;
		else if (validModes & AMM_BRANCH)
			codeArg = CA_BRANCH;
		else {
			switch (addrMode) {
				case AMB_ZP_REL_X:		// 0 ($12:x)
				case AMB_ZP:			// 1 $12
				case AMB_ZP_Y_REL:		// 4 ($12),y
				case AMB_ZP_X:			// 5 $12,x
				case AMB_ZP_REL:		// b ($12)
				case AMB_ZP_REL_L:		// e [$02]
				case AMB_ZP_REL_Y_L:	// f [$00],y
				case AMB_STK:			// 12 $12,s
				case AMB_STK_REL_Y:		// 13 ($12,s),y
					codeArg = CA_ONE_BYTE;
					break;

				case AMB_ABS_Y:			// 6 $1234,y
				case AMB_ABS_X:			// 7 $1234,x
				case AMB_ABS:			// 3 $1234
				case AMB_REL:			// 8 ($1234)
				case AMB_REL_X:			// c ($1234,x)
				case AMB_REL_L:			// 14 [$1234]
					codeArg = CA_TWO_BYTES;
					break;

				case AMB_ABS_L:			// 10 $e00001
				case AMB_ABS_L_X:		// 11 $123456,x
					codeArg = CA_THREE_BYTES;
					break;

				case AMB_ZP_ABS:			// d $12, label
					codeArg = CA_BYTE_BRANCH;
					break;

				case AMB_BLK_MOV:		// 15 $12,$34
					codeArg = CA_TWO_ARG_BYTES;
					break;

				case AMB_IMM:			// 2 #$12
					if (op_param && (validModes&(AMM_IMM_DBL_A | AMM_IMM_DBL_XY)))
						codeArg = op_param == 2 ? CA_TWO_BYTES : CA_ONE_BYTE;
					else if ((validModes&(AMM_IMM_DBL_A | AMM_IMM_DBL_XY)) &&
							 expression[0]=='$' && (expression+1).len_hex()==4)
						codeArg = CA_TWO_BYTES;
					else if (((validModes&AMM_IMM_DBL_A) && accumulator_16bit) ||
						((validModes&AMM_IMM_DBL_XY) && index_reg_16bit))
						codeArg = CA_TWO_BYTES;
					else
						codeArg = CA_ONE_BYTE;
					break;
					
				case AMB_ACC:			// 9 A
				case AMB_NON:			// a
				default:
					break;
					
			}
		}
			
		switch (codeArg) {
			case CA_ONE_BYTE:
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_BYTE);
				else if (error == STATUS_RELATIVE_SECTION)
					CurrSection().AddReloc(target_section_offs, CurrSection().DataOffset(), target_section, 1, target_section_shift);
				AddByte(value);
				break;

			case CA_TWO_BYTES:
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_ABS_REF);
				else if (error == STATUS_RELATIVE_SECTION) {
					CurrSection().AddReloc(target_section_offs, CurrSection().DataOffset(), target_section, 2, target_section_shift);
					value = 0;
				}
				AddWord(value);
				break;

			case CA_THREE_BYTES:
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_ABS_L_REF);
				else if (error == STATUS_RELATIVE_SECTION) {
					CurrSection().AddReloc(target_section_offs, CurrSection().DataOffset(), target_section, 3, target_section_shift);
					value = 0;
				}
				AddTriple(value);
				break;

			case  CA_TWO_ARG_BYTES: {
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_BYTE);
				else if (error == STATUS_RELATIVE_SECTION) {
					CurrSection().AddReloc(target_section_offs, CurrSection().DataOffset(), target_section, 1, target_section_shift);
				}
				AddByte(value);
				struct EvalContext etx;
				SetEvalCtxDefaults(etx);
				etx.pc = CurrSection().GetPC()-2;
				line.split_token_trim(',');
				error = EvalExpression(line, etx, value);
				if (error==STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], line, source_file, LateEval::LET_BYTE);
				AddByte(value);
				break;
			}
			case CA_BRANCH:
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_BRANCH);
				else if (((int)value - (int)CurrSection().GetPC()-1) < -128 || ((int)value - (int)CurrSection().GetPC()-1) > 127)
					error = ERROR_BRANCH_OUT_OF_RANGE;
				AddByte(evalLater ? 0 : (unsigned char)((int)value - (int)CurrSection().GetPC()) - 1);
				break;

			case CA_BRANCH_16:
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_BRANCH_16);
				AddWord(evalLater ? 0 : (value-(CurrSection().GetPC()+2)));
				break;

			case CA_BYTE_BRANCH: {
				if (evalLater)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], expression, source_file, LateEval::LET_BYTE);
				else if (error == STATUS_RELATIVE_SECTION)
					CurrSection().AddReloc(target_section_offs, CurrSection().DataOffset(), target_section, 1, target_section_shift);
				AddByte(value);
				struct EvalContext etx;
				SetEvalCtxDefaults(etx);
				etx.pc = CurrSection().GetPC()-2;
				etx.relative_section = SectionId();
				error = EvalExpression(line, etx, value);
				if (error==STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT)
					AddLateEval(CurrSection().DataOffset(), CurrSection().GetPC(), scope_address[scope_depth], line, source_file, LateEval::LET_BRANCH);
				else if (((int)value - (int)CurrSection().GetPC() - 1) < -128 || ((int)value - (int)CurrSection().GetPC() - 1) > 127)
					error = ERROR_BRANCH_OUT_OF_RANGE;
				AddByte((error == STATUS_NOT_READY || error == STATUS_XREF_DEPENDENT) ?
						0 : (unsigned char)((int)value - (int)CurrSection().GetPC()) - 1);
				break;
			}
			case CA_NONE:
				break;
		}
	}
	return error;
}

// Build a line of code
void Asm::PrintError(strref line, StatusCode error)
{
	strown<512> errorText;
	if (contextStack.has_work()) {
		errorText.sprintf("Error " STRREF_FMT "(%d): ", STRREF_ARG(contextStack.curr().source_name),
						  contextStack.curr().source_file.count_lines(line)+1);
	} else
		errorText.append("Error: ");
	errorText.append(aStatusStrings[error]);
	errorText.append(" \"");
	errorText.append(line.get_trimmed_ws());
	errorText.append("\"\n");
	errorText.c_str();
	fwrite(errorText.get(), errorText.get_len(), 1, stderr);
	error_encountered = true;
}

// Build a line of code
StatusCode Asm::BuildLine(strref line)
{
	StatusCode error = STATUS_OK;

	// MERLIN: First char of line is * means comment
	if (syntax==SYNTAX_MERLIN && line[0]=='*')
		return STATUS_OK;

	// remember for listing
	int start_section = SectionId();
	int start_address = CurrSection().address;
	strref code_line = line;
	list_flags = 0;
	while (line && error == STATUS_OK) {
		strref line_start = line;
		char char0 = line[0];				// first char including white space
		line.skip_whitespace();				// skip to first character
		line = line.before_or_full(';');	// clip any line comments
		line = line.before_or_full(c_comment);
		line.clip_trailing_whitespace();
		if (line[0]==':' && syntax!=SYNTAX_MERLIN)	// Kick Assembler macro prefix (incompatible with merlin)
			++line;
		strref line_nocom = line;
		strref operation = line.split_range(syntax==SYNTAX_MERLIN ? label_end_char_range_merlin : label_end_char_range);
		char char1 = operation[0];			// first char of first word
		char charE = operation.get_last();	// end char of first word

		line.trim_whitespace();
		bool force_label = charE==':' || charE=='$';
		if (!force_label && syntax==SYNTAX_MERLIN && (line || operation)) // MERLIN fixes and PoP does some naughty stuff like 'and = 0'
			force_label = !strref::is_ws(char0) || char1==']' || charE=='?';
		else if (!force_label && syntax!=SYNTAX_MERLIN && line[0]==':')
			force_label = true;
		if (!operation && !force_label) {
			if (ConditionalAsm()) {
				// scope open / close
				switch (line[0]) {
					case '{':
						error = EnterScope();
						list_flags |= ListLine::CYCLES_START;
						if (error == STATUS_OK) {
							++line;
							line.skip_whitespace();
						}
						break;
					case '}':
						// check for late eval of anything with an end scope
						error = ExitScope();
						list_flags |= ListLine::CYCLES_STOP;
						if (error == STATUS_OK) {
							++line;
							line.skip_whitespace();
						}
						break;
					case '*':
						// if first char is '*' this seems like a line comment on some assemblers
						line.clear();
						break;
					case 127:
						++line;	// bad character?
						break;
				}
			}
		} else {
			// ignore leading period for instructions and directives - not for labels
			strref label = operation;
			if ((syntax != SYNTAX_MERLIN && operation[0]==':') || operation[0]=='.')
				++operation;
			operation = operation.before_or_full('.');

			int op_idx = LookupOpCodeIndex(operation.fnv1a_lower(), aInstructions, num_instructions);
			if (op_idx >= 0 && !force_label && (aInstructions[op_idx].type==OT_DIRECTIVE || line[0]!='=')) {
				if (line_nocom.is_substr(operation.get())) {
					line = line_nocom + strl_t(operation.get()+operation.get_len()-line_nocom.get());
					line.skip_whitespace();
				}
				if (aInstructions[op_idx].type==OT_DIRECTIVE) {
					error = ApplyDirective((AssemblerDirective)aInstructions[op_idx].index, line, contextStack.curr().source_file);
					list_flags |= ListLine::KEYWORD;
				} else if (ConditionalAsm() && aInstructions[op_idx].type == OT_MNEMONIC) {
					error = AddOpcode(line, aInstructions[op_idx].index, contextStack.curr().source_file);
					list_flags |= ListLine::MNEMONIC;
				}
				line.clear();
			} else if (!ConditionalAsm()) {
				line.clear(); // do nothing if conditional nesting so clear the current line
			} else if (line.get_first()=='=') {
				++line;
				error = AssignLabel(label, line);
				line.clear();
				list_flags |= ListLine::KEYWORD;
			}
			else if (keyword_equ.is_prefix_word(line)) {
				line += keyword_equ.get_len();
				line.skip_whitespace();
				error = AssignLabel(label, line);
				line.clear();
				list_flags |= ListLine::KEYWORD;
			}
			else {
				unsigned int nameHash = label.fnv1a();
				unsigned int macro = FindLabelIndex(nameHash, macros.getKeys(), macros.count());
				bool gotConstruct = false;
				while (macro < macros.count() && nameHash==macros.getKey(macro)) {
					if (macros.getValue(macro).name.same_str_case(label)) {
						error = BuildMacro(macros.getValue(macro), line);
						gotConstruct = true;
						line.clear();	// don't process codes from here
						break;
					}
					macro++;
				}
				if (!gotConstruct) {
					unsigned int labPool = FindLabelIndex(nameHash, labelPools.getKeys(), labelPools.count());
					gotConstruct = false;
					while (labPool < labelPools.count() && nameHash==labelPools.getKey(labPool)) {
						if (labelPools.getValue(labPool).pool_name.same_str_case(label)) {
							error = AssignPoolLabel(labelPools.getValue(labPool), line);
							gotConstruct = true;
							line.clear();	// don't process codes from here
							break;
						}
						labPool++;
					}
					if (!gotConstruct) {
						if (syntax==SYNTAX_MERLIN && strref::is_ws(line_start[0])) {
							error = ERROR_UNDEFINED_CODE;
						} else if (label[0]=='$' || strref::is_number(label[0]))
							line.clear();
						else {
							if (label.get_last()==':')
								label.clip(1);
							error = AddressLabel(label);
							line = line_start + int(label.get() + label.get_len() -line_start.get());
							if (line[0]==':' || line[0]=='?')
								++line;	// there may be codes after the label
							list_flags |= ListLine::KEYWORD;
						}
					}
				}
			}
		}
		// Check for unterminated condition in source
		if (!contextStack.curr().next_source &&
			(!ConditionalAsm() || ConditionalConsumed() || conditional_depth)) {
			if (syntax == SYNTAX_MERLIN) {	// this isn't a listed feature,
				conditional_nesting[0] = 0;	//	some files just seem to get away without closing
				conditional_consumed[0] = 0;
				conditional_depth = 0;
			} else {
				PrintError(conditional_source[conditional_depth], error);
				return ERROR_UNTERMINATED_CONDITION;
			}
		}

		if (line.same_str_case(line_start))
			error = ERROR_UNABLE_TO_PROCESS;
		else if (CurrSection().type == ST_ZEROPAGE && CurrSection().address > 0x100)
			error = ERROR_ZEROPAGE_SECTION_OUT_OF_RANGE;

		if (error > STATUS_XREF_DEPENDENT)
			PrintError(line_start, error);

		// dealt with error, continue with next instruction unless too broken
		if (error < ERROR_STOP_PROCESSING_ON_HIGHER)
			error = STATUS_OK;
	}
	// update listing
	if (error == STATUS_OK && list_assembly) {
		if (SectionId() == start_section) {
			Section &curr = CurrSection();
			if (!curr.pListing)
				curr.pListing = new Listing;
			if (curr.pListing && curr.pListing->size() == curr.pListing->capacity())
				curr.pListing->reserve(curr.pListing->size() + 256);
			if (((list_flags&(ListLine::KEYWORD|ListLine::CYCLES_START|ListLine::CYCLES_STOP)) ||
					(curr.address != start_address && curr.size())) && !curr.IsDummySection()) {
				struct ListLine lst;
				lst.address = start_address - curr.start_address;
				lst.size = curr.address - start_address;
				lst.code = contextStack.curr().source_file;
				lst.source_name = contextStack.curr().source_name;
				lst.line_offs = int(code_line.get() - lst.code.get());
				lst.flags = list_flags;
				curr.pListing->push_back(lst);
			}
		}
	}
	return error;
}

// Build a segment of code (file or macro)
StatusCode Asm::BuildSegment()
{
	StatusCode error = STATUS_OK;
	while (contextStack.curr().read_source) {
		contextStack.curr().next_source = contextStack.curr().read_source;
		error = BuildLine(contextStack.curr().next_source.line());
		if (error > ERROR_STOP_PROCESSING_ON_HIGHER)
			break;
		contextStack.curr().read_source = contextStack.curr().next_source;
	}
	if (error == STATUS_OK) {
		error = CheckLateEval(strref(), CurrSection().GetPC());
	}
	return error;
}

// Produce the assembler listing
#define MAX_DEPTH_CYCLE_COUNTER 64

struct cycleCnt {
	int base;
	short plus, a16, x16, dp;
	void clr() { base = 0; plus = a16 = x16 = dp = 0; }
	void add(unsigned char c) {
		if (c != 0xff) {
			base += (c >> 1) & 7;
			plus += c & 1;
			if (c & 0xf0) {
				int i = c >> 4;
				if (i <= 8) {
					a16 += timing_65816_plus[i][0];
					x16 += timing_65816_plus[i][1];
					dp += timing_65816_plus[i][2];
				}
			}
		}
	}
	int plus_acc() {
		return plus + a16 + x16 + dp;
	}
	void combine(const struct cycleCnt &o) {
		base += o.base; plus += o.plus; a16 += o.a16; x16 += o.x16; dp += o.dp;
	}
	bool complex() const { return a16 != 0 || x16 != 0 || dp != 0; }
	static int get_base(unsigned char c) {
		return (c & 0xf) >> 1;
	}
	static int sum_plus(unsigned char c) {
		if (c == 0xff)
			return 0;
		int i = c >> 4;
		if (i)
			return i <= 8 ? (timing_65816_plus[i][0] + timing_65816_plus[i][1] + timing_65816_plus[i][2]) : 0;
		return c & 1;
	}
};

bool Asm::List(strref filename)
{
	FILE *f = stdout;
	bool opened = false;
	if (filename) {
		f = fopen(strown<512>(filename).c_str(), "w");
		if (!f)
			return false;
		opened = true;
	}

	// ensure that the greatest instruction set referenced is used for listing
	if (list_cpu != cpu)
		SetCPU(list_cpu);

	// Build a disassembly lookup table
	unsigned char mnemonic[256];
	unsigned char addrmode[256];
	memset(mnemonic, 255, sizeof(mnemonic));
	memset(addrmode, 255, sizeof(addrmode));
	for (int i = 0; i < opcode_count; i++) {
		for (int j = AMB_COUNT-1; j >= 0; j--) {
			if (opcode_table[i].modes & (1 << j)) {
				unsigned char op = opcode_table[i].aCodes[j];
				if (addrmode[op]==255) {
					mnemonic[op] = i;
					addrmode[op] = j;
				}
			}
		}
	}

	struct cycleCnt cycles[MAX_DEPTH_CYCLE_COUNTER];
	short cycles_depth = 0;
	memset(cycles, 0, sizeof(cycles));

	strref prev_src;
	int prev_offs = 0;
	for (std::vector<Section>::iterator si = allSections.begin(); si != allSections.end(); ++si) {
		if (!si->pListing)
			continue;
		for (Listing::iterator li = si->pListing->begin(); li != si->pListing->end(); ++li) {
			strown<256> out;
			const struct ListLine &lst = *li;
			if (prev_src.fnv1a() != lst.source_name.fnv1a() || lst.line_offs < prev_offs) {
				fprintf(f, STRREF_FMT "(%d):\n", STRREF_ARG(lst.source_name), lst.code.count_lines(lst.line_offs));
				prev_src = lst.source_name;
			} else {
				strref prvline = lst.code.get_substr(prev_offs, lst.line_offs - prev_offs);
				prvline.next_line();
				if (prvline.count_lines() < 5) {
					while (strref space_line = prvline.line()) {
						space_line.clip_trailing_whitespace();
						strown<128> line_fix(space_line);
						for (strl_t pos = 0; pos < line_fix.len(); ++pos) {
							if (line_fix[pos] == '\t')
								line_fix.exchange(pos, 1, pos & 1 ? strref(" ") : strref("  "));
						}
						out.append_to(' ', aCPUs[cpu].timing ? 40 : 33);
						out.append(line_fix.get_strref());
						fprintf(f, STRREF_FMT "\n", STRREF_ARG(out));
						out.clear();
					}
				} else {
					fprintf(f, STRREF_FMT "(%d):\n", STRREF_ARG(lst.source_name), lst.code.count_lines(lst.line_offs));
				}
			}

			if (lst.size)
				out.sprintf_append("$%04x ", lst.address + si->start_address);

			int s = lst.wasMnemonic() ? (lst.size < 4 ? lst.size : 4) : (lst.size < 8 ? lst.size : 8);
			if (si->output && si->output_capacity >= (lst.address + s)) {
				for (int b = 0; b < s; ++b)
					out.sprintf_append("%02x ", si->output[lst.address + b]);
			}
			if (lst.startClock() && cycles_depth<MAX_DEPTH_CYCLE_COUNTER) {
				cycles_depth++;	cycles[cycles_depth].clr();
				out.append_to(' ', 6); out.sprintf_append("c>%d", cycles_depth);
			}
			if (lst.stopClock()) {
				out.append_to(' ', 6);
				if (cycles[cycles_depth].complex())
					out.sprintf_append("c<%d = %d + m%d + i%d + d%d", cycles_depth, cycles[cycles_depth].base, cycles[cycles_depth].a16, cycles[cycles_depth].x16, cycles[cycles_depth].dp);
				else
					out.sprintf_append("c<%d = %d + %d", cycles_depth, cycles[cycles_depth].base, cycles[cycles_depth].plus_acc());
				if (cycles_depth) {
					cycles_depth--;
					cycles[cycles_depth].combine(cycles[cycles_depth + 1]);
				}
			}
			if (lst.size && lst.wasMnemonic()) {
				out.append_to(' ', 18);
				unsigned char *buf = si->output + lst.address;
				unsigned char op = mnemonic[*buf];
				unsigned char am = addrmode[*buf];
				if (op != 255 && am != 255 && am<(sizeof(aAddrModeFmt)/sizeof(aAddrModeFmt[0]))) {
					const char *fmt = aAddrModeFmt[am];
					if (opcode_table[op].modes & AMM_FLIPXY) {
						if (am == AMB_ZP_X)	fmt = "%s $%02x,y";
						else if (am == AMB_ABS_X) fmt = "%s $%04x,y";
					}
					if (opcode_table[op].modes & AMM_ZP_ABS)
						out.sprintf_append(fmt, opcode_table[op].instr, buf[1], (char)buf[2] + lst.address + si->start_address + 3);
					else if (opcode_table[op].modes & AMM_BRANCH)
						out.sprintf_append(fmt, opcode_table[op].instr, (char)buf[1] + lst.address + si->start_address + 2);
					else if (opcode_table[op].modes & AMM_BRANCH_L)
						out.sprintf_append(fmt, opcode_table[op].instr, (short)(buf[1] | (buf[2] << 8)) + lst.address + si->start_address + 3);
					else if (am == AMB_NON || am == AMB_ACC)
						out.sprintf_append(fmt, opcode_table[op].instr);
					else if (am == AMB_ABS || am == AMB_ABS_X || am == AMB_ABS_Y || am == AMB_REL || am == AMB_REL_X || am == AMB_REL_L)
						out.sprintf_append(fmt, opcode_table[op].instr, buf[1] | (buf[2] << 8));
					else if (am == AMB_ABS_L || am == AMB_ABS_L_X)
						out.sprintf_append(fmt, opcode_table[op].instr, buf[1] | (buf[2] << 8) | (buf[3] << 16));
					else if (am == AMB_BLK_MOV)
						out.sprintf_append(fmt, opcode_table[op].instr, buf[1], buf[2]);
					else if (am == AMB_IMM && lst.size==3)
						out.sprintf_append("%s #$%04x", opcode_table[op].instr, buf[1] | (buf[2]<<8));
					else
						out.sprintf_append(fmt, opcode_table[op].instr, buf[1]);
					if (aCPUs[cpu].timing) {
						cycles[cycles_depth].add(aCPUs[cpu].timing[*buf]);
						out.append_to(' ', 33);
						if (cycleCnt::sum_plus(aCPUs[cpu].timing[*buf])==1)
							out.sprintf_append("%d+", cycleCnt::get_base(aCPUs[cpu].timing[*buf]));
						else if (cycleCnt::sum_plus(aCPUs[cpu].timing[*buf]))
							out.sprintf_append("%d+%d", cycleCnt::get_base(aCPUs[cpu].timing[*buf]), cycleCnt::sum_plus(aCPUs[cpu].timing[*buf]));
						else
							out.sprintf_append("%d", cycleCnt::get_base(aCPUs[cpu].timing[*buf]));
					}
				}
			}

			out.append_to(' ', aCPUs[cpu].timing ? 40 : 33);
			strref line = lst.code.get_skipped(lst.line_offs).get_line();
			line.clip_trailing_whitespace();
			strown<128> line_fix(line);
			for (strl_t pos = 0; pos < line_fix.len(); ++pos) {
				if (line_fix[pos] == '\t')
					line_fix.exchange(pos, 1, pos & 1 ? strref(" ") : strref("  "));
			}
			out.append(line_fix.get_strref());

			fprintf(f, STRREF_FMT "\n", STRREF_ARG(out));
			prev_offs = lst.line_offs;
		}
	}
	if (opened)
		fclose(f);
	return true;
}

// Create a listing of all valid instructions and addressing modes
bool Asm::AllOpcodes(strref filename)
{
	FILE *f = stdout;
	bool opened = false;
	if (filename) {
		f = fopen(strown<512>(filename).c_str(), "w");
		if (!f)
			return false;
		opened = true;
	}
	for (int i = 0; i < opcode_count; i++) {
		unsigned int modes = opcode_table[i].modes;
		for (int a = 0; a < AMB_COUNT; a++) {
			if (modes & (1 << a)) {
				const char *fmt = aAddrModeFmt[a];
				fputs("\t", f);
				if (opcode_table[i].modes & AMM_BRANCH)
					fprintf(f, "%s *+%d", opcode_table[i].instr, 5);
				else if (a==AMB_ZP_ABS)
					fprintf(f, "%s $%02x,*+%d", opcode_table[i].instr, 0x23, 13);
				else {
					if (opcode_table[i].modes & AMM_FLIPXY) {
						if (a == AMB_ZP_X)	fmt = "%s $%02x,y";
						else if (a == AMB_ABS_X) fmt = "%s $%04x,y";
					}
					if (a == AMB_ABS_L || a == AMB_ABS_L_X) {
						if ((modes & ~(AMM_ABS_L|AMM_ABS_L_X)))
							fprintf(f, a==AMB_ABS_L ? "%s.l $%06x" : "%s.l $%06x,x", opcode_table[i].instr, 0x222120);
						else
							fprintf(f, fmt, opcode_table[i].instr, 0x222120);
					} else if (a == AMB_ABS || a == AMB_ABS_X || a == AMB_ABS_Y || a == AMB_REL || a == AMB_REL_X || a == AMB_REL_L)
						fprintf(f, fmt, opcode_table[i].instr, 0x2120);
					else if (a == AMB_IMM && (modes&(AMM_IMM_DBL_A|AMM_IMM_DBL_XY))) {
						fprintf(f, "%s.b #$%02x\n", opcode_table[i].instr, 0x21);
						fprintf(f, "\t%s.w #$%04x", opcode_table[i].instr, 0x2322);
					} else
						fprintf(f, fmt, opcode_table[i].instr, 0x21, 0x20, 0x1f);
					}
				fputs("\n", f);
			}
		}
	}
	if (opened)
		fclose(f);
	return true;
}

// create an instruction table (mnemonic hash lookup + directives)
void Asm::Assemble(strref source, strref filename, bool obj_target)
{
	SetCPU(cpu);

	StatusCode error = STATUS_OK;
	contextStack.push(filename, source, source);

	scope_address[scope_depth] = CurrSection().GetPC();
	while (contextStack.has_work()) {
		error = BuildSegment();
		if (contextStack.curr().complete()) {
			if (contextStack.curr().scoped_context && scope_depth)
				ExitScope();
			contextStack.pop();
		} else
			contextStack.curr().restart();
	}
	if (link_all_section)
		LinkAllToSection();
	if (error == STATUS_OK) {
		error = CheckLateEval();
		if (error > STATUS_XREF_DEPENDENT) {
			strown<512> errorText;
			errorText.copy("Error: ");
			errorText.append(aStatusStrings[error]);
			fwrite(errorText.get(), errorText.get_len(), 1, stderr);
		} else
			CheckLateEval(strref(), -1, true);	// output any missing xref's

		if (!obj_target) {
			for (std::vector<LateEval>::iterator i = lateEval.begin(); i!=lateEval.end(); ++i) {
				strown<512> errorText;
				int line = i->source_file.count_lines(i->expression);
				errorText.sprintf("Error (%d): ", line+1);
				errorText.append("Failed to evaluate label \"");
				errorText.append(i->expression);
				if (line>=0) {
					errorText.append("\" : \"");
					errorText.append(i->source_file.get_line(line).get_trimmed_ws());
				}
				errorText.append("\"\n");
				fwrite(errorText.get(), errorText.get_len(), 1, stderr);
			}
		}
	}
}



//
//
// OBJECT FILE HANDLING
//
//

struct ObjFileHeader {
	short id;	// 'x6'
	short sections;
	short relocs;
	short labels;
	short late_evals;
	short map_symbols;
	unsigned int stringdata;
	int bindata;
};

struct ObjFileStr {
	int offs;				// offset into string table
};

struct ObjFileSection {
	enum SectionFlags {
		OFS_DUMMY,
		OFS_FIXED,
		OFS_MERGED,
	};
	struct ObjFileStr name;
	struct ObjFileStr exp_app;
	int start_address;
	int output_size;		// assembled binary size
	int align_address;
	short relocs;
	SectionType type;
	char flags;
};

struct ObjFileReloc {
	int base_value;
	int section_offset;
	short target_section;
	char bytes;
	char shift;
};

struct ObjFileLabel {
	enum LabelFlags {
		OFL_EVAL = (1<<15),			// Evaluated (may still be relative)
		OFL_ADDR = (1<<14),			// Address or Assign
		OFL_CNST = (1<<13),			// Constant
		OFL_XDEF = OFL_CNST-1		// External (index into file array)
	};
	struct ObjFileStr name;
	int value;
	int flags;				// 1<<(LabelFlags)
	short section;			// -1 if resolved, file section # if section rel
	short mapIndex;			// -1 if resolved, index into map if relative
};

struct ObjFileLateEval {
	struct ObjFileStr label;
	struct ObjFileStr expression;
	int address;			// PC relative to section or fixed
	int target;				// offset into section memory
	short section;			// section to target
	short rept;				// value of rept for this late eval
	short scope;			// PC start of scope
	short type;				// label, byte, branch, word (LateEval::Type)
};

struct ObjFileMapSymbol {
	struct ObjFileStr name;	// symbol name
	int value;
	short section;
	bool local;				// local labels are probably needed
};

// Simple string pool, converts strref strings to zero terminated strings and returns the offset to the string in the pool.
static int _AddStrPool(const strref str, pairArray<unsigned int, int> *pLookup, char **strPool, unsigned int &strPoolSize, unsigned int &strPoolCap)
{
	if (!str.get() || !str.get_len())
		return -1;	// empty string

	unsigned int hash = str.fnv1a();
	unsigned int index = FindLabelIndex(hash, pLookup->getKeys(), pLookup->count());
	if (index<pLookup->count() && str.same_str_case(*strPool + pLookup->getValue(index)))
		return pLookup->getValue(index);

	int strOffs = strPoolSize;
	if ((strOffs + str.get_len() + 1) > strPoolCap) {
		strPoolCap = strOffs + str.get_len() + 4096;
		char *strPoolGrow = (char*)malloc(strPoolCap);
		if (strPoolGrow) {
			if (*strPool) {
				memcpy(strPoolGrow, *strPool, strPoolSize);
				free(*strPool);
			}
			*strPool = strPoolGrow;
		} else
			return -1;
	}

	if (*strPool) {
		char *dest = *strPool + strPoolSize;
		memcpy(dest, str.get(), str.get_len());
		dest[str.get_len()] = 0;
		strPoolSize += str.get_len()+1;
		pLookup->insert(index, hash);
		pLookup->getValues()[index] = strOffs;
	}
	return strOffs;
}

StatusCode Asm::WriteObjectFile(strref filename)
{
	if (FILE *f = fopen(strown<512>(filename).c_str(), "wb")) {
		struct ObjFileHeader hdr = { 0 };
		hdr.id = 0x7836;
		hdr.sections = (short)allSections.size();
		hdr.relocs = 0;
		hdr.bindata = 0;
		for (std::vector<Section>::iterator s = allSections.begin(); s!=allSections.end(); ++s) {
			if (s->pRelocs)
				hdr.relocs += short(s->pRelocs->size());
			hdr.bindata += (int)s->size();
		}
		hdr.late_evals = (short)lateEval.size();
		hdr.map_symbols = (short)map.size();
		hdr.stringdata = 0;

		// labels don't include XREF labels
		hdr.labels = 0;
		for (unsigned int l = 0; l<labels.count(); l++) {
			if (!labels.getValue(l).reference)
				hdr.labels++;
		}


		// include space for external protected labels
		for (std::vector<ExtLabels>::iterator el = externals.begin(); el != externals.end(); ++el)
			hdr.labels += el->labels.count();

		char *stringPool = nullptr;
		unsigned int stringPoolCap = 0;
		pairArray<unsigned int, int> stringArray;
		stringArray.reserve(hdr.labels * 2 + hdr.sections + hdr.late_evals*2);

		struct ObjFileSection *aSects = hdr.sections ? (struct ObjFileSection*)calloc(hdr.sections, sizeof(struct ObjFileSection)) : nullptr;
		struct ObjFileReloc *aRelocs = hdr.relocs ? (struct ObjFileReloc*)calloc(hdr.relocs, sizeof(struct ObjFileReloc)) : nullptr;
		struct ObjFileLabel *aLabels = hdr.labels ? (struct ObjFileLabel*)calloc(hdr.labels, sizeof(struct ObjFileLabel)) : nullptr;
		struct ObjFileLateEval *aLateEvals = hdr.late_evals ? (struct ObjFileLateEval*)calloc(hdr.late_evals, sizeof(struct ObjFileLateEval)) : nullptr;
		struct ObjFileMapSymbol *aMapSyms = hdr.map_symbols ? (struct ObjFileMapSymbol*)calloc(hdr.map_symbols, sizeof(struct ObjFileMapSymbol)) : nullptr;
		int sect = 0, reloc = 0, labs = 0, late = 0, map_sym = 0;

		// write out sections and relocs
		if (hdr.sections) {
			for (std::vector<Section>::iterator si = allSections.begin(); si!=allSections.end(); ++si) {
				struct ObjFileSection &s = aSects[sect++];
				s.name.offs = _AddStrPool(si->name, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
				s.exp_app.offs = _AddStrPool(si->export_append, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
				s.output_size = (short)si->size();
				s.align_address = si->align_address;
				s.relocs = si->pRelocs ? (short)(si->pRelocs->size()) : 0;
				s.start_address = si->start_address;
				s.type = si->type;
				s.flags =
					(si->IsDummySection() ? (1 << ObjFileSection::OFS_DUMMY) : 0) |
					(si->IsMergedSection() ? (1 << ObjFileSection::OFS_MERGED) : 0) |
					(si->address_assigned ? (1 << ObjFileSection::OFS_FIXED) : 0);
				if (si->pRelocs && si->pRelocs->size() && aRelocs) {
					for (relocList::iterator ri = si->pRelocs->begin(); ri!=si->pRelocs->end(); ++ri) {
						struct ObjFileReloc &r = aRelocs[reloc++];
						r.base_value = ri->base_value;
						r.section_offset = ri->section_offset;
						r.target_section = ri->target_section;
						r.bytes = ri->bytes;
						r.shift = ri->shift;
					}
				}
			}
		}

		// write out labels
		if (hdr.labels) {
			for (unsigned int li = 0; li<labels.count(); li++) {
				Label &lo = labels.getValue(li);
				if (!lo.reference) {
					struct ObjFileLabel &l = aLabels[labs++];
					l.name.offs = _AddStrPool(lo.label_name, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
					l.value = lo.value;
					l.section = lo.section;
					l.mapIndex = lo.mapIndex;
					l.flags =
						(lo.constant ? ObjFileLabel::OFL_CNST : 0) |
						(lo.pc_relative ? ObjFileLabel::OFL_ADDR : 0) |
						(lo.evaluated ? ObjFileLabel::OFL_EVAL : 0) |
						(lo.external ? ObjFileLabel::OFL_XDEF : 0);
				}
			}
		}

		// protected labels included from other object files
		if (hdr.labels) {
			int file_index = 1;
			for (std::vector<ExtLabels>::iterator el = externals.begin(); el != externals.end(); ++el) {
				for (unsigned int li = 0; li < el->labels.count(); ++li) {
					Label &lo = el->labels.getValue(li);
					struct ObjFileLabel &l = aLabels[labs++];
					l.name.offs = _AddStrPool(lo.label_name, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
					l.value = lo.value;
					l.section = lo.section;
					l.mapIndex = lo.mapIndex;
					l.flags =
						(lo.constant ? ObjFileLabel::OFL_CNST : 0) |
						(lo.pc_relative ? ObjFileLabel::OFL_ADDR : 0) |
						(lo.evaluated ? ObjFileLabel::OFL_EVAL : 0) |
						file_index;
				}
				file_index++;
			}
		}

		// write out late evals
		if (aLateEvals) {
			for (std::vector<LateEval>::iterator lei = lateEval.begin(); lei != lateEval.end(); ++lei) {
				struct ObjFileLateEval &le = aLateEvals[late++];
				le.label.offs = _AddStrPool(lei->label, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
				le.expression.offs = _AddStrPool(lei->expression, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
				le.section = lei->section;
				le.rept = lei->rept;
				le.target = (short)lei->target;
				le.address = lei->address;
				le.scope = lei->scope;
				le.type = lei->type;
			}
		}

		// write out map symbols
		if (aMapSyms) {
			for (MapSymbolArray::iterator mi = map.begin(); mi != map.end(); ++mi) {
				struct ObjFileMapSymbol &ms = aMapSyms[map_sym++];
				ms.name.offs = _AddStrPool(mi->name, &stringArray, &stringPool, hdr.stringdata, stringPoolCap);
				ms.value = mi->value;
				ms.local = mi->local;
				ms.section = mi->section;
			}
		}

		// write out the file
		fwrite(&hdr, sizeof(hdr), 1, f);
		fwrite(aSects, sizeof(aSects[0]), sect, f);
		fwrite(aRelocs, sizeof(aRelocs[0]), reloc, f);
		fwrite(aLabels, sizeof(aLabels[0]), labs, f);
		fwrite(aLateEvals, sizeof(aLateEvals[0]), late, f);
		fwrite(aMapSyms, sizeof(aMapSyms[0]), map_sym, f);
		fwrite(stringPool, hdr.stringdata, 1, f);
		for (std::vector<Section>::iterator si = allSections.begin(); si!=allSections.end(); ++si) {
			if (!si->IsDummySection() && !si->IsMergedSection() && si->size()!=0)
				fwrite(si->output, si->size(), 1, f);
		}

		// done with I/O
		fclose(f);

		if (stringPool)
			free(stringPool);
		if (aMapSyms)
			free(aMapSyms);
		if (aLateEvals)
			free(aLateEvals);
		if (aLabels)
			free(aLabels);
		if (aRelocs)
			free(aRelocs);
		if (aSects)
			free(aSects);
		stringArray.clear();

	}
	return STATUS_OK;
}

StatusCode Asm::ReadObjectFile(strref filename)
{
	size_t size;
	strown<512> file;
	file.copy(filename); // Merlin mostly uses extension-less files, append .x65 as a default
	if ((syntax==SYNTAX_MERLIN && !file.has_suffix(".x65")) || filename.find('.')<0)
		file.append(".x65");
	int file_index = (int)externals.size();
	if (char *data = LoadBinary(file.get_strref(), size)) {
		struct ObjFileHeader &hdr = *(struct ObjFileHeader*)data;
		size_t sum = sizeof(hdr) + hdr.sections*sizeof(struct ObjFileSection) +
			hdr.relocs * sizeof(struct ObjFileReloc) + hdr.labels * sizeof(struct ObjFileLabel) +
			hdr.late_evals * sizeof(struct ObjFileLateEval) +
			hdr.map_symbols * sizeof(struct ObjFileMapSymbol) + hdr.stringdata + hdr.bindata;
		if (hdr.id == 0x7836 && sum == size) {
			struct ObjFileSection *aSect = (struct ObjFileSection*)(&hdr + 1);
			struct ObjFileReloc *aReloc = (struct ObjFileReloc*)(aSect + hdr.sections);
			struct ObjFileLabel *aLabels = (struct ObjFileLabel*)(aReloc + hdr.relocs);
			struct ObjFileLateEval *aLateEval = (struct ObjFileLateEval*)(aLabels + hdr.labels);
			struct ObjFileMapSymbol *aMapSyms = (struct ObjFileMapSymbol*)(aLateEval + hdr.late_evals);
			const char *str_orig = (const char*)(aMapSyms + hdr.map_symbols);
			const char *bin_data = str_orig + hdr.stringdata;

			char *str_pool = (char*)malloc(hdr.stringdata);
			memcpy(str_pool, str_orig, hdr.stringdata);
			loadedData.push_back(str_pool);

			int prevSection = SectionId();

			short *aSctRmp = (short*)malloc(hdr.sections * sizeof(short));

			// for now just append to existing assembler data

			// sections
			for (int si = 0; si < hdr.sections; si++) {
				short f = aSect[si].flags;
				aSctRmp[si] = (short)allSections.size();
				if (f & (1 << ObjFileSection::OFS_MERGED))
					continue;
				if (f & (1 << ObjFileSection::OFS_DUMMY)) {
					if (f&(1 << ObjFileSection::OFS_FIXED))
						DummySection(aSect[si].start_address);
					else
						DummySection();
				} else {
					if (f&(1 << ObjFileSection::OFS_FIXED))
						SetSection(aSect[si].name.offs>=0 ? strref(str_pool + aSect[si].name.offs) : strref(), aSect[si].start_address);
					else
						SetSection(aSect[si].name.offs >= 0 ? strref(str_pool + aSect[si].name.offs) : strref());
					CurrSection().export_append = aSect[si].exp_app.offs>=0 ? strref(str_pool + aSect[si].name.offs) : strref();
					CurrSection().align_address = aSect[si].align_address;
					CurrSection().address = CurrSection().start_address + aSect[si].output_size;
					CurrSection().type = aSect[si].type;
					if (aSect[si].output_size) {
						CurrSection().output = (unsigned char*)malloc(aSect[si].output_size);
						memcpy(CurrSection().output, bin_data, aSect[si].output_size);
						CurrSection().curr = CurrSection().output + aSect[si].output_size;
						CurrSection().output_capacity = aSect[si].output_size;

						bin_data += aSect[si].output_size;
					}
				}
			}

			for (int si = 0; si < hdr.sections; si++) {
				for (int r = 0; r < aSect[si].relocs; r++) {
					struct ObjFileReloc &rs = aReloc[r];
					allSections[aSctRmp[si]].AddReloc(rs.base_value, rs.section_offset, aSctRmp[rs.target_section], rs.bytes, rs.shift);
				}
			}

			for (int mi = 0; mi < hdr.map_symbols; mi++) {
				struct ObjFileMapSymbol &m = aMapSyms[mi];
				if (map.size() == map.capacity())
					map.reserve(map.size() + 256);
				MapSymbol sym;
				sym.name = m.name.offs>=0 ? strref(str_pool + m.name.offs) : strref();
				sym.section = m.section >=0 ? aSctRmp[m.section] : m.section;
				sym.value = m.value;
				sym.local = m.local;
				map.push_back(sym);
			}

			for (int li = 0; li < hdr.labels; li++) {
				struct ObjFileLabel &l = aLabels[li];
				strref name = l.name.offs >= 0 ? strref(str_pool + l.name.offs) : strref();
				Label *lbl = GetLabel(name);
				short f = l.flags;
				int external = f & ObjFileLabel::OFL_XDEF;
				if (external == ObjFileLabel::OFL_XDEF) {
					if (!lbl)
						lbl = AddLabel(name.fnv1a());	// insert shared label
					else if (!lbl->reference)
						continue;
				} else {								// insert protected label
					while ((file_index + external) >= (int)externals.size()) {
						if (externals.size() == externals.capacity())
							externals.reserve(externals.size() + 32);
						externals.push_back(ExtLabels());
					}
					unsigned int hash = name.fnv1a();
					unsigned int index = FindLabelIndex(hash, externals[file_index].labels.getKeys(), externals[file_index].labels.count());
					externals[file_index].labels.insert(index, hash);
					lbl = externals[file_index].labels.getValues() + index;
				}
				lbl->label_name = name;
				lbl->pool_name.clear();
				lbl->value = l.value;
				lbl->section = l.section >= 0 ? aSctRmp[l.section] : l.section;
				lbl->mapIndex = l.mapIndex >= 0 ? (l.mapIndex + (int)map.size()) : -1;
				lbl->evaluated = !!(f & ObjFileLabel::OFL_EVAL);
				lbl->pc_relative = !!(f & ObjFileLabel::OFL_ADDR);
				lbl->constant = !!(f & ObjFileLabel::OFL_CNST);
				lbl->external = external == ObjFileLabel::OFL_XDEF;
				lbl->reference = false;
			}

			if (file_index==(int)externals.size())
				file_index = -1;	// no protected labels => don't track as separate file

			for (int li = 0; li < hdr.late_evals; ++li) {
				struct ObjFileLateEval &le = aLateEval[li];
				strref name = le.label.offs >= 0 ? strref(str_pool + le.label.offs) : strref();
				Label *pLabel = GetLabel(name);
				if (pLabel) {
					if (pLabel->evaluated) {
						AddLateEval(name, le.address, le.scope, strref(str_pool + le.expression.offs), (LateEval::Type)le.type);
						LateEval &last = lateEval[lateEval.size()-1];
						last.section = le.section >= 0 ? aSctRmp[le.section] : le.section;
						last.rept = le.rept;
						last.source_file = strref();
						last.file_ref = file_index;
					}
				} else {
					AddLateEval(le.target, le.address, le.scope, strref(str_pool + le.expression.offs), strref(), (LateEval::Type)le.type);
					LateEval &last = lateEval[lateEval.size()-1];
					last.section = le.section >= 0 ? aSctRmp[le.section] : le.section;
					last.rept = le.rept;
					last.file_ref = file_index;
				}
			}

			free(aSctRmp);

			// restore previous section
			current_section = &allSections[prevSection];
		} else
			return ERROR_NOT_AN_X65_OBJECT_FILE;

	}
	return STATUS_OK;
}

int main(int argc, char **argv)
{
	const strref listing("lst");
	const strref allinstr("opcodes");
	const strref endmacro("endm");
	const strref cpu("cpu");
	const strref acc("acc");
	const strref xy("xy");
	int return_value = 0;
	bool load_header = true;
	bool size_header = false;
	bool info = false;
	bool gen_allinstr = false;
	Asm assembler;

	const char *source_filename = nullptr, *obj_out_file = nullptr;
	const char *binary_out_name = nullptr;
	const char *sym_file = nullptr, *vs_file = nullptr;
	strref list_file, allinstr_file;
	for (int a = 1; a<argc; a++) {
		strref arg(argv[a]);
		if (arg.get_first()=='-') {
			++arg;
			if (arg.get_first()=='i')
				assembler.AddIncludeFolder(arg+1);
			else if (arg.same_str("merlin"))
				assembler.syntax = SYNTAX_MERLIN;
			else if (arg.get_first()=='D' || arg.get_first()=='d') {
				++arg;
				if (arg.find('=')>0)
					assembler.AssignLabel(arg.before('='), arg.after('='));
				else
					assembler.AssignLabel(arg, "1");
			} else if (arg.same_str("c64")) {
				load_header = true;
				size_header = false;
			} else if (arg.same_str("a2b")) {
				load_header = true;
				size_header = true;
			} else if (arg.same_str("bin")) {
				load_header = false;
				size_header = false;
			} else if (arg.same_str("sect"))
				info = true;
			else if (arg.same_str(endmacro))
				assembler.end_macro_directive = true;
			else if (arg.has_prefix(listing) && (arg.get_len() == listing.get_len() || arg[listing.get_len()] == '=')) {
				assembler.list_assembly = true;
				list_file = arg.after('=');
			} else if (arg.has_prefix(allinstr) && (arg.get_len() == allinstr.get_len() || arg[allinstr.get_len()] == '=')) {
				gen_allinstr = true;
				allinstr_file = arg.after('=');
			} else if (arg.has_prefix(acc) && arg[acc.get_len()] == '=') {
				assembler.accumulator_16bit = arg.after('=').atoi() == 16;
			} else if (arg.has_prefix(xy) && arg[xy.get_len()] == '=') {
				assembler.index_reg_16bit = arg.after('=').atoi() == 16;
			} else if (arg.has_prefix(cpu) && (arg.get_len() == cpu.get_len() || arg[cpu.get_len()] == '=')) {
				arg.split_token_trim('=');
				bool found = false;
				for (int c = 0; c<nCPUs; c++) {
					if (arg) {
						if (arg.same_str(aCPUs[c].name)) {
							assembler.SetCPU((CPUIndex)c);
							found = true;
							break;
						}
					} else
						printf("%s\n", aCPUs[c].name);
				}
				if (!found && arg) {
					printf("ERROR: UNKNOWN CPU " STRREF_FMT "\n", STRREF_ARG(arg));
					return 1;
				}
				if (!arg)
					return 0;
			} else if (arg.same_str("sym") && (a + 1) < argc)
				sym_file = argv[++a];
			else if (arg.same_str("obj") && (a + 1) < argc)
				obj_out_file = argv[++a];
			else if (arg.same_str("vice") && (a + 1) < argc)
				vs_file = argv[++a];
		} else if (!source_filename)
			source_filename = arg.get();
		else if (!binary_out_name)
			binary_out_name = arg.get();
	}

	if (gen_allinstr) {
		assembler.AllOpcodes(allinstr_file);
	} else if (!source_filename) {
		puts("Usage:\n"
			 " x65 filename.s code.prg [options]\n"
			 "  * -i(path) : Add include path\n"
			 "  * -D(label)[=value] : Define a label with an optional value (otherwise defined as 1)\n"
			 "  * -cpu=6502/65c02/65c02wdc/65816: assemble with opcodes for a different cpu\n"
			 "  * -acc=8/16: set the accumulator mode for 65816 at start, default is 8 bits\n"
			 "  * -xy=8/16: set the index register mode for 65816 at start, default is 8 bits\n"
			 "  * -obj (file.x65) : generate object file for later linking\n"
			 "  * -bin : Raw binary\n"
			 "  * -c64 : Include load address(default)\n"
			 "  * -a2b : Apple II Dos 3.3 Binary\n"
			 "  * -sym (file.sym) : symbol file\n"
			 "  * -lst / -lst = (file.lst) : generate disassembly text from result(file or stdout)\n"
			 "  * -opcodes / -opcodes = (file.s) : dump all available opcodes(file or stdout)\n"
			 "  * -sect: display sections loaded and built\n"
			 "  * -vice (file.vs) : export a vice symbol file\n"
			 "  * -merlin: use Merlin syntax\n"
			 "  * -endm : macros end with endm or endmacro instead of scoped('{' - '}')\n");
		return 0;
	}

	// Load source
	if (source_filename) {
		size_t size = 0;
		strref srcname(source_filename);

		assembler.export_base_name = strref(binary_out_name).after_last_or_full('/', '\\').before_or_full('.');

		if (char *buffer = assembler.LoadText(srcname, size)) {
			// if source_filename contains a path add that as a search path for include files
			assembler.AddIncludeFolder(srcname.before_last('/', '\\'));

			assembler.Assemble(strref(buffer, strl_t(size)), srcname, obj_out_file != nullptr);

			if (assembler.error_encountered)
				return_value = 1;
			else {
				// export object file
				if (obj_out_file)
					assembler.WriteObjectFile(obj_out_file);

				// if exporting binary, complete the build
				if (binary_out_name && !srcname.same_str(binary_out_name)) {
					strref binout(binary_out_name);
					strref ext = binout.after_last('.');
					if (ext)
						binout.clip(ext.get_len() + 1);
					strref aAppendNames[MAX_EXPORT_FILES];
					StatusCode err = assembler.LinkZP();	// link zero page sections
					if (err > FIRST_ERROR) {
						assembler.PrintError(strref(), err);
						return_value = 1;
					}
					int numExportFiles = assembler.GetExportNames(aAppendNames, MAX_EXPORT_FILES);
					for (int e = 0; e < numExportFiles; e++) {
						strown<512> file(binout);
						file.append(aAppendNames[e]);
						file.append('.');
						file.append(ext);
						int size;
						int addr;
						if (unsigned char *buf = assembler.BuildExport(aAppendNames[e], size, addr)) {
							if (FILE *f = fopen(file.c_str(), "wb")) {
								if (load_header) {
									char load_addr[2] = { (char)addr, (char)(addr >> 8) };
									fwrite(load_addr, 2, 1, f);
								}
								if (size_header) {
									char byte_size[2] = { (char)size, (char)(size >> 8) };
									fwrite(byte_size, 2, 1, f);
								}
								fwrite(buf, size, 1, f);
								fclose(f);
							}
							free(buf);
						}
					}
				}

				// print encountered sections info
				if (info) {
					printf("SECTIONS SUMMARY\n================\n");
					for (size_t i = 0; i < assembler.allSections.size(); ++i) {
						Section &s = assembler.allSections[i];
						if (s.address > s.start_address) {
							printf("Section %d: \"" STRREF_FMT "\" Dummy: %s Relative: %s Merged: %s Start: 0x%04x End: 0x%04x\n",
								   (int)i, STRREF_ARG(s.name), s.dummySection ? "yes" : "no",
								   s.IsRelativeSection() ? "yes" : "no", s.IsMergedSection() ? "yes" : "no", s.start_address, s.address);
							if (s.pRelocs) {
								for (relocList::iterator i = s.pRelocs->begin(); i != s.pRelocs->end(); ++i)
									printf("\tReloc value $%x at offs $%x section %d\n", i->base_value, i->section_offset, i->target_section);
							}
						}
					}
				}

				// listing after export since addresses are now resolved
				if (assembler.list_assembly)
					assembler.List(list_file);

				// export .sym file
				if (sym_file && !srcname.same_str(sym_file) && !assembler.map.empty()) {
					if (FILE *f = fopen(sym_file, "w")) {
						bool wasLocal = false;
						for (MapSymbolArray::iterator i = assembler.map.begin(); i!=assembler.map.end(); ++i) {
							unsigned int value = (unsigned int)i->value;
							int section = i->section;
							while (section >= 0 && section < (int)assembler.allSections.size()) {
								if (assembler.allSections[section].IsMergedSection()) {
									value += assembler.allSections[section].merged_offset;
									section = assembler.allSections[section].merged_section;
								} else {
									value += assembler.allSections[section].start_address;
									break;
								}
							}
							fprintf(f, "%s.label " STRREF_FMT " = $%04x", wasLocal==i->local ? "\n" :
									(i->local ? " {\n" : "\n}\n"), STRREF_ARG(i->name), value);
							wasLocal = i->local;
						}
						fputs(wasLocal ? "\n}\n" : "\n", f);
						fclose(f);
					}
				}

				// export vice label file
				if (vs_file && !srcname.same_str(vs_file) && !assembler.map.empty()) {
					if (FILE *f = fopen(vs_file, "w")) {
						for (MapSymbolArray::iterator i = assembler.map.begin(); i!=assembler.map.end(); ++i) {
							unsigned int value = (unsigned int)i->value;
							int section = i->section;
							while (section >= 0 && section < (int)assembler.allSections.size()) {
								if (assembler.allSections[section].IsMergedSection()) {
									value += assembler.allSections[section].merged_offset;
									section = assembler.allSections[section].merged_section;
								} else {
									value += assembler.allSections[section].start_address;
									break;
								}
							}
							fprintf(f, "al $%04x %s" STRREF_FMT "\n", value, i->name[0]=='.' ? "" : ".",
									STRREF_ARG(i->name));
						}
						fclose(f);
					}
				}
			}
			// free some memory
			assembler.Cleanup();
		}
	}
	return return_value;
}
