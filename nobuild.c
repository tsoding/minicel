#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define CFLAGS "-Wall", "-Wextra", "-std=c11", "-pedantic", "-ggdb"
#define CSV_FILE_PATH "./csv/stress-copy.csv"

const char *cc(void)
{
    const char *result = getenv("CC");
    return result ? result : "cc";
}

int posix_main(int argc, char **argv)
{
    CMD(cc(), CFLAGS, "-o", "minicel", "src/main.c");

    if (argc > 1) {
        if (strcmp(argv[1], "run") == 0) {
            CMD("./minicel", CSV_FILE_PATH);
        } else if (strcmp(argv[1], "gdb") == 0) {
            CMD("gdb", "./minicel");
        } else if (strcmp(argv[1], "valgrind") == 0) {
            CMD("valgrind", "./minicel", CSV_FILE_PATH);
        } else {
            PANIC("%s is unknown subcommand", argv[1]);
        }
    }

    return 0;
}

int win32_main(int argc, char **argv)
{
    CMD("cl.exe", "/Feminicel", "src/main.c");
    if (argc > 1) {
        if (strcmp(argv[1], "run") == 0) {
            CMD(".\\minicel.exe", CSV_FILE_PATH);
        } else {
            PANIC("%s is unknown subcommand", argv[1]);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

#ifndef _WIN32
    return posix_main(argc, argv);
#else
    return win32_main(argc, argv);
#endif
}
