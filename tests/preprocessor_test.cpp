// Preprocessor tests: .macro / .endm expansion, @local labels, built-in PUSH/POP,
// the line map, error context, every error case, and a final check that a source
// written with macros assembles to the same words as its hand-expanded copy.

#include "assembler/assembler.h"
#include "assembler/preprocessor.h"
#include "utils/utils.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        std::cerr << "  FAIL line " << __LINE__ << ": " #cond << "\n";     \
        ++failures;                                                        \
    }                                                                      \
} while (0)

void section(const char* name) { std::cout << name << "\n"; }

using namespace risc201;
using Lines = std::vector<std::string>;

// One non-empty output line with where it came from. Comments and blank lines are
// dropped, so these tests don't depend on whether the preprocessor keeps them.
struct Row {
    std::string text;
    int origin = 0;
    std::string context;
};

std::vector<Row> rowsOf(const PreprocessResult& r) {
    std::vector<Row> rows;
    for (size_t i = 0; i < r.lines.size(); ++i) {
        std::string text = stripCommentandTrim(r.lines[i]);
        if (text.empty()) continue;
        std::string ctx = i < r.origin_context.size() ? r.origin_context[i] : "";
        rows.push_back(Row{text, r.origin_line[i], ctx});
    }
    return rows;
}

Lines textsOf(const PreprocessResult& r) {
    Lines out;
    for (const auto& row : rowsOf(r)) out.push_back(row.text);
    return out;
}

PreprocessResult run(const Lines& src) {
    Preprocessor pp;
    return pp.expand(src);
}

std::string show(const Lines& lines) {
    std::string s;
    for (const auto& l : lines) s += "      | " + l + "\n";
    return s;
}

void checkLines(int line, const char* what, const Lines& got, const Lines& want) {
    if (got != want) {
        std::cerr << "  FAIL line " << line << ": " << what << "\n    got:\n" << show(got)
                  << "    want:\n" << show(want);
        ++failures;
    }
}
#define CHECK_LINES(got, want) checkLines(__LINE__, #got, (got), (want))

std::string allErrors(const PreprocessResult& r) {
    std::string s;
    for (const auto& e : r.errors) s += "      | line " + std::to_string(e.line_number) + ": " + e.message + "\n";
    return s;
}

// True if some error is on `line` and its message contains `text`.
bool hasError(const PreprocessResult& r, int line, const std::string& text) {
    for (const auto& e : r.errors) {
        if (e.line_number == line && e.message.find(text) != std::string::npos) return true;
    }
    return false;
}

#define CHECK_ERROR(result, line, text) do {                                          \
    if (!hasError((result), (line), (text))) {                                        \
        std::cerr << "  FAIL line " << __LINE__ << ": expected error on line " << (line) \
                  << " containing \"" << (text) << "\"\n    errors were:\n"            \
                  << allErrors(result);                                               \
        ++failures;                                                                   \
    }                                                                                 \
} while (0)

// Preprocess, assemble and encode. ok is false if either stage reports an error.
std::vector<uint32_t> encodeAll(const Lines& src, bool& ok) {
    ok = false;
    PreprocessResult expanded = run(src);
    if (!expanded.errors.empty()) {
        std::cerr << "    preprocessor errors:\n" << allErrors(expanded);
        return {};
    }

    Assembler assembler;
    assembler.assemblePass1(expanded.lines);
    std::vector<Instruction> program = assembler.assemblePass2();
    if (!assembler.errors().empty()) {
        for (const auto& e : assembler.errors()) std::cerr << "    assembler error line " << e.line_number << ": " << e.message << "\n";
        return {};
    }

    std::vector<uint32_t> words;
    for (const auto& instr : program) words.push_back(assembler.encodeInstruction(instr));
    for (uint32_t w : assembler.literalSection()) words.push_back(w);
    ok = true;
    return words;
}

// ---------------------------------------------------------------- expansion

