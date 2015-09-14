#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>

#ifndef HAVE_LLVM
#error "This code needs LLVM enabled"
#endif

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/IRReader/IRReader.h>

using namespace llvm;

static void get_lines_from_module(const Module *M, std::set<unsigned>& lines)
{
    // iterate over all instructions
    for (const Function& F : *M) {
        for (const BasicBlock& B : F) {
            for (const Instruction& I : B) {
                const DebugLoc& Loc = I.getDebugLoc();
                lines.insert(Loc.getLine());
            }
        }
    }

    // iterate over all globals
    /*
    for (const GlobalVariable& G : M->globals()) {
        const DebugLoc& Loc = G.getDebugLoc();
        lines.insert(Loc.getLine());
    }
    */
}

static bool should_print(const char *buf, unsigned linenum,
                         std::set<unsigned>& lines)
{
    static unsigned bnum = 0;
    std::istringstream ss(buf);
    char c;

    ss >> std::skipws >> c;
    if (c == '{')
        ++bnum;
    else if (c == '}')
        --bnum;

    // bnum == 1 means we're in function
    if (bnum == 1) {
        // opening bracket
        if (c == '{')
            return true;

        // empty line
        if (*buf == '\n')
            return true;
    }

    // closing bracket
    if (bnum == 0 && c == '}')
        return true;

    if (lines.count(linenum) != 0)
        return true;

    return false;
}

static void print_lines(std::ifstream& ifs, std::set<unsigned>& lines)
{
    char buf[1024];
    unsigned cur_line = 1;
    while (!ifs.eof()) {
        ifs.getline(buf, sizeof buf);

        if (should_print(buf, cur_line, lines)) {
            std::cout << cur_line << ": ";
            std::cout << buf << "\n";
        }

        if (ifs.bad()) {
            errs() << "An error occured\n";
            break;
        }

        ++cur_line;
    }
}

int main(int argc, char *argv[])
{
    LLVMContext context;
    SMDiagnostic SMD;
    std::unique_ptr<Module> M;

    const char *source = NULL;
    const char *module = NULL;

    if (argc != 3) {
        errs() << "Usage: source_code module\n";
        return 1;
    }

    source = argv[1];
    module = argv[2];

    M = parseIRFile(module, SMD, context);
    if (!M) {
        SMD.print(argv[0], errs());
        return 1;
    }

    // FIXME find out if we have debugging info at all

    std::ifstream ifs(source);
    if (!ifs.is_open() || ifs.bad()) {
        errs() << "Failed opening given source file: " << source << "\n";
        return 1;
    }

    // no difficult machineris - just find out
    // which lines are in our module and print them
    std::set<unsigned> lines;
    get_lines_from_module(&*M, lines);

    print_lines(ifs, lines);
    ifs.close();

    return 0;
}