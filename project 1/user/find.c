#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *name) {
    int fd;
    struct stat st;
    struct dirent de;
    char buf[512];
    int plen, nlen;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if (st.type != T_DIR) {
        close(fd);
        return;
    }

    plen = strlen(path);

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        // Get actual name length (de.name may not be null-terminated)
        nlen = 0;
        while (nlen < DIRSIZ && de.name[nlen]) nlen++;

        // Build full path
        if (plen + 1 + nlen + 1 > (int)sizeof(buf)) continue;
        memmove(buf, path, plen);
        buf[plen] = '/';
        memmove(buf + plen + 1, de.name, nlen);
        buf[plen + 1 + nlen] = 0;

        // Null-terminated name for comparison
        char entry_name[DIRSIZ + 1];
        memmove(entry_name, de.name, nlen);
        entry_name[nlen] = 0;

        struct stat st2;
        if (stat(buf, &st2) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }

        if (st2.type == T_DIR) {
            // Recurse into subdirectory
            find(buf, name);
        } else {
            // Print path if name matches
            if (strcmp(entry_name, name) == 0) {
                printf("%s\n", buf);
            }
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: find <dir> <name>\n");
        exit(1);
    }
    find(argv[1], argv[2]);
    exit(0);
}