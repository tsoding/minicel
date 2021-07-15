#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define CFLAGS "-Wall", "-Wextra", "-std=c11", "-pedantic", "-ggdb"

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    // CMD("clang", CFLAGS, "-fsanitize=memory", "-o", "minicel", "src/main.c");
    CMD("gcc", CFLAGS, "-o", "minicel", "src/main.c");

    if (argc > 1) {
        if (strcmp(argv[1], "run") == 0) {
            CMD("./minicel", "./csv/stress-copy.csv");
        } else if (strcmp(argv[1], "gdb") == 0) {
            CMD("gdb", "./minicel");
        } else {
            PANIC("%s is unknown subcommand", argv[1]);
        }
    }

    return 0;
}
