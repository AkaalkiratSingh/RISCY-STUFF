# RISCY201 Assembler — Documentation
This document describes everything in the `assembler/` folder of RISCY-STUFF: the preprocessor (macros), the two-pass assembler, the disassembler, the two command-line tools, and the C++ library that backs them.

### Table of contents
1. Overview
2. Folder layout, build and dependencies
3. Command-line tools
4. Source language reference
5. Instruction reference
6. Literals (`.literals:`)
7. Macros and the preprocessor
8. Diagnostics: errors, warnings and line numbers
9. Machine code and file formats
10. How the assembler works internally
11. The disassembler
12. C++ library API
13. Integration with the simulator
14. Known limitations and quirks
15. Tests
16. Worked examples
17. Appendix A: complete message catalogue

---

## 1. Overview
The toolchain turns a text program written for the `risc201` processor into a binary image, and turns an image back into text.

```text
                 ┌────────────────────────────────────────────────────┐
  program.asm ─► │ Preprocessor   .macro / .endm, @local labels,      │
  (text lines)   │                built-in PUSH / POP, line map       │
                 └───────────────────────┬────────────────────────────┘
                                         │ expanded lines + origin map
                 ┌───────────────────────▼────────────────────────────┐
                 │ Assembler                                          │
                 │   Pass 1: find .main, insert "JMP .main", parse    │
                 │           lines, build symbol table, count bytes   │
                 │   Pass 2: resolve operands, range-check, build     │
                 │           Instruction structs                      │
                 │   Encode: Instruction -> 32-bit word               │
                 └───────────────────────┬────────────────────────────┘
                                         │ 32-bit words (+ literal section)
                       ┌─────────────────▼───────────────┐
                       │ program.bin (big-endian bytes)  │
                       │ program.hex (one word per line) │
                       └─────────────────┬───────────────┘
                                         │
                 ┌───────────────────────▼────────────────────────────┐
                 │ Disassembler   decode words -> assembler source,   │
                 │                .main: restored, optional generated │
                 │                labels, literal listing             │
                 └────────────────────────────────────────────────────┘
```

**Key facts at a glance:**
| Topic | Rule |
| :--- | :--- |
| **Entry point** | The file must contain exactly one `.main:` label. The assembler inserts `JMP .main` as the very first instruction. |
| **Instruction size** | Always 32 bits. Code addresses inside the assembler are byte addresses (multiples of 4). |
| **Registers** | `R0`–`R15`. `SP` = `R15`, `RA` = `R14`. `R0` is always zero and cannot be a destination. |
| **Comments** | `;` or `#` to end of line. |
| **Case** | Mnemonics, registers and `.macro`/`.endm` are case-insensitive. Labels, literal names, macro parameters, `.main:` and `.literals:` are case-sensitive. Macro names are case-insensitive. |
| **Immediates** | 18-bit signed for register/immediate instructions; 27-bit signed byte displacement for jumps. Out-of-range values are truncated with a warning. |
| **Constants** | `.literals:` section of named 32-bit floats, read with `LOAD reg, name`. Literals are data: they cannot be jump targets. |
| **Macros** | `.macro NAME p1, p2 …` / `.endm`, fixed arity, `@` local labels, built-in `PUSH` and `POP`. |
| **Output** | `.bin` (big-endian) or `.hex` (8 hex digits per line). The extension of `-o` chooses the format. |
| **Disassembly** | `risc201-disasm` writes assembler source (starting at `.main:`) that assembles back to the identical image, for images made by `risc201-asm` (11.5). |

---

## 2. Folder layout, build and dependencies

```text
assembler/
├── CMakeLists.txt
├── include/assembler/
│   ├── assembler.h        Assembler class, ParsedLine, AssemblyError, hex/bin readers
│   ├── disassembler.h     Disassembler class
│   ├── preprocessor.h     Preprocessor class, PreprocessResult, PreprocessError
│   └── op_table.h         OperandKind, OpInfo, opTable(), opcodeToMnemonic(), writesDestination()
└── src/
    ├── assembler.cpp      Pass 1, Pass 2, encoding, file I/O
    ├── disassembler.cpp   Decoding, source generation, label synthesis, literal listing
    ├── preprocessor.cpp   Macro collection and expansion
    ├── main_asm.cpp       risc201-asm command-line tool
    └── main_disasm.cpp    risc201-disasm command-line tool
```

### 2.1 CMake targets
| Target | Kind | Sources / purpose |
| :--- | :--- | :--- |
| `assembler_lib` | static library | `assembler.cpp`, `disassembler.cpp`, `preprocessor.cpp`. Public include dir is `include/`. Links `isa_spec` and `utils_lib` publicly. |
| `risc201-asm` | executable | `main_asm.cpp`, links `assembler_lib` |
| `risc201-disasm` | executable | `main_disasm.cpp`, links `assembler_lib` |

**Build** (from the repository root):
```bash
cmake -S . -B build
cmake --build build
# tools end up in build/assembler/risc201-asm and build/assembler/risc201-disasm
```

### 2.2 Shared code the assembler depends on
| File | What the assembler uses from it |
| :--- | :--- |
| `isa_spec/isa201.h` | `Opcode`, `Register`, `InstrFormat`, `Instruction`, `kLiteralsMarker` (`0xFFFFFFFF`) |
| `utils/include/utils/utils.h` | `trim`, `toUpper`, `toLower`, `stripCommentandTrim`, `extractLabel`, `floatToBits`, `bitsToFloat`, `hasSuffix`, `parseRegisterAlias`, `formatInstruction` |

The text helpers in `utils` are used by all three components, so line handling is identical everywhere:
-   `trim(s)`: Removes leading/trailing space, tab, `\r`, `\n`.
-   `stripCommentandTrim(s)`: Cuts the line at the first `;` or `#`, then trims. (Note the spelling: lower-case `and`.)
-   `extractLabel(line, label)`: If `line` contains a `:` and the text before it is non-empty and has no space/tab, stores that text in `label`, removes `label:` and following spaces from `line`, returns `true`. Otherwise returns `false`.
-   `toUpper(s)`, `toLower(s)`: ASCII case conversion on a copy.
-   `floatToBits`, `bitsToFloat`: Bit-exact float ⇄ uint32_t via `memcpy`.
-   `hasSuffix(path, suffix)`: Used to pick `.hex` vs binary by file extension.
-   `parseRegisterAlias(token, reg)`: Recognises the register aliases `SP` (→ `R15`) and `RA` (→ `R14`), in any case. The assembler's register parser and the preprocessor's "looks like a register" check both call it, so they always agree on what an alias is.
-   `formatInstruction(instr)`: The one instruction formatter: prints an `Instruction` exactly as the assembler accepts it. `Disassembler::formatInstruction` calls it.

---

## 3. Command-line tools

### 3.1 risc201-asm
```bash
risc201-asm <input.asm> [-o output.bin | output.hex]
```
| Argument | Meaning |
| :--- | :--- |
| `input.asm` | Source file. |
| `-o file` | Output file. The format follows the extension: names ending in `.hex` are written as hex text, anything else as raw binary. Default is `a.bin`. |

Rules for the argument list: exactly `risc201-asm file` or `risc201-asm file -o out`. Any other argument count, or a third argument other than `-o`, prints the usage line and exits with status 1.

**Processing steps:**
1.  Read the file into lines. Failure → `error: cannot open <file>`, exit 1.
2.  Run the preprocessor. If it reports any errors, print them all and exit 1 before the assembler runs.
3.  Run Pass 1 and Pass 2 on the expanded lines.
4.  Print all warnings, then, if there are errors, all errors, and exit 1. Warnings alone do not fail the build and the output file is still written.
5.  Encode every instruction, append the literal section (if any), and write the file.
6.  Print `Assembled N instruction(s) -> <file>`, where `N` is the number of instructions including the automatic `JMP .main`, but not counting literals.

Diagnostics are printed to `stderr` in the form `file:line: error: message` or `file:line: warning: message`. Line numbers always refer to the original file even when the offending line came from a macro. See section 8.

**Exit status:** 0 success, 1 for usage errors, I/O errors, or any assembly error.

### 3.2 risc201-disasm
```bash
risc201-disasm <input.bin | input.hex> [-o output.asm] [-l]
```
| Argument | Meaning |
| :--- | :--- |
| `input` | Machine image. A name ending in `.hex` is parsed as hex text; anything else as raw big-endian binary. |
| `-o file` | Output text file. Default `a.asm`. |
| `-l` | Synthesise labels for jump/branch/call targets. |

Arguments may appear in any order; the first argument that is not `-o <x>` or `-l` is the input. A missing input prints the usage line and exits 1. Unreadable or unparsable input prints `error: cannot read or parse <file>` and exits 1.

For every word whose top five bits are not a known opcode, a warning is printed to `stderr` (`warning: word <index> (0x<hex>) has an unrecognized opcode, emitted as '; unknown opcode'`) and the line `; unknown opcode` is written in its place. After a successful run it prints `Disassembled N instruction(s) -> <file>`, where `N` is the number of decoded words before the literals marker. That count includes the automatic entry JMP, even though that instruction is not written to the text (see 11.2).

The output is assembler source: it starts at `.main:` and can be fed straight back to `risc201-asm` (11.5).

---

## 4. Source language reference

### 4.1 Lines, whitespace and comments
-   A program is a list of text lines. Each line may hold: nothing; a label; an instruction; a label followed by an instruction; a directive; or a macro call (optionally with a label).
-   `;` and `#` both start a comment that runs to the end of the line. Because `#` is a comment character, it cannot be used as an immediate prefix.
-   Leading and trailing spaces, tabs and carriage returns are ignored, so Windows (`\r\n`) files work.
-   Operands are separated by commas. Spaces around commas are ignored. Empty operands (for example `ADD R1,,R2`) are dropped by the assembler's operand splitter, which then usually produces an operand-count error.
-   The mnemonic is the first space/tab-delimited word after the optional label.

```asm
; full-line comment
.main:                      # label only
    add r1, R2, r3         ; lower-case mnemonic and registers are fine
loop: SUB R1, R1, 1        ; label and instruction on one line
```

### 4.2 Labels
A label is `name:` at the start of a line (after trimming). The text before the first `:` must be non-empty and contain no space or tab.

| Rule | Detail |
| :--- | :--- |
| **Valid names** | Anything that does not start with a digit or `-`. Letters, digits, `_`, `.`, `$`, `-` (not first) all work in practice. |
| **Invalid names** | `invalid label 'X': label names cannot start with a digit or '-'`. |
| **Case** | Case-sensitive: `Loop` and `loop` are different labels. |
| **Duplicates** | `duplicate label 'X'`. Labels and literal names share one namespace. |
| **Label alone** | A line with only a label is allowed; the label takes the address of the next emitted instruction. |
| **Same line** | `label: instruction` is allowed. |
| **Reserved** | `.main` and `.literals` are directives (see below). Names starting with `.` are rejected inside macro bodies. Names starting with `@` are macro-local labels and are only legal inside macro bodies. |

A label's value is the byte address of the next instruction (the location counter, which advances 4 per emitted word). Because the assembler inserts `JMP .main` at address 0, the first user instruction is at byte address 4.

### 4.3 Directives
| Directive | Meaning |
| :--- | :--- |
| `.main:` | Required entry point, exactly once. `.main` is a label (it can be a jump target) and may share a line with an instruction. Matching is case-sensitive: `.MAIN:` is not recognised. |
| `.literals:` | Starts the literal data section. Case-sensitive. See section 6. |
| `.macro NAME params…` / `.endm` | Defines a macro. Case-insensitive (`.MACRO` works). See section 7. |

### 4.4 Registers
| Spelling | Register | Notes |
| :--- | :--- | :--- |
| `R0` … `R15` (any case) | General registers | `R16` and above are invalid. |
| `SP` (any case) | `R15` | Stack pointer. Initialised to the top of data memory by the simulator. |
| `RA` (any case) | `R14` | Link register: `CALL` writes the return address here, `RET` jumps to it. |

-   The aliases are recognised by one shared helper (`parseRegisterAlias`, 2.2), which the preprocessor also uses, so a name the assembler reads as a register is never usable as a macro parameter (7.1).
-   The disassembler always prints `Rn`, never `SP`/`RA`.
-   **`R0` is the zero register.** The assembler rejects any instruction that writes `R0`: `'ADD' cannot write to R0 (R0 is always zero)`. This applies to every instruction for which `writesDestination()` is true: `ADD SUB MUL DIV REM AND OR XOR NOT SHL SHR SAR FADD FSUB FMUL FDIV MOV LOAD`. Reading `R0` is fine, `STORE R0, …` (store a zero) is fine, and `CMP`/`FCMP` (which have no destination) are fine.
-   The rule is enforced by the assembler only. A hand-built machine word that writes `R0` is not stopped by the CPU model. Macros are checked after expansion, so `INC R0` reports the error with `(in expansion of 'INC')`.

### 4.5 Numbers
| Form | Examples | Notes |
| :--- | :--- | :--- |
| **Decimal** | `5`, `-12`, `0` | Leading `-` allowed. |
| **Hexadecimal** | `0x1F`, `0XFF`, `-0x10` | `0x`/`0X` prefix, then at least one digit. |

Anything else (`5.0`, `1_000`, `0b101`, `'a'`, `$10`) is not a number.