void testFixedArity() {
    section("Expansion: fixed-arity macro substitutes its arguments");
    PreprocessResult r = run({
        ".macro ADD3 a, b, c",          // 1
        "    ADD a, a, b",              // 2
        "    ADD a, a, c",              // 3
        ".endm",                        // 4
        ".main:",                       // 5
        "    ADD3 R1, R2, R3",          // 6
        "    HALT",                     // 7
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, R2", "ADD R1, R1, R3", "HALT"}));

    // The definition is removed; every expanded line points at the call site.
    auto rows = rowsOf(r);
    CHECK(rows.size() == 4);
    CHECK(rows[0].origin == 5);
    CHECK(rows[1].origin == 6 && rows[2].origin == 6);
    CHECK(rows[3].origin == 7);
}

void testManyParameters() {
    section("Expansion: any number of parameters, including none");
    PreprocessResult r = run({
        ".macro ADD5 a, b, c, d, e",
        "    ADD a, b, c",
        "    ADD a, a, d",
        "    ADD a, a, e",
        ".endm",
        ".macro BYE",
        "    HALT",
        ".endm",
        ".main:",
        "    ADD5 R1, R2, R3, R4, R5",
        "    BYE",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R2, R3", "ADD R1, R1, R4", "ADD R1, R1, R5", "HALT"}));

    // 20 parameters
    Lines src = {".macro WIDE p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20",
                 "    ADD p1, p20, p10",
                 ".endm",
                 ".main:",
                 "    WIDE R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R0, R1, R2, R3, 77"};
    PreprocessResult wide = run(src);
    CHECK(wide.errors.empty());
    CHECK_LINES(textsOf(wide), (Lines{".main:", "ADD R1, 77, R10"}));
}

void testWholeTokenSubstitution() {
    section("Expansion: whole tokens only, operands only");
    PreprocessResult r = run({
        ".macro T a",
        "    ADD a, a, data",            // 'data' contains the letter a: must not change
        "    JMP loop_a",                // 'loop_a' is one token: must not change
        ".endm",
        ".macro T2 add",                 // parameter named like a mnemonic
        "    ADD add, add, 1",
        ".endm",
        ".main:",
        "    T R1",
        "    T2 R5",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, data", "JMP loop_a", "ADD R5, R5, 1"}));
}

void testArgumentsPassThroughAsText() {
    section("Expansion: memory operands, negatives and labels pass through as text");
    PreprocessResult r = run({
        ".macro M r, off, target",
        "    LOAD r, off",
        "    JMP target",
        ".endm",
        ".macro ADDI r, v",
        "    ADD r, r, v",
        ".endm",
        ".main:",
        "    M R1, 4[R2], done",
        "    ADDI R3, -5",
        "done: HALT",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "LOAD R1, 4[R2]", "JMP done", "ADD R3, R3, -5", "done: HALT"}));
}

void testCaseInsensitiveNames() {
    section("Expansion: macro names are case-insensitive");
    PreprocessResult r = run({
        ".macro Inc a",
        "    ADD a, a, 1",
        ".endm",
        ".main:",
        "    inc R1",
        "    INC R2",
        "    iNc R3",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, 1", "ADD R2, R2, 1", "ADD R3, R3, 1"}));
}

void testUseBeforeDefinition() {
    section("Expansion: a macro can be used above its definition");
    PreprocessResult r = run({
        ".main:",
        "    INC R1",
        ".macro INC a",
        "    ADD a, a, 1",
        ".endm",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, 1"}));
}

void testLabelOnCallLine() {
    section("Expansion: a label on a call line goes on its own line first");
    PreprocessResult r = run({
        ".macro INC a",
        "    ADD a, a, 1",
        ".endm",
        ".main:",
        "top: INC R1",
        "    JMP top",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "top:", "ADD R1, R1, 1", "JMP top"}));
}

void testNesting() {
    section("Expansion: macros can call other macros");
    PreprocessResult r = run({
        ".macro ADD3 a, b, c",
        "    ADD a, a, b",
        "    ADD a, a, c",
        ".endm",
        ".macro ADD5 a, b, c, d, e",
        "    ADD3 a, b, c",
        "    ADD a, a, d",
        "    ADD a, a, e",
        ".endm",
        ".main:",
        "    ADD5 R1, R2, R3, R4, 9",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, R2", "ADD R1, R1, R3", "ADD R1, R1, R4", "ADD R1, R1, 9"}));
}

void testLiteralsUntouched() {
    section("Expansion: nothing after .literals: is expanded");
    PreprocessResult r = run({
        ".macro TWO a, b",
        "    ADD a, a, b",
        ".endm",
        ".main:",
        "    TWO R1, R2",
        "    HALT",
        ".literals:",
        "TWO: 1.5f",
        "pi: 3.14f",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R1, R2", "HALT", ".literals:", "TWO: 1.5f", "pi: 3.14f"}));
}

void testNoMacrosPassThrough() {
    section("Expansion: a file without macros keeps its lines and numbering");
    PreprocessResult r = run({
        ".main:",
        "    ADD R1, R2, R3   ; comment",
        "",
        "    HALT",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "ADD R1, R2, R3", "HALT"}));
    auto rows = rowsOf(r);
    CHECK(rows.size() == 3);
    CHECK(rows[0].origin == 1 && rows[1].origin == 2 && rows[2].origin == 4);
    for (const auto& row : rows) CHECK(row.context.empty());
}

void testReusable() {
    section("Expansion: one Preprocessor object can be used twice");
    Preprocessor pp;
    Lines a = {".macro INC a", "    ADD a, a, 1", ".endm", ".main:", "    INC R1"};
    Lines b = {".main:", "    INC R1"};          // INC no longer exists here
    PreprocessResult first = pp.expand(a);
    PreprocessResult second = pp.expand(b);
    CHECK(first.errors.empty());
    CHECK_LINES(textsOf(first), (Lines{".main:", "ADD R1, R1, 1"}));
    CHECK(second.errors.empty());
    CHECK_LINES(textsOf(second), (Lines{".main:", "INC R1"}));      // passed through, not expanded
}

// -------------------------------------------------------------- local labels

void testLocalLabelsUniquePerExpansion() {
    section("Local labels: @labels are renamed per expansion");
    PreprocessResult r = run({
        ".macro WAIT n",
        "    ADD R1, R0, n",
        "@loop:",
        "    SUB R1, R1, 1",
        "    BEQ @done",
        "    JMP @loop",
        "@done:",
        ".endm",
        ".main:",
        "    WAIT 10",
        "    WAIT 20",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{
        ".main:",
        "ADD R1, R0, 10", "__WAIT_1_loop:", "SUB R1, R1, 1", "BEQ __WAIT_1_done", "JMP __WAIT_1_loop", "__WAIT_1_done:",
        "ADD R1, R0, 20", "__WAIT_2_loop:", "SUB R1, R1, 1", "BEQ __WAIT_2_done", "JMP __WAIT_2_loop", "__WAIT_2_done:",
    }));
}

void testLocalLabelsNested() {
    section("Local labels: nested macros each get their own names");
    PreprocessResult r = run({
        ".macro WAIT n",
        "    ADD R1, R0, n",
        "@loop:",
        "    SUB R1, R1, 1",
        "    BEQ @done",
        "    JMP @loop",
        "@done:",
        ".endm",
        ".macro TWICE n",
        "    WAIT n",
        "    WAIT n",
        "@end:",
        "    JMP @end",
        ".endm",
        ".main:",
        "    TWICE 5",
    });
    CHECK(r.errors.empty());

    // Every label that gets defined must be different from every other one.
    std::set<std::string> defined;
    size_t count = 0;
    for (const auto& text : textsOf(r)) {
        if (!text.empty() && text.back() == ':') { defined.insert(text); ++count; }
    }
    CHECK(count == defined.size());
    CHECK(defined.count("__TWICE_1_end:") == 1);
    CHECK(defined.count("__WAIT_2_loop:") == 1);
    CHECK(defined.count("__WAIT_3_loop:") == 1);
    CHECK(defined.count("__WAIT_2_done:") == 1);
    CHECK(defined.count("__WAIT_3_done:") == 1);
}

void testLocalLabelWithCallLineLabel() {
    section("Local labels: a call-line label and @labels together");
    PreprocessResult r = run({
        ".macro SPIN",
        "@here:",
        "    JMP @here",
        ".endm",
        ".main:",
        "top: SPIN",
        "    JMP top",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{".main:", "top:", "__SPIN_1_here:", "JMP __SPIN_1_here", "JMP top"}));
}

// ------------------------------------------------------------ built-in macros

void testPushPopExpansion() {
    section("Built-ins: PUSH and POP");
    PreprocessResult r = run({
        ".main:",
        "    PUSH R1",
        "    POP R2",
        "top: PUSH R3",
        "    push r4",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{
        ".main:",
        "SUB sp, sp, 1", "STORE R1, 0[sp]",
        "LOAD R2, 0[sp]", "ADD sp, sp, 1",
        "top:", "SUB sp, sp, 1", "STORE R3, 0[sp]",
        "SUB sp, sp, 1", "STORE r4, 0[sp]",
    }));
}

void testPushPopAreNotInstructionsAnymore() {
    section("Built-ins: PUSH/POP are macros, not mnemonics");
    CHECK(opTable().count("PUSH") == 0);
    CHECK(opTable().count("POP") == 0);
}

void testBuiltinsInsideUserMacro() {
    section("Built-ins: usable inside user macros");
    PreprocessResult r = run({
        ".macro SAVE2 a, b",
        "    PUSH a",
        "    PUSH b",
        ".endm",
        ".main:",
        "    SAVE2 R1, R2",
    });
    CHECK(r.errors.empty());
    CHECK_LINES(textsOf(r), (Lines{
        ".main:", "SUB sp, sp, 1", "STORE R1, 0[sp]", "SUB sp, sp, 1", "STORE R2, 0[sp]",
    }));
}

// ----------------------------------------------------------- line map and context

void testLineMapAndContext() {
    section("Line map: expanded lines point at the call site, with the macro chain");
    PreprocessResult r = run({
        ".macro SET a, v",              // 1
        "    ADD a, R0, v",             // 2
        ".endm",                        // 3
        ".macro INIT a",                // 4
        "    SET a, 5",                 // 5
        ".endm",                        // 6
        ".macro OUTER a",               // 7
        "    INIT a",                   // 8
        ".endm",                        // 9
        ".main:",                       // 10
        "    ADD R1, R2, R3",           // 11
        "    SET R1, 5",                // 12
        "    OUTER R3",                 // 13
        "    HALT",                     // 14
    });
    CHECK(r.errors.empty());
    auto rows = rowsOf(r);
    CHECK(rows.size() == 5);
    if (rows.size() == 5) {
        CHECK(rows[0].text == ".main:" && rows[0].origin == 10 && rows[0].context.empty());
        CHECK(rows[1].text == "ADD R1, R2, R3" && rows[1].origin == 11 && rows[1].context.empty());
        CHECK(rows[2].text == "ADD R1, R0, 5" && rows[2].origin == 12);
        CHECK(rows[2].context == "in expansion of 'SET'");
        CHECK(rows[3].text == "ADD R3, R0, 5" && rows[3].origin == 13);
        CHECK(rows[3].context == "in expansion of 'SET', called from 'INIT', called from 'OUTER'");
        CHECK(rows[4].text == "HALT" && rows[4].origin == 14 && rows[4].context.empty());
    }
}

void testErrorContext() {
    section("Line map: preprocessor errors inside nested macros name the chain");
    PreprocessResult r = run({
        ".macro SET a, v",              // 1
        "    ADD a, R0, v",             // 2
        ".endm",                        // 3
        ".macro INIT a",                // 4
        "    SET a",                    // 5   wrong argument count
        ".endm",                        // 6
        ".main:",                       // 7
        "    INIT R1",                  // 8
    });
    CHECK_ERROR(r, 8, "macro 'SET' expects 2 argument(s), got 1");
    CHECK_ERROR(r, 8, "(in expansion of 'INIT')");
}

// ------------------------------------------------------------------ errors

void testDefinitionErrors() {
    section("Errors: macro definitions");

    PreprocessResult dup = run({".macro A x", ".endm", ".macro A y", ".endm"});
    CHECK_ERROR(dup, 3, "macro 'A' is already defined on line 1");

    PreprocessResult clash = run({".macro ADD x", ".endm"});
    CHECK_ERROR(clash, 1, "clashes with an instruction mnemonic");

    PreprocessResult badName = run({".macro 9x a", ".endm"});
    CHECK_ERROR(badName, 1, "invalid macro name '9x'");

    PreprocessResult noName = run({".macro", ".endm"});
    CHECK_ERROR(noName, 1, "needs a name");

    PreprocessResult dupParam = run({".macro M a, a", ".endm"});
    CHECK_ERROR(dupParam, 1, "duplicate parameter 'a'");

    PreprocessResult regParam = run({".macro M R3, b", ".endm"});
    CHECK_ERROR(regParam, 1, "parameter 'R3' looks like a register name");

    PreprocessResult spParam = run({".macro M sp", ".endm"});
    CHECK_ERROR(spParam, 1, "parameter 'sp' looks like a register name");

    // Every register alias the assembler accepts is also refused as a parameter name.
    for (const char* alias : { "RA", "ra", "Ra", "SP", "Sp" }) {
        PreprocessResult aliasParam = run({std::string(".macro M ") + alias, ".endm"});
        CHECK_ERROR(aliasParam, 1, std::string("parameter '") + alias + "' looks like a register name");
    }

    PreprocessResult badParam = run({".macro M 1a", ".endm"});
    CHECK_ERROR(badParam, 1, "invalid parameter name '1a'");

    PreprocessResult emptyParam = run({".macro M a,,b", ".endm"});
    CHECK_ERROR(emptyParam, 1, "empty parameter name");

    PreprocessResult builtin = run({".macro PUSH x", ".endm"});
    CHECK_ERROR(builtin, 1, "macro 'PUSH' is built in and cannot be redefined");

    Lines many = {".macro BIG p0"};
    for (int i = 1; i <= 64; ++i) many.back() += ", p" + std::to_string(i);
    many.push_back(".endm");
    PreprocessResult tooMany = run(many);
    CHECK_ERROR(tooMany, 1, "too many parameters");
}

void testStructureErrors() {
    section("Errors: .macro / .endm structure");

    PreprocessResult noEnd = run({".main:", ".macro A x", "    NOP"});
    CHECK_ERROR(noEnd, 2, "'.macro A' has no matching '.endm'");

    PreprocessResult stray = run({".main:", ".endm"});
    CHECK_ERROR(stray, 2, "'.endm' without a matching '.macro'");

    PreprocessResult nested = run({".macro A x", ".macro B y", ".endm", ".endm"});
    CHECK_ERROR(nested, 2, "'.macro' inside macro 'A'");

    PreprocessResult textAfter = run({".macro A x", ".endm junk"});
    CHECK_ERROR(textAfter, 2, "unexpected text after '.endm'");

    PreprocessResult dotLabel = run({".macro A x", ".main:", "    NOP", ".endm"});
    CHECK_ERROR(dotLabel, 2, "label '.main' inside macro 'A' cannot start with '.'");
}

void testCallErrors() {
    section("Errors: macro calls");
    Lines defs = {".macro TWO a, b", "    ADD a, a, b", ".endm", ".main:"};

    auto withCall = [&](const std::string& call) {
        Lines src = defs;
        src.push_back(call);
        return run(src);
    };

    CHECK_ERROR(withCall("    TWO R1"), 5, "macro 'TWO' expects 2 argument(s), got 1");
    CHECK_ERROR(withCall("    TWO R1, R2, R3"), 5, "macro 'TWO' expects 2 argument(s), got 3");
    CHECK_ERROR(withCall("    TWO"), 5, "expects 2 argument(s), got 0");
    CHECK_ERROR(withCall("    TWO R1,,R2"), 5, "empty argument in call to 'TWO'");
    CHECK_ERROR(withCall("    TWO R1, R2,"), 5, "empty argument in call to 'TWO'");
}

void testRecursionAndSizeLimits() {
    section("Errors: recursion and runaway expansion");

    PreprocessResult self = run({".macro A x", "    A x", ".endm", ".main:", "    A R1"});
    CHECK_ERROR(self, 5, "expands too deeply");

    PreprocessResult mutual = run({".macro A x", "    B x", ".endm", ".macro B x", "    A x", ".endm", ".main:", "    A R1"});
    CHECK_ERROR(mutual, 8, "expands too deeply");

    // M1 = 10 NOPs, M2 = 10 x M1, ... M6 = 10^6 NOPs: far over the line limit.
    Lines big = {".macro M1", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", "    NOP", ".endm"};
    for (int level = 2; level <= 6; ++level) {
        big.push_back(".macro M" + std::to_string(level));
        for (int i = 0; i < 10; ++i) big.push_back("    M" + std::to_string(level - 1));
        big.push_back(".endm");
    }
    big.push_back(".main:");
    big.push_back("    M6");
    PreprocessResult huge = run(big);
    CHECK_ERROR(huge, static_cast<int>(big.size()), "macro expansion is too large");
}

void testLocalLabelErrors() {
    section("Errors: @local labels");

    PreprocessResult undefinedUse = run({".macro A", "    JMP @y", ".endm"});
    CHECK_ERROR(undefinedUse, 2, "local label '@y' is used but never defined in macro 'A'");

    PreprocessResult twice = run({".macro A", "@x:", "    NOP", "@x:", ".endm"});
    CHECK_ERROR(twice, 4, "local label '@x' is defined twice in macro 'A'");

    PreprocessResult bare = run({".macro A", "    JMP @", ".endm"});
    CHECK_ERROR(bare, 2, "invalid local label '@'");

    PreprocessResult digit = run({".macro A", "@1bad:", ".endm"});
    CHECK_ERROR(digit, 2, "invalid local label '@1bad'");

    PreprocessResult outsideUse = run({".main:", "    JMP @x"});
    CHECK_ERROR(outsideUse, 2, "local label '@x' can only be used inside a macro body");

    PreprocessResult outsideDef = run({".main:", "@top:", "    HALT"});
    CHECK_ERROR(outsideDef, 2, "local label '@top' can only be used inside a macro body");

    PreprocessResult outsideCallArg = run({".macro A x", "    ADD x, x, 1", ".endm", ".main:", "    A @y"});
    CHECK_ERROR(outsideCallArg, 5, "can only be used inside a macro body");
}

void testErrorsAreCollected() {
    section("Errors: all problems are reported together");
    PreprocessResult r = run({
        ".macro ADD x",                 // 1  clashes with a mnemonic
        ".endm",
        ".macro B a, a",                // 3  duplicate parameter
        ".endm",
        ".macro C x",
        ".endm",
        ".main:",
        "    C",                        // 8  wrong argument count
        ".endm",                        // 9  stray .endm
    });
    CHECK(r.errors.size() >= 4);
    CHECK_ERROR(r, 1, "clashes with an instruction mnemonic");
    CHECK_ERROR(r, 3, "duplicate parameter 'a'");
    CHECK_ERROR(r, 8, "expects 1 argument(s), got 0");
    CHECK_ERROR(r, 9, "'.endm' without a matching '.macro'");
}

// ------------------------------------------------------------- end to end

void testMacrosEqualHandExpanded() {
    section("End to end: macro source assembles to the same words as its hand-expanded copy");

    Lines withMacros = {
        ".macro ADD3 a, b, c",
        "    ADD a, a, b",
        "    ADD a, a, c",
        ".endm",
        ".macro WAIT n",
        "    ADD R1, R0, n",
        "@loop:",
        "    SUB R1, R1, 1",
        "    BEQ @done",
        "    JMP @loop",
        "@done:",
        ".endm",
        ".macro SAVE2 a, b",
        "    PUSH a",
        "    PUSH b",
        ".endm",
        ".main:",
        "    ADD sp, R0, 100",
        "top: ADD3 R2, R3, 7",
        "    WAIT 3",
        "    SAVE2 R2, R3",
        "    POP R4",
        "    WAIT 4",
        "    JMP top",
        "    HALT",
        ".literals:",
        "pi: 3.14f",
    };

    Lines handExpanded = {
        ".main:",
        "    ADD R15, R0, 100",
        "top:",
        "    ADD R2, R2, R3",
        "    ADD R2, R2, 7",
        "    ADD R1, R0, 3",
        "l1:",
        "    SUB R1, R1, 1",
        "    BEQ d1",
        "    JMP l1",
        "d1:",
        "    SUB R15, R15, 1",
        "    STORE R2, 0[R15]",
        "    SUB R15, R15, 1",
        "    STORE R3, 0[R15]",
        "    LOAD R4, 0[R15]",
        "    ADD R15, R15, 1",
        "    ADD R1, R0, 4",
        "l2:",
        "    SUB R1, R1, 1",
        "    BEQ d2",
        "    JMP l2",
        "d2:",
        "    JMP top",
        "    HALT",
        ".literals:",
        "pi: 3.14f",
    };

    bool ok1 = false, ok2 = false;
    std::vector<uint32_t> a = encodeAll(withMacros, ok1);
    std::vector<uint32_t> b = encodeAll(handExpanded, ok2);
    CHECK(ok1);
    CHECK(ok2);
    CHECK(!a.empty());
    CHECK(a == b);
}

void testAssemblerErrorsKeepOriginalLines() {
    section("End to end: assembler errors map back to the original file");
    Lines src = {
        ".macro TWO a, b",              // 1
        "    ADD a, a, b",              // 2
        ".endm",                        // 3
        "",                             // 4
        ".main:",                       // 5
        "    HALT",                     // 6
        "    TWO R1, R77",              // 7   bad register, inside the expansion
        "    JMP nowhere",              // 8
    };
    PreprocessResult expanded = run(src);
    CHECK(expanded.errors.empty());

    Assembler assembler;
    assembler.assemblePass1(expanded.lines);
    assembler.assemblePass2();
    CHECK(assembler.errors().size() == 2);

    // main_asm translates line N of the assembler's input through the line map.
    auto originOf = [&](int n) { return expanded.origin_line[static_cast<size_t>(n - 1)]; };
    auto contextOf = [&](int n) { return expanded.origin_context[static_cast<size_t>(n - 1)]; };
    if (assembler.errors().size() == 2) {
        CHECK(originOf(assembler.errors()[0].line_number) == 7);
        CHECK(contextOf(assembler.errors()[0].line_number) == "in expansion of 'TWO'");
        CHECK(originOf(assembler.errors()[1].line_number) == 8);
        CHECK(contextOf(assembler.errors()[1].line_number).empty());
    }
}

void testEdgeCases() {
    section("Edge cases: comments, CRLF, empty macro, label+instruction body line, operands");

    PreprocessResult comments = run({".macro INC a", "  ADD a, a, 1 ; bump", ".endm", ".main:", "  INC R1 ; call comment"});
    CHECK(comments.errors.empty());
    CHECK_LINES(textsOf(comments), (Lines{".main:", "ADD R1, R1, 1"}));

    PreprocessResult crlf = run({".macro INC a\r", "  ADD a, a, 1\r", ".endm\r", ".main:\r", "  INC R1\r"});
    CHECK(crlf.errors.empty());
    CHECK_LINES(textsOf(crlf), (Lines{".main:", "ADD R1, R1, 1"}));

    PreprocessResult empty = run({".macro NOTHING", ".endm", ".main:", "top: NOTHING", "  HALT"});
    CHECK(empty.errors.empty());
    CHECK_LINES(textsOf(empty), (Lines{".main:", "top:", "HALT"}));

    PreprocessResult same = run({".macro L n", "@l: SUB n, n, 1", "  JMP @l", ".endm", ".main:", "  L R1"});
    CHECK(same.errors.empty());
    CHECK_LINES(textsOf(same), (Lines{".main:", "__L_1_l: SUB R1, R1, 1", "JMP __L_1_l"}));

    PreprocessResult mem = run({".macro LD r, base", "  LOAD r, 0[base]", ".endm", ".main:", "  LD R1, R2"});
    CHECK(mem.errors.empty());
    CHECK_LINES(textsOf(mem), (Lines{".main:", "LOAD R1, 0[R2]"}));

    // Arguments that are spelled like parameters must not be substituted again.
    PreprocessResult swapped = run({".macro M a, b", "  ADD a, a, b", ".endm", ".main:", "  M b, a"});
    CHECK(swapped.errors.empty());
    CHECK_LINES(textsOf(swapped), (Lines{".main:", "ADD b, b, a"}));

    // A macro's own @label can be handed to a nested macro as an argument.
    PreprocessResult localArg = run({".macro INNER t", "  JMP t", ".endm", ".macro OUT", "@l:", "  INNER @l", ".endm", ".main:", "  OUT"});
    CHECK(localArg.errors.empty());
    CHECK_LINES(textsOf(localArg), (Lines{".main:", "__OUT_1_l:", "JMP __OUT_1_l"}));
}

void testNestingLimit() {
    section("Edge cases: nesting is allowed 16 levels deep, 17 is an error");
    auto chain = [](int n, int& call_line) {
        Lines src;
        for (int i = 1; i <= n; ++i) {
            src.push_back(".macro M" + std::to_string(i));
            src.push_back(i == 1 ? "  NOP" : "  M" + std::to_string(i - 1));
            src.push_back(".endm");
        }
        src.push_back(".main:");
        src.push_back("  M" + std::to_string(n));
        call_line = static_cast<int>(src.size());
        return run(src);
    };

    int line = 0;
    PreprocessResult ok = chain(16, line);
    CHECK(ok.errors.empty());
    CHECK_LINES(textsOf(ok), (Lines{".main:", "NOP"}));

    PreprocessResult tooDeep = chain(17, line);
    CHECK_ERROR(tooDeep, line, "expands too deeply");
}

void testMacroAfterLiteralsIsError() {
    section("Errors: .macro after .literals: is rejected (and not registered)");

    PreprocessResult r = run({
        ".main:",                       // 1
        "    INC R1",                   // 2   INC is only defined below, after .literals:
        "    HALT",                     // 3
        ".literals:",                   // 4
        "pi: 3.14f",                    // 5
        ".macro INC a",                 // 6
        "    ADD a, a, 1",              // 7
        ".endm",                        // 8
    });
    CHECK_ERROR(r, 6, "'.macro' is not allowed after '.literals:'");
    CHECK(r.errors.size() == 1);        // the body and '.endm' are consumed quietly: one error, not three

    // A definition before .literals: is still fine.
    PreprocessResult before = run({
        ".macro INC a", "    ADD a, a, 1", ".endm",
        ".main:", "    INC R1", ".literals:", "pi: 3.14f",
    });
    CHECK(before.errors.empty());
    CHECK_LINES(textsOf(before), (Lines{".main:", "ADD R1, R1, 1", ".literals:", "pi: 3.14f"}));
}

} // namespace

int main() {
    testFixedArity();
    testManyParameters();
    testWholeTokenSubstitution();
    testArgumentsPassThroughAsText();
    testCaseInsensitiveNames();
    testUseBeforeDefinition();
    testLabelOnCallLine();
    testNesting();
    testLiteralsUntouched();
    testNoMacrosPassThrough();
    testReusable();

    testLocalLabelsUniquePerExpansion();
    testLocalLabelsNested();
    testLocalLabelWithCallLineLabel();

    testPushPopExpansion();
    testPushPopAreNotInstructionsAnymore();
    testBuiltinsInsideUserMacro();

    testLineMapAndContext();
    testErrorContext();

    testDefinitionErrors();
    testStructureErrors();
    testCallErrors();
    testRecursionAndSizeLimits();
    testLocalLabelErrors();
    testErrorsAreCollected();

    testMacrosEqualHandExpanded();
    testAssemblerErrorsKeepOriginalLines();

    testEdgeCases();
    testNestingLimit();

    testMacroAfterLiteralsIsError();

    if (failures == 0) {
        std::cout << "\nAll preprocessor tests passed\n";
        return 0;
    }
    std::cerr << "\n" << failures << " check(s) FAILED\n";
    return 1;
}