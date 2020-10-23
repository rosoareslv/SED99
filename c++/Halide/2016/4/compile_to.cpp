#include "Halide.h"
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

using namespace Halide;

void testCompileToOutput(Func j) {
    const char *fn_object = "compile_to_native.o";

    #ifndef _MSC_VER
    if (access(fn_object, F_OK) == 0) { unlink(fn_object); }
    assert(access(fn_object, F_OK) != 0 && "Output file already exists.");
    #endif

    std::vector<Argument> empty_args;
    j.compile_to(Outputs().object(fn_object), empty_args, "");

    #ifndef _MSC_VER
    assert(access(fn_object, F_OK) == 0 && "Output file not created.");
    #endif
}

void testCompileToOutputAndAssembly(Func j) {
    const char *fn_object = "compile_to_native1.o";
    const char *fn_assembly = "compile_to_assembly1.s";

    #ifndef _MSC_VER
    if (access(fn_object, F_OK) == 0) { unlink(fn_object); }
    if (access(fn_assembly, F_OK) == 0) { unlink(fn_assembly); }
    assert(access(fn_object, F_OK) != 0 && "Output file already exists.");
    assert(access(fn_assembly, F_OK) != 0 && "Assembly file already exists.");
    #endif

    std::vector<Argument> empty_args;
    j.compile_to(Outputs().object(fn_object).assembly(fn_assembly), empty_args, "");

    #ifndef _MSC_VER
    assert(access(fn_object, F_OK) == 0 && "Output file not created.");
    assert(access(fn_assembly, F_OK) == 0 && "Assembly file not created.");
    #endif
}

int main(int argc, char **argv) {
    Func f, g, h, j;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x+1, y));
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    testCompileToOutput(j);

    testCompileToOutputAndAssembly(j);

    printf("Success!\n");
    return 0;
}
