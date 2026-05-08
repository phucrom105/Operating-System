#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void tree(char *path, int depth) {
    int fd;
    struct stat st;
    struct dirent de;
    char buf[512];
    int len;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "tree: cannot open %s\n", path);
        return;
    }
    if (fstat(fd, &st) < 0) {
        fprintf(2, "tree: cannot stat %s\n", path);
        close(fd);
        return;
    }
    if (st.type != T_DIR) {
        close(fd);
        return;
    }

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        // Print indentation (2 spaces per depth level)
        for (int i = 0; i < depth; i++) printf("  ");

        // Build full path: path + "/" + name
        len = strlen(path);
        memset(buf, 0, sizeof(buf));
        memmove(buf, path, len);
        if (len > 0 && buf[len - 1] != '/') {
            buf[len] = '/';
            len++;
        }
        memmove(buf + len, de.name, strlen(de.name));

        // Stat the entry
        struct stat st2;
        if (stat(buf, &st2) < 0) {
            fprintf(2, "tree: cannot stat %s\n", buf);
            // Print name anyway and continue
            printf("%s\n", de.name);
            continue;
        }

        if (st2.type == T_DIR) {
            printf("%s/\n", de.name);
            tree(buf, depth + 1);
        } else {
            printf("%s\n", de.name);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    char *path = (argc < 2) ? "." : argv[1];

    // Check if path can be opened; exit on failure for root path
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "tree: cannot open %s\n", path);
        exit(1);
    }
    close(fd);

    printf("%s/\n", path);
    tree(path, 1);
    exit(0);
}