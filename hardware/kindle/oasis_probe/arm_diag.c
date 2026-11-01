#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    write(1, "DIAG 1: raw syscall write OK\n", 29);
    printf("DIAG 2: libc printf OK\n");
    fflush(stdout);
    return 42;
}