Values are parsed into 32 bits, then range-checked against the field they will occupy:
| Field | Used by | Range | Out-of-range behaviour |
| :--- | :--- | :--- | :--- |
| 18-bit signed immediate | register/immediate instructions, memory offsets | −131072 … 131071 | Truncated to 18 bits (two's complement) + warning |
| 27-bit signed byte displacement | `JMP` `BEQ` `BGT` `CALL` | −67108864 … 67108863 | Truncated to 27 bits + warning |

Warning text: `value 200000 out of range for 18-bit field, truncated to -62144`. The operand token is shown as written; for resolved labels it appears as `label (resolved offset N)`.

### 4.6 Operand kinds
Every instruction has a pattern of operand kinds in `op_table.h`.

| Kind | Accepts | Encoded as |
| :--- | :--- | :--- |
| `REG` | A register | Next free register slot (`rd`, then `rs1`, then `rs2`) |
| `IMM` | A number | Immediate, `I=1` (defined but currently unused by any instruction) |
| `REG_OR_IMM` | A register or a number | Register → next register slot, `I=0`. Number → immediate, `I=1`. |
| `MEM` | `offset[reg]` or a literal name | `rs1 = reg`, `imm = offset`, `I=1`. A literal name becomes `rs1 = R0`, `imm = literal index`. |
| `LABEL` | A label name or a raw byte offset | 27-bit displacement. See 4.8. |
| `EMPTY` | Nothing is written | Skips one register slot (see below). |

**`offset[reg]` rules for `MEM`:**
-   `offset` is a decimal or hex number and is required (`[R2]` alone is rejected; write `0[R2]`). It may be negative.
-   `reg` is any register spelling, including `SP`/`RA`.
-   No spaces inside the brackets or between the number and `[`: `4[ R2 ]` and `4 [R2]` are malformed.
-   Nothing may follow the `]`.

Malformed operands report `malformed memory operand 'X' (expected imm[reg] or a literal name)`.

**Register slots and `EMPTY`.** An instruction word has three register fields, `rd`, `rs1`, `rs2`. Source operands fill them left to right; an `EMPTY` entry in the pattern skips a slot without consuming a source operand. That is why `MOV R1, R2` puts `R2` in `rs2` (pattern `REG`, `EMPTY`, `REG_OR_IMM`) and `CMP R1, R2` puts `R1` in `rs1` and `R2` in `rs2` (pattern `EMPTY`, `REG`, `REG_OR_IMM`). Immediates never use a register slot.

### 4.7 Operand count
The number of written operands must equal the number of non-`EMPTY` entries in the pattern: `'ADD' expects 3 operand(s), got 2`. `NOP`, `RET` and `HALT` take none (`'RET' expects 0 operand(s), got 1`).

### 4.8 Jump targets
`JMP`, `BEQ`, `BGT` and `CALL` take one operand, either:
-   **A label** (code label, `.main`, or a macro-expanded label). The assembler stores<br>`displacement = target_byte_address − (address_of_this_instruction + 4)`.<br>The displacement is in bytes, relative to the next instruction.
-   **A raw byte offset** (a number, decimal or hex, possibly negative). It is stored as given. `JMP 0` jumps to the next instruction. An offset that is not a multiple of 4 produces a warning (`jump offset 3 is not a multiple of 4 (instructions are 4 bytes)`) but is still encoded.

*Disambiguation:* a token that parses as a number is a raw offset. A token that starts with a digit or `-` but is not a valid number is `invalid jump offset 'X'`. Anything else is looked up as a label; an unknown name is `undefined label 'X'`.

**Literals are not jump targets.** A name that belongs to the literal section is data, not code. Naming a literal, or `.literals` itself, as a jump target is an error: `cannot jump to 'pi': it is in the literal section (data)`. The check runs after the undefined-label check, so a program with no `.literals:` section reports `undefined label '.literals'` instead.

---

## 5. Instruction reference
Opcode numbers come from `isa_spec/isa201.h`. The five opcode bits sit at the top of every word. Opcodes `0x1B`–`0x1E` are unassigned; `0x1B`/`0x1C` are reserved for planned `FTOI`/`ITOF` and are not accepted by the assembler.

| Mnemonic | Opcode | Format | Operands (as written) | Writes `rd`? | Summary |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `NOP` | `0x00` | RI | none | no | Does nothing. |
| `MOV` | `0x01` | RI | `rd, reg\|imm` | yes | Copy a register or immediate into `rd`. |
| `ADD` | `0x02` | RI | `rd, rs1, reg\|imm` | yes | Integer add. |
| `SUB` | `0x03` | RI | `rd, rs1, reg\|imm` | yes | Integer subtract. |
| `MUL` | `0x04` | RI | `rd, rs1, reg\|imm` | yes | Integer multiply. |
| `DIV` | `0x05` | RI | `rd, rs1, reg\|imm` | yes | Integer divide (divide-by-zero raises an exception in the simulator). |
| `REM` | `0x06` | RI | `rd, rs1, reg\|imm` | yes | Integer remainder. |
| `AND` | `0x07` | RI | `rd, rs1, reg\|imm` | yes | Bitwise AND. |
| `OR`  | `0x08` | RI | `rd, rs1, reg\|imm` | yes | Bitwise OR. |
| `XOR` | `0x09` | RI | `rd, rs1, reg\|imm` | yes | Bitwise XOR. |
| `NOT` | `0x0A` | RI | `rd, reg\|imm` | yes | Bitwise NOT of the source. |
| `SHL` | `0x0B` | RI | `rd, rs1, reg\|imm` | yes | Shift left. |
| `SHR` | `0x0C` | RI | `rd, rs1, reg\|imm` | yes | Logical shift right. |
| `SAR` | `0x0D` | RI | `rd, rs1, reg\|imm` | yes | Arithmetic shift right. |
| `LOAD`| `0x0E` | RI | `rd, off[reg]` or `rd, literal` | yes | `rd = data_memory[reg + off]` (word index). |
| `STORE`|`0x0F` | RI | `src, off[reg]` | no | `data_memory[reg + off] = src`. The first operand lands in the `rd` field but is the source. |
| `CMP` | `0x10` | RI | `rs1, reg\|imm` | no | Subtract for flags only. |
| `JMP` | `0x11` | J  | `label\|offset` | no | Unconditional jump. |
| `BEQ` | `0x12` | J  | `label\|offset` | no | Jump if the zero flag is set. |
| `BGT` | `0x13` | J  | `label\|offset` | no | Jump if the greater-than flag is set. |
| `CALL`| `0x14` | J  | `label\|offset` | no | Call: writes the return address to R14, then jumps. |
| `RET` | `0x15` | RI | none | no | Jump to the address in R14. |
| `FADD`| `0x16` | RI | `rd, rs1, rs2` | yes | Float add on register bit patterns. Registers only. |
| `FSUB`| `0x17` | RI | `rd, rs1, rs2` | yes | Float subtract. Registers only. |
| `FMUL`| `0x18` | RI | `rd, rs1, rs2` | yes | Float multiply. Registers only. |
| `FDIV`| `0x19` | RI | `rd, rs1, rs2` | yes | Float divide. Registers only. |
| `FCMP`| `0x1A` | RI | `rs1, rs2` | no | Float compare, sets the flags. Registers only. |
| `HALT`| `0x1F` | RI | none | no | Stop fetching and drain the pipeline. |

*Notes:*
-   The "Summary" column is a one-line orientation only. The authoritative semantics (flag behaviour, exceptions, pipeline interaction) live in the simulator and in `docs/PROJECT_ARCHITECTURE.md`.
-   Floating-point instructions operate on the raw 32-bit pattern held in an integer register. `FADD R3, R1, R2` with `R1 = 0x3F800000` (`1.0f`) treats it as a float. Immediates are not allowed for the `F*` instructions (`invalid register '5'`).
-   `PUSH` and `POP` are not instructions; they are built-in macros (7.9).
-   There is no `FMOV`, no `LI`, and no pseudo-instructions other than the macros.

### 5.1 Where each operand lands in the instruction word
| Instruction | `rd` | `rs1` | `rs2` / `imm` | `I` |
| :--- | :--- | :--- | :--- | :--- |
| `ADD` … `SAR rd, rs1, x` | 1st operand | 2nd operand | 3rd operand (register in `rs2`, or immediate) | `0` or `1` |
| `MOV rd, x`, `NOT rd, x` | 1st operand | `R0` (unused) | the source (register in `rs2`, or immediate) | `0` or `1` |
| `FADD`/`FSUB`/`FMUL`/`FDIV rd, rs1, rs2`| 1st | 2nd | 3rd (in `rs2`) | `0` |
| `CMP rs1, x` | `R0` (unused) | 1st operand | 2nd (register in `rs2`, or immediate) | `0` or `1` |
| `FCMP rs1, rs2` | `R0` (unused) | 1st | 2nd (in `rs2`) | `0` |
| `LOAD rd, off[reg]` | 1st | `reg` | `off` | `1` |
| `LOAD rd, literal` | 1st | `R0` | literal index | `1` |
| `STORE src, off[reg]` | `src` | `reg` | `off` | `1` |
| `JMP`/`BEQ`/`BGT`/`CALL` | — | — | 27-bit displacement | — (J format) |
| `NOP`, `RET`, `HALT` | `R0` | `R0` | `0` | `0` |

---

## 6. Literals (`.literals:`)
Literals are named 32-bit floating-point constants kept in a data section after the code.

### 6.1 Syntax
```asm
.main:
    LOAD R1, five          ; R1 = bit pattern of 5.0f
    LOAD R2, half
    FADD R3, R1, R2
    HALT

.literals:
    five: 5.0f
    half: 0.5f
```
-   `.literals:` must be alone on its line (`unexpected text after '.literals:'` otherwise).
-   Every following non-blank line must be `name: value`. A line with no label gives `literal needs a name, expected 'name: 1.0f'`; a label with no value gives `literal 'name' has no value`.
-   Everything after `.literals:` is parsed as a literal, including lines that look like instructions. `NOP` there produces the "needs a name" error.
-   Only one `.literals:` is allowed (a second one is `duplicate label '.literals'`).
-   Instructions are not allowed after the section starts, and neither are `.macro` definitions (`'.macro' is not allowed after '.literals:' …`). Macro calls after `.literals:` are not expanded.

### 6.2 Value syntax
A value is a decimal floating-point number immediately followed by `f` or `F`, parsed with `std::stof`.
| Accepted | Rejected |
| :--- | :--- |
| `3.14f`, `-2.5f`, `.5f`, `3F`, `1e3f`, `0.0f` | `3.0` (no suffix), `inf`, `nanf` and anything that overflows to infinity/NaN, `1.0f junk` |

Rejected values give `invalid float literal 'X' (expected e.g. 3.14f)`. The stored word is the IEEE-754 single-precision bit pattern.

### 6.3 Using a literal
A literal name is accepted wherever a `MEM` operand is, which in practice means `LOAD`:
```asm
LOAD R1, half        ; same as LOAD R1, 1[R0]  if 'half' is the second literal
```
-   The assembler replaces the name with `rs1 = R0` and `imm = index of the literal` (0 for the first literal, 1 for the second, …). It does not use the byte address.
-   The literal pool is placed at the start of data memory by the simulator (section 13), so index `n` is data word `n`. This works only because `R0` is zero — another reason `R0` is protected.
-   Use `LOAD R1, 3[R2]` with a numeric offset if you need table-style access.
-   `STORE` accepts the same operand form syntactically, which would overwrite a literal word. Nothing prevents that: there is no separate protected address space.
-   Literal names share the label namespace. A literal and a code label with the same name give `duplicate label`.
-   A literal name cannot be a jump target. `JMP pi`, `BEQ pi`, `BGT pi` and `CALL pi` (and the pool's own label, `JMP .literals`) are all rejected with `cannot jump to 'pi': it is in the literal section (data)` (4.8).

### 6.4 Layout in the output
When at least one literal exists the output is: all instruction words, then the marker `0xFFFFFFFF`, then the literal words in source order. With no literals nothing is appended (no marker). See 9.3.

---

## 7. Macros and the preprocessor

```text
┌─────────────────────────────────────────────────────┐
│                 source_lines (text)                 │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│ Preprocessor::expand                                │
│ 1. loadBuiltins (PUSH, POP)                         │
│ 2. collect(): extract .macro / .endm, record lines  │
│ 3. expandLine(): substitute args, rename @ labels   │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│                  PreprocessResult                   │
│ (expanded lines, origin_line, origin_context, errs) │
└─────────────────────────────────────────────────────┘
```

The preprocessor runs before the assembler. It works on text lines only; it does not know instruction semantics.

### 7.1 Defining a macro
```asm
.macro ADD3 a, b, c
    ADD a, a, b
    ADD a, a, c
.endm
```
-   Form: `.macro NAME [param1, param2, …]` … `.endm`. `.endm` must be alone on its line.
-   Any number of parameters (zero is fine) up to 64.
-   Names are case-insensitive and stored upper-case: `add3`, `Add3` and `ADD3` call the same macro. They must be identifiers (letters, digits, `_`, not starting with a digit).
-   A macro name must not equal an instruction mnemonic and must not be built in (`PUSH`, `POP`).
-   Parameter names are identifiers, case-sensitive, unique within the macro, and must not look like a register: `R3`, `r15` and the aliases `SP` and `RA` (in any case) are all rejected (`parameter 'ra' looks like a register name`).
-   Blank lines and comments inside the body are discarded.
-   Macros cannot be defined inside macros, and cannot be defined after `.literals:`.
-   Definitions may appear anywhere in the file, including after their first use: all definitions are collected before any expansion starts. Definition lines are removed from the output.

### 7.2 Calling a macro
```asm
.main:
top: ADD3 R1, R2, R3       ; optional label on the call line
     add3 R4, R5, 7
```
-   A call is a line whose first word (after an optional label) is a macro name.
-   The number of arguments must equal the number of parameters: `macro 'ADD3' expects 3 argument(s), got 2`.
-   Arguments are split on commas and trimmed. An empty argument (`ADD3 R1,,R2` or a trailing comma) is `empty argument in call to 'ADD3'`.
-   Arguments are inserted as plain text, so anything is allowed that makes sense in the final instruction: registers, numbers, `-5`, `4[R2]`, label names, literal names.
-   A label on the call line is emitted on its own line before the expansion, so `top:` points at the first expanded instruction.

### 7.3 Substitution rules
-   **Whole tokens only.** A token is a run of letters, digits, `_`, `.` and `@`. Parameter `a` replaces the token `a` but not part of `data` or `loop_a`.
-   **Operands only.** Substitution happens in the operand part of each body line. The mnemonic and any label are never substituted, so a parameter named `add` cannot change `ADD`.
-   The text of an argument is inserted verbatim; it is not re-tokenised against the macro's own parameters.

### 7.4 Nesting and limits
A macro body may call other macros (including built-ins). Expansion repeats until no macro calls remain.
| Limit | Value | Error |
| :--- | :--- | :--- |
| Nesting depth | 16 | `macro 'X' expands too deeply (recursive macro?)` (also catches direct and mutual recursion) |
| Total expanded lines | 100 000 | `macro expansion is too large (over 100000 lines)` |
| Parameters per macro | 64 | `macro 'X' has too many parameters (max 64)` |

### 7.5 Macro-local labels (`@`)
A label or operand that starts with `@` inside a macro body is local to each expansion:
```asm
.macro WAIT n
    ADD R1, R0, n
@loop:
    SUB R1, R1, 1
    BEQ @done
    JMP @loop
@done:
.endm

.main:
    WAIT 10
    WAIT 20
```
Each call gets a number (1, 2, 3 … counting every macro call in the file, nested ones included, in expansion order). `@name` becomes `__<MACRONAME>_<n>_name`. The example above produces `__WAIT_1_loop`, `__WAIT_1_done`, `__WAIT_2_loop`, `__WAIT_2_done`.

Rules and errors:
-   The part after `@` must be an identifier (`invalid local label '@1bad'`, `'@'`).
-   A local label used in a macro must be defined in the same macro (`local label '@y' is used but never defined in macro 'A'`) and only once (`local label '@x' is defined twice in macro 'A'`).
-   `@` labels outside a macro body are an error, whether defined or used, or passed as a call argument (`local label '@x' can only be used inside a macro body`).
-   Ordinary (non-`@`) labels in a macro body are copied as written, so a macro containing one can only be called once (the second call gives `duplicate label`).
-   A label starting with `.` in a body is rejected (`label '.main' inside macro 'A' cannot start with '.'`), so a macro cannot fake `.main:` or `.literals:`.

### 7.6 Literals and macros
Lines after `.literals:` pass through untouched, so a literal may have the same name as a macro (`TWO: 1.5f` is fine). Macro definitions are not allowed in that section.

### 7.7 Line map and error context
For every output line the preprocessor records where it came from:
-   `origin_line[i]` — the 1-based line of the original file. All lines produced by a macro call, however deeply nested, map to the call-site line.
-   `origin_context[i]` — empty for ordinary lines, otherwise a text such as `in expansion of 'SET'` or `in expansion of 'SET', called from 'INIT', called from 'OUTER'` (innermost macro first).

Both preprocessor and assembler messages use this, so a bad register inside a macro is reported at the line that called the macro, with the macro chain appended. See section 8.

### 7.8 Preprocessing is one pass over text
-   `Preprocessor::expand` can be called repeatedly on the same object; each call starts clean.
-   All preprocessor errors are collected and returned together. If any exist, `risc201-asm` stops before assembling.
-   The preprocessor does not validate instructions; an expansion that produces an invalid instruction is caught by the assembler, and reported at the call line with the macro context.

### 7.9 Built-in macros: PUSH and POP
Defined by the preprocessor itself in the same macro language, available in every program, and not redefinable:
```asm
.macro PUSH reg
    SUB sp, sp, 1
    STORE reg, 0[sp]
.endm

.macro POP reg
    LOAD reg, 0[sp]
    ADD sp, sp, 1
.endm
```
-   The stack is full-descending and word-addressed: a push decrements SP by 1 word, then stores; a pop loads, then increments.
-   Because they expand to ordinary `SUB`/`ADD`, both macros change the flags.
-   Because they are ordinary instructions, the stack-bound checks come from the memory checks on `LOAD`/`STORE`, and a push that faults after its `SUB` has already committed leaves SP decremented.
-   Edge cases are not special-cased: `PUSH sp` stores the already-decremented value and `POP sp` is ambiguous; `PUSH R0` pushes a zero; `PUSH 5` fails in `STORE` with `invalid register '5'` (`in expansion of 'PUSH'`).
-   `CALL`/`RET` do not use the stack. Code that makes nested calls saves the link register itself: `PUSH ra … POP ra`.

### 7.10 Worked macro example
**Source:**
```asm
.macro SAVE2 a, b
    PUSH a
    PUSH b
.endm

.main:
    ADD sp, R0, 100
top: SAVE2 R2, R3
    POP R4
    JMP top
    HALT
```
**After preprocessing (what the assembler sees):**
```asm
.main:
    ADD sp, R0, 100
top:
    SUB sp, sp, 1
    STORE R2, 0[sp]
    SUB sp, sp, 1
    STORE R3, 0[sp]
    LOAD R4, 0[sp]
    ADD sp, sp, 1
    JMP top
    HALT
```

---

## 8. Diagnostics: errors, warnings and line numbers

### 8.1 Format
```
<file>:<line>: error: <message>
<file>:<line>: warning: <message>
```
If the line came from a macro, a suffix is appended:  `(in expansion of 'INNER', called from 'OUTER')`.

### 8.2 Order and grouping
1.  Preprocessor errors are printed first. If there are any, nothing else runs.
2.  Then all warnings, then all errors of the assembler.
3.  Pass 1 errors are printed before Pass 2 errors, so the order is by pass, not by line. For example, a duplicate label on line 3 and an unknown operation on line 5 (Pass 1) come before a bad register on line 4 (Pass 2).
4.  Everything wrong in a file is reported in one run; the tools do not stop at the first error.

### 8.3 Line numbers
-   They are 1-based and refer to the original file.
-   Missing `.main:` is reported as line 0 because it is not tied to a line.
-   Duplicate `.main:` is reported at the line of the last `.main:`.
-   Internally the assembler numbers its input after the preprocessor and after inserting `JMP .main`; the tools translate that back through `origin_line`. If you embed the library yourself you must do the same (12.4).

### 8.4 Errors versus warnings
-   **Errors** stop output from being produced. `assemblePass2()` returns an empty program whenever any error exists.
-   **Warnings** do not stop anything. Only two exist: jump offset not a multiple of 4, and out-of-range value truncated.

The complete list of messages is in Appendix A.

---

## 9. Machine code and file formats

### 9.1 Instruction words
All instructions are 32 bits.

**RI format** (everything except jumps and CALL):
```text
 31       27 26 25     22 21     18 17     14 13           0
┌───────────┬──┬─────────┬─────────┬─────────┬──────────────┐
│ opcode(5) │I │ rd (4)  │ rs1 (4) │ rs2 (4) │   unused     │   I = 0
└───────────┴──┴─────────┴─────────┴─────────┴──────────────┘
┌───────────┬──┬─────────┬─────────┬───────────────────────┐
│ opcode(5) │I │ rd (4)  │ rs1 (4) │   signed immediate(18)│   I = 1
└───────────┴──┴─────────┴─────────┴───────────────────────┘
```
Bit positions: `opcode` [31:27], `I` [26], `rd` [25:22], `rs1` [21:18], `rs2` [17:14], `immediate` [17:0].

**J format** (JMP, BEQ, BGT, CALL):
```text
 31       27 26                                           0
┌───────────┬──────────────────────────────────────────────┐
│ opcode(5) │       signed byte displacement (27)          │
└───────────┴──────────────────────────────────────────────┘
```
The disassembler sign-extends both the 18-bit and the 27-bit fields.

### 9.2 Addresses
| Quantity | Unit |
| :--- | :--- |
| Assembler location counter, label values, jump displacements | Bytes (4 per instruction) |
| Simulator PCs, breakpoints, data-memory addresses, SP, LOAD/STORE offsets | Words |

-   The first word of every image is `JMP .main` (`0x88000000` when `.main` is the next word) and `.main` is at byte address 4 or later.
-   A displacement of `N` bytes from the instruction at byte `A` targets byte `A + 4 + N`. In a disassembly, `JMP 0` means "jump to the next instruction", not "jump to address zero".

### 9.3 Literal section layout
```text
word 0 .. n-1   instruction words                    (JMP .main first)
word n          0xFFFFFFFF   literals marker         (only if there are literals)
word n+1 ..     literal words, in source order       (IEEE-754 single precision bit patterns)
```
The marker equals an all-ones word, whose opcode bits (`0x1F`) decode as `HALT`.

### 9.4 .bin format
Raw words, four bytes each, big-endian (most significant byte first), no header. A file whose size is not a multiple of 4 is rejected by the reader.
`88 00 00 00 | 14 48 00 05 | F8 00 00 00`

### 9.5 .hex format
One word per line, 8 upper-case hex digits, no prefix, no comments:
```text
88000000
14480005
F8000000
```
The reader (`readHexWords`) skips blank lines, accepts an optional `0x`/`0X` prefix, rejects non-hex text and values wider than 32 bits, and does not allow comments or several words per line.

---

## 10. How the assembler works internally

### 10.1 Pass 1 — Assembler::assemblePass1

```text
┌─────────────────────────────────────────────────────┐
│    expanded.lines (from Preprocessor)               │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│ Assembler::assemblePass1                            │
│ 1. Find .main: (error if missing or multiple)       │
│ 2. Insert "JMP .main" at start                      │
│ 3. For each line: parseLine()                       │
│    - Strip comments/whitespace                      │
│    - Extract labels -> symbol_table_                │
│    - Parse literals -> literal_words_               │
│    - Tokenize operation & operands                  │
│    - Advance location_counter_ by 4 (if word emits) │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│ parsed_lines_, symbol_table_, literal_words_, etc.  │
└─────────────────────────────────────────────────────┘
```

Input: the (already preprocessed) lines. Output: `parsed_lines_`, `symbol_table_`, `literal_words_`, `literal_index_`, errors.
1.  Clear all state (symbol table, errors, parsed lines, literals, location counter).
2.  Scan the lines for `.main:` labels. None → error at line 0 and stop. More than one → error at the last one and stop.
3.  Insert `"JMP .main"` at the front of the line list. From here on, index `i` in the list equals the 1-based line number in the lines that were passed in (the inserted line takes index 0), which keeps diagnostics correct without extra bookkeeping. When the input came from the preprocessor, that number is a line of the expanded text and is mapped back to the original file through `origin_line`.
4.  For each line call `parseLine`:
-   strip comment and whitespace; skip empty lines;
-   extract a label, validate it (`isValidLabelName`), reject duplicates, record `symbol_table_[label] = location_counter_`;
-   `.literals:` switches `in_literals_` on;
-   in the literal section: parse `name: value`, store the float bits in `literal_words_` and the name→index in `literal_index_`; the literal still gets a `symbol_table_` entry and counts 4 bytes;
-   otherwise tokenise into operation + operands, check the mnemonic against `opTable()` and the operand count against the pattern;
-   advance `location_counter_` by 4 for every line that emits a word.
5.  Return `errors_.empty()`.

A line with an unknown operation emits no word (so later addresses are computed as if it were absent), but the error means no output is produced anyway.

### 10.2 Pass 2 — Assembler::assemblePass2

```text
┌─────────────────────────────────────────────────────┐
│ parsed_lines_ (from Pass 1)                         │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│ Assembler::assemblePass2                            │
│ For each non-literal word-emitting line:            │
│ 1. resolveInstruction()                             │
│    - Parse operands (Registers, Immediates, Labels) │
│    - Resolve labels using symbol_table_             │
│    - Range-check immediate/offset (18 or 27 bits)   │
│    - Verify R0 write protection                     │
│ 2. Construct Instruction struct                     │
└────────────────────────┬────────────────────────────┘
                         ▼
┌─────────────────────────────────────────────────────┐
│            std::vector<Instruction>                 │
└─────────────────────────────────────────────────────┘
```

1.  Clear warnings (errors from Pass 1 are kept).
2.  For every parsed line that emits a word and is not a literal, look up its `OpInfo` and call `resolveInstruction`.
3.  Return the program if there are no errors, otherwise an empty vector.

`resolveInstruction`:
1.  Re-check operand count (silently returns `false` if it differs; Pass 1 already reported it).
2.  Initialise an `Instruction` with all registers `R0`, `imm = 0`, `I = false`.
3.  Walk the pattern. `EMPTY` skips a register slot. For each other kind, parse the operand (4.6) and fill `rd`/`rs1`/`rs2`/`imm`/`I`. For a `LABEL` operand: a number is a raw offset; otherwise the name is looked up in `symbol_table_` (undefined label if absent); a name found in `literal_index_`, or `.literals` itself, is rejected as data (`cannot jump to …`); otherwise `imm = target − (pc + 4)`.
4.  `R0` check: if `writesDestination(op)` and `rd == R0` → error `'OP' cannot write to R0 (R0 is always zero)`.
5.  Range check the immediate/displacement (18 or 27 bits). Out of range → warning and truncation.
6.  Return `true`.

### 10.3 Encoding — Assembler::encodeInstruction
Implements the layouts in 9.1. `main_asm` encodes every instruction, appends `literalSection()` (marker + literals, or nothing) and writes the file.

### 10.4 Operation table — op_table.h
-   `opTable()` maps the upper-case mnemonic to `OpInfo { opcode, format, operand_kinds }`.
-   `opcodeToMnemonic()` is the reverse map, built once from `opTable()`; the disassembler uses it, so adding a row to `opTable()` is enough for the assembler and disassembler to know a new instruction, provided the shared formatter prints it correctly (16.1).
-   `writesDestination(op)` lists the opcodes whose first register operand is a destination (used for the `R0` rule).
-   `writtenOperandCount(info)` counts non-`EMPTY` pattern entries.

---

## 11. The disassembler

### 11.1 What it does
`Disassembler` turns words back into text. The main steps:
1.  `decodeProgram(words)` decodes each word until it meets the marker `0xFFFFFFFF`; the words after the marker are stored as literals.
2.  A word with an unassigned opcode is recorded in `decodeErrors()` as `(index, word)`, and a placeholder instruction is kept so that addresses stay correct.
3.  `disassembleSource(program, use_labels)` formats the instructions as assembler source: it drops the automatic entry JMP and restores `.main:`. This is what `risc201-disasm` uses.
4.  `disassemble` and `disassembleWithLabels` are plain listings: one line per decoded instruction, entry JMP included, no `.main:` (12.3).
5.  `formatLiterals` formats the literal section.

Instruction text comes from one shared formatter, `formatInstruction` in `utils`; `Disassembler::formatInstruction` calls it. A test assembles one instruction for every op-table entry and checks that the formatter prints exactly the text the assembler read.

### 11.2 Output format
For `disassembleSource` (and therefore `risc201-disasm`):
-   One instruction per line, mnemonic in upper case, operands separated by `, `.
-   Registers are always printed `Rn`.
-   Immediates are printed in decimal (hex input is not preserved).
-   Memory operands are `imm[Rn]`; a `LOAD` that came from a literal name is therefore printed `LOAD R1, 0[R0]`.
-   Float instructions print as the assembler writes them: `FADD`/`FSUB`/`FMUL`/`FDIV rd, rs1, rs2` and `FCMP rs1, rs2`. Fields an instruction does not use are never printed, whatever bits they hold.
-   Jump operands are raw byte displacements (`JMP 8`, `BEQ -16`), or labels with `-l` (11.3).
-   Unknown opcodes print `; unknown opcode`.

**Entry point.** The assembler always puts `JMP .main` at word 0, so the disassembler undoes it:
-   If word 0 is a `JMP` whose target (4 + displacement) is a multiple of 4, is at least 4, and lies inside the program, that `JMP` is not printed, and the line `.main:` is written immediately before the instruction at that address. Code that came before `.main:` in the original source stays before it.
-   Otherwise (word 0 is not a JMP, or its target is not an instruction of the program) the first line is the comment `; no entry JMP at word 0, so .main is placed on the first instruction`, `.main:` goes before the first instruction, and word 0 is printed like any other instruction (a `JMP 400` there stays `JMP 400`).
-   An empty program produces just `.main:`.

### 11.3 Label synthesis (-l)
-   A label is generated for every distinct target of a `JMP`/`BEQ`/`BGT`/`CALL` (target = address + 4 + displacement) that is a printed instruction: a multiple of 4, not before the first printed instruction, and not past the last instruction.
-   Names are `L` followed by the target byte address in lower-case hex, at least 4 digits: `L0004`, `L000c`, `L0018`.
-   The label line (`L000c:`) is written immediately before the instruction at that address; jumps print the name.
-   The entry point is always `.main`. A generated name never replaces it: jumps back to the entry instruction print `.main`, and no `L….:` line is written at that address.
-   Any target that is not a printed instruction (past the end of the program, before the first printed instruction — including address 0, the dropped entry JMP — or not a multiple of 4) keeps its raw displacement, with no label: `BEQ 400`, `JMP -100`.

### 11.4 Literal listing
If a literal section exists, the output ends with:
```asm
.literals:
lit0: 3.14f
lit1: 2.71828f
```
-   Names are `lit0`, `lit1`, … (original names are not stored in the image).
-   Each value is printed with the shortest precision (1–9 significant digits) that parses back to the same float. `.0` is added when the text has no `.`, `e` or `E`; scientific notation appears for large/small values (`1e+03f`).
-   NaN/infinite bit patterns are printed as a comment: `; litN: non-finite float, raw bits 0xXXXXXXXX`.

`disassembleSource` does not include the literal listing; `risc201-disasm` appends `formatLiterals()` after it.

### 11.5 Round trip
The output of `risc201-disasm`, with or without `-l`, assembles back to the identical image: source → words → source → words gives the same words. This is tested for plain code, code placed before `.main:`, loops that jump back to `.main`, jumps past the last instruction, and programs with literals (`LOAD R1, pi` comes back as `LOAD R1, 0[R0]`, which encodes to the same word).

**What does not survive:**
-   **Names and layout.** Label names, literal names (`lit0`, `lit1`, …), register aliases, comments, macros and number formats are not stored in the image, so they are not reconstructed.
-   **Images that are not assembler output.** If word 0 is not an entry JMP, the re-assembled image gains one, because the assembler always inserts its own.
-   **Lines that are comments.** `; unknown opcode` lines and non-finite literals (`; litN: …`) are comments to the assembler, so the words after them move and the image no longer matches.

The plain listings `disassemble` and `disassembleWithLabels` keep the entry JMP and write no `.main:`, so they are not re-assemblable (`missing required entry-point label '.main:'`); use `disassembleSource` for text you want to assemble again.

---

## 12. C++ library API
All declarations live in namespace `risc201`. Link against `assembler_lib` (it brings `isa_spec` and `utils_lib`).

### 12.1 Preprocessor (preprocessor.h)

| Member | Purpose | 
| ----- | ----- | 
| `PreprocessResult expand(const std::vector<std::string>&)` | Expands macros and built-ins. Returns expanded source and origin maps. | 
| `PreprocessResult::lines` | `std::vector<std::string>`: expanded source. | 
| `PreprocessResult::origin_line` | `std::vector<int>`: 1-based original line for each output line. | 
| `PreprocessResult::origin_context` | `std::vector<std::string>`: `""` or `"in expansion of 'X', called from 'Y'"`. | 
| `PreprocessResult::errors` | `std::vector<PreprocessError>`: preprocessing errors, if any. | 

**Supporting types:** `PreprocessError` (`int line_number`, `std::string message`), `PreprocessResult`.

**Usage notes:**

* `lines`, `origin_line` and `origin_context` always have the same length.
* Output lines are the original text for ordinary lines, and rebuilt text for macro bodies.
* If `errors` is non-empty, do not assemble `lines`.

### 12.2 Assembler (assembler.h)
| Member | Purpose |
| :--- | :--- |
| `bool assemblePass1(const std::vector<std::string>&)` | Pass 1. `false` if errors. |
| `std::vector<Instruction> assemblePass2()` | Pass 2. Empty vector if any error (Pass 1 or 2). |
| `symbolTable()` | `unordered_map<string, uint32_t>`: label and literal name → byte address. |
| `errors()` / `warnings()` | `vector<AssemblyError{line_number, message}>`. |
| `literalWords()` | Raw literal words, without marker. |
| `literalSection()` | Marker + literal words, or empty if there are no literals. |
| `encodeInstruction(const Instruction&)` | `Instruction` → 32-bit word. |
| `writeBinaryFile(path, words)` / `writeHexFile(path, words)` | Write the formats in section 9. Return `false` on I/O failure. |

**Free functions:** `readHexWords(path, out)` and `readBinaryWords(path, out)` (both return `false` on failure).

**Supporting types:** `ParsedLine` (line number, label, op, operands, address, emits_word, is_literal) and `AssemblyError`.

**Usage notes:**
-   Call `assemblePass1` then `assemblePass2` on the same object. `assemblePass1` resets everything, so one object can assemble many programs in turn.
-   The line numbers in `errors()`/`warnings()` are 1-based positions in the lines you passed to `assemblePass1` (see 10.1, step 3). Line 0 means "not tied to a line".

### 12.3 Disassembler (disassembler.h)
| Member | Purpose |
| :--- | :--- |
| `decodeWord(word, Instruction&)` | `false` if the opcode is unassigned. |
| `decodeProgram(words)` | Decode up to the marker; fills `decodeErrors()` and `literalWords()`. |
| `disassembleSource(program, use_labels)` | Assembler source: entry JMP dropped, `.main:` restored, optional generated labels (11.2, 11.3). Does not include the literal listing; append `formatLiterals()`. Used by `risc201-disasm`. |
| `disassemble(program)` | One text line per instruction exactly as decoded (entry JMP included, raw displacements). Not re-assemblable. |
| `disassembleWithLabels(program)` | Same with generated labels, for in-range, 4-byte-aligned targets only. No `.main:`. Not re-assemblable. |
| `formatInstruction(instr)` | Text for one instruction (`; unknown opcode` for an unassigned opcode). Calls the shared `utils` formatter. |
| `formatLiterals()` | The `.literals:` listing (empty if none). |
| `readBinaryFile(path, out)` / `readHexFile(path, out)` | Wrappers over the free readers. |
| `decodeErrors()` | `vector<pair<size_t, uint32_t>>`: word index and value. |

### 12.4 Using the whole pipeline from code
This mirrors `main_asm.cpp`:
```cpp
#include <assembler/assembler.h>
#include <assembler/preprocessor.h>

using namespace risc201;

std::vector<uint32_t> assembleText(const std::vector<std::string>& lines,
                                   std::vector<std::string>& messages) {
    Preprocessor pp;
    PreprocessResult expanded = pp.expand(lines);
    if (!expanded.errors.empty()) {
        for (auto& e : expanded.errors)
            messages.push_back(std::to_string(e.line_number) + ": " + e.message);
        return {};
    }

    // Assembler line N  <->  expanded.lines[N - 1]  (Pass 1 puts JMP .main in front).
    auto originalLine = [&](int n) {
-         return (n >= 1 && n <= (int)expanded.origin_line.size()) ? expanded.origin_line[n
1] : n;
    };

    Assembler as;
    as.assemblePass1(expanded.lines);
    std::vector<Instruction> program = as.assemblePass2();
    if (!as.errors().empty()) {
        for (auto& e : as.errors())
            messages.push_back(std::to_string(originalLine(e.line_number)) + ": " + e.message);
        return {};
    }

    std::vector<uint32_t> words;
    for (auto& instr : program) words.push_back(as.encodeInstruction(instr));
    for (uint32_t w : as.literalSection()) words.push_back(w);
    return words;
}
```
For the macro context suffix, also read `expanded.origin_context[n - 1]` as `main_asm` does.

---

## 13. Integration with the simulator
This is outside `assembler/`, but it explains why some assembler rules exist.
-   **Loading.** `Cpu::loadWords` splits the image at the first `0xFFFFFFFF`: words before it become the program, words after it are copied into data memory starting at index 0. That is why a literal name assembles to `imm = index` with base `R0`.
-   **Separate memories.** Program memory and data memory are separate. `LOAD`/`STORE` only touch data memory, and literals live there.
-   **Registers at reset.** All zero, except `R15` (`= SP`), which points one word above the top of data memory (4096 words by default).
-   **`R0`.** Treated as zero by convention. The simulator does not discard writes to it, so the assembler's rule is the only guard for assembled programs.
-   **Entry.** Execution starts at word 0, which is the automatic `JMP .main`.
-   **Input paths.** The simulator accepts `.asm` (preprocessed and assembled on the fly), `.hex` and `.bin`.

---

## 14. Known limitations and quirks
These were verified against the code. None of them crash the tools.

| # | Area | Behaviour |
| :--- | :--- | :--- |
| 1 | Disassembler | Names are not reconstructed. Label names, literal names (`lit0`, …), register aliases, comments, macros and number formats are not in the image, and immediates print in decimal. Images containing unknown opcodes or non-finite literals do not round-trip, because those print as comment lines (11.5). |
| 2 | Library | `Disassembler::disassemble` and `disassembleWithLabels` are plain listings: they keep the entry JMP and write no `.main:`, so their output is not re-assemblable. Use `disassembleSource` (12.3). |
| 3 | Assembler | Whitespace inside `[...]` is rejected: `4[ R2 ]` and `4 [R2]` are malformed. |
| 4 | Error ordering | Pass 1 errors are printed before Pass 2 errors regardless of line number. |

---

## 15. Tests
All tests are registered in `tests/CMakeLists.txt` and run with `ctest --test-dir build`.

| Test | File | Covers |
| :--- | :--- | :--- |
| `Pass1Test` | `pass1_test.cpp` | Label pass, `.main`, symbol table, location counter |
| `Pass2Test` | `pass2_test.cpp` | Operand resolution, ranges, warnings, `R0` rule |
| `EncoderTest` | `encoder_test.cpp` | Bit layouts for RI and J formats |
| `DisassemblerTest` | `disassembler_test.cpp` | Decoding and text formatting; every op-table entry prints exactly what the assembler accepts (`testFormattersAgreeWithOpTable`); FCMP formatting; `disassembleSource` restores `.main:`, drops the entry JMP, keeps jumps past the end raw, handles images with no entry JMP, and re-assembles to identical words with and without `-l` |
| `LiteralsTest` | `literals_test.cpp` | `.literals:` parsing, marker, `LOAD name`, and the error for jumping to a literal or to `.literals` (`JMP`/`BEQ`/`BGT`/`CALL`) |
| `PreprocessorTest`| `preprocessor_test.cpp` | Macros, `@` labels, built-ins, line map, error cases (including `SP`/`RA` in any case refused as parameter names), macro source vs hand-expanded source producing identical words |

Run only the assembler-related ones:
```bash
ctest --test-dir build -R "Pass1|Pass2|Encoder|Disassembler|Literals|Preprocessor"
```

---

## 17. Worked examples

### 17.1 A loop, with the machine code explained
```asm
; sum of 1..3
.main:
    MOV R1, 3
    MOV R2, 0
loop:
    ADD R2, R2, R1
    SUB R1, R1, 1
    CMP R1, 0
    BGT loop
    HALT
```
`risc201-asm sum.asm -o sum.hex` writes:
| Byte addr | Hex word | Instruction | Why |
| :--- | :--- | :--- | :--- |
| 0 | `88000000` | `JMP .main` | opcode `0x11` in the top 5 bits; displacement 0 (`.main` is the next instruction) |
| 4 | `0C400003` | `MOV R1, 3` | opcode 1, `I=1`, `rd=1`, immediate 3 |
| 8 | `0C800000` | `MOV R2, 0` | opcode 1, `I=1`, `rd=2`, immediate 0 |
| 12 | `10884000` | `ADD R2, R2, R1` | opcode 2, `I=0`, `rd=2`, `rs1=2`, `rs2=1` |
| 16 | `1C440001` | `SUB R1, R1, 1` | opcode 3, `I=1`, `rd=1`, `rs1=1`, immediate 1 |
| 20 | `84040000` | `CMP R1, 0` | opcode `0x10`, `I=1`, `rd` empty, `rs1=1`, immediate 0 |
| 24 | `9FFFFFF0` | `BGT loop` | opcode `0x13`; displacement 12 − (24 + 4) = −16 in 27 bits |
| 28 | `F8000000` | `HALT` | opcode `0x1F` |

`risc201-disasm sum.hex -l` gives (the entry JMP becomes `.main:`; only `loop` needs a generated name):
```asm
.main:
MOV R1, 3
MOV R2, 0
L000c:
ADD R2, R2, R1
SUB R1, R1, 1
CMP R1, 0
BGT L000c
HALT
```
Without `-l` the jump keeps its raw displacement and there is no `L000c:` line:
```asm
.main:
MOV R1, 3
MOV R2, 0
ADD R2, R2, R1
SUB R1, R1, 1
CMP R1, 0
BGT -16
HALT
```
Both texts assemble back to exactly the eight words above. The tool reports `Disassembled 8 instruction(s)` in both cases: the count includes the entry JMP, which is not printed.

### 17.2 A literal
```asm
.main:
    LOAD R1, one_half
    HALT
.literals:
    one_half: 0.5f
```
Image (`.hex`):
```text
88000000     JMP .main
74400000     LOAD R1, 0[R0]     (opcode 0x0E, I=1, rd=1, rs1=R0, imm=0 = literal index)
F8000000     HALT
FFFFFFFF     literals marker
3F000000     0.5f
```
Disassembly:
```asm
.main:
LOAD R1, 0[R0]
HALT
.literals:
lit0: 0.5f
```
This text also assembles back to the same five words, even though the name `one_half` became `lit0` and the load became `0[R0]`.

### 17.3 Errors with macro context
```asm
.macro SET a, v        ; line 1
    ADD a, R0, v       ; line 2
.endm                  ; line 3
.main:                 ; line 4
    SET R1, 5          ; line 5
    SET R0, 5          ; line 6  <- writes R0
    SET R2, R88        ; line 7  <- bad register
```
```text
prog.asm:6: error: 'ADD' cannot write to R0 (R0 is always zero) (in expansion of 'SET')
prog.asm:7: error: 'R88' is not a valid register or immediate (in expansion of 'SET')
```

### 17.4 Code before .main:, round trip
```asm
fn: ADD R1, R1, 1
    RET
.main:
    CALL fn
    HALT
```
`risc201-asm` puts the entry JMP first, then `fn` at byte 4, `RET` at 8, `.main` at 12. `risc201-disasm` finds the entry target (byte 12), drops the JMP, and writes `.main:` in the middle:
```asm
ADD R1, R1, 1
RET
.main:
CALL -12
HALT
```
With `-l`, the call target gets a generated name and `.main` is unchanged:
```asm
L0004:
ADD R1, R1, 1
RET
.main:
CALL L0004
HALT
```
Both assemble back to the original image.

---

## 18. Appendix A: complete message catalogue

### A.1 Preprocessor errors
| Message | When |
| :--- | :--- |
| `'.macro' needs a name` | `.macro` with nothing after it |
| `invalid macro name 'X'` | name is not an identifier |
| `macro name 'X' clashes with an instruction mnemonic` | e.g. `.macro ADD` |
| `macro 'X' is built in and cannot be redefined` | redefining `PUSH` / `POP` |
| `macro 'X' is already defined on line N` | duplicate macro |
| `empty parameter name in '.macro X'` | e.g. `.macro X a,,b` |
| `invalid parameter name 'p'` | not an identifier |
| `parameter 'p' looks like a register name` | `R0`–`R15`-style, or the aliases `SP` / `RA` in any case |
| `duplicate parameter 'p'` | repeated name |
| `macro 'X' has too many parameters (max 64)` | more than 64 |
| `'.macro' inside macro 'X' (macros cannot be nested in a definition)` | nested definition |
| `'.macro' is not allowed after '.literals:' (that section holds only literals)` | definition in the literal section |
| `'.endm' without a matching '.macro'` | stray `.endm` |
| `unexpected text after '.endm'` | `.endm junk` |
| `'.macro X' has no matching '.endm'` | missing `.endm` (reported on the definition line) |
| `label '.x' inside macro 'X' cannot start with '.'` | e.g. fake `.main:` |
| `invalid local label '@…'` | `@`, `@1bad`, `@:` |
| `local label '@x' is defined twice in macro 'X'` | duplicate `@` label |
| `local label '@x' is used but never defined in macro 'X'` | `@` use without definition |
| `local label '@x' can only be used inside a macro body` | `@` label outside a macro |
| `macro 'X' expects N argument(s), got M` | wrong argument count |
| `empty argument in call to 'X'` | `X a,,b` or trailing comma |
| `macro 'X' expands too deeply (recursive macro?)` | depth over 16 |
| `macro expansion is too large (over 100000 lines)` | output over 100000 lines |

Errors raised while expanding a macro carry the suffix  `(in expansion of 'X'[, called from 'Y' …])`.

### A.2 Assembler errors
| Message | When |
| :--- | :--- |
| `missing required entry-point label '.main:'` | no `.main:` (line 0) |
| `duplicate entry-point label '.main:'` | more than one |
| `invalid label 'X': label names cannot start with a digit or '-'` | bad label |
| `duplicate label 'X'` | label or literal name defined twice (including a second `.literals:`) |
| `unexpected text after '.literals:'` | `.literals: junk` |
| `literal needs a name, expected 'name: 1.0f'` | unnamed line in the literal section |
| `literal 'X' has no value` | `name:` with nothing after it |
| `invalid float literal 'X' (expected e.g. 3.14f)` | missing `f`, inf/NaN, trailing text |
| `unknown operation 'X'` | not in the op table (shown upper-case) |
| `'X' expects N operand(s), got M` | wrong operand count |
| `invalid register 'X'` | register expected, e.g. `FADD R1, R2, 5` |
| `invalid immediate 'X'` | immediate expected but not a number |
| `'X' is not a valid register or immediate` | e.g. `R16`, `banana` |
| `malformed memory operand 'X' (expected imm[reg] or a literal name)` | bad `off[reg]` |
| `invalid jump offset 'X'` | starts with digit/`-` but not a number |
| `undefined label 'X'` | unknown jump target |
| `cannot jump to 'X': it is in the literal section (data)` | jump target is a literal name or `.literals` |
| `'X' cannot write to R0 (R0 is always zero)` | destination is `R0` |
| `internal error: …` | table/pass inconsistency (should not occur) |

### A.3 Assembler warnings
| Message | When |
| :--- | :--- |
| `jump offset X is not a multiple of 4 (instructions are 4 bytes)` | raw jump offset not divisible by 4 |
| `value X out of range for N-bit field, truncated to T` | immediate over 18 bits or displacement over 27 bits |

### A.4 Command-line messages
| Tool | Message |
| :--- | :--- |
| `risc201-asm` | `usage: risc201-asm <input.asm> [-o output.bin/output.hex]` |
| `risc201-asm` | `error: cannot open <file>` |
| `risc201-asm` | `error: failed to write output file <file>` |
| `risc201-asm` | `Assembled N instruction(s) -> <file>` |
| `risc201-disasm` | `usage: risc201-disasm <input.bin/input.hex> [-o output.asm] [-l]` |
| `risc201-disasm` | `error: cannot read or parse <file>` |
| `risc201-disasm` | `warning: word I (0xH) has an unrecognized opcode, emitted as '; unknown opcode'` |
| `risc201-disasm` | `error: cannot open <file>` |
| `risc201-disasm` | `Disassembled N instruction(s) -> <file>` |