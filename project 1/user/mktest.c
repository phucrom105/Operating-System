#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Tạo các file text để test xargs + grep + find
int main(void) {
    int fd;

    // Tạo a.c
    fd = open("a.c", O_CREATE | O_WRONLY);
    write(fd, "int main() { return 0; }\n", 25);
    close(fd);

    // Tạo b.c
    fd = open("b.c", O_CREATE | O_WRONLY);
    write(fd, "void foo() { return; }\n", 23);
    close(fd);

    // Tạo c.c
    fd = open("c.c", O_CREATE | O_WRONLY);
    write(fd, "int main(void) { exit(0); }\n", 28);
    close(fd);

    printf("Created: a.c b.c c.c\n");
    printf("a.c: int main() { return 0; }\n");
    printf("b.c: void foo() { return; }\n");
    printf("c.c: int main(void) { exit(0); }\n");
    exit(0);
}