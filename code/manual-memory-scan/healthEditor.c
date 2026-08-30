#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <dirent.h>
#include <stdint.h>   // Defines uintptr_t

// pointer chain values we found
#define PLAYER1_STATIC 0x5a3518
#define HEALTH_OFFSET  0x100

pid_t find_pid(const char *name) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // only look at numeric directories
        if (entry->d_name[0] < '0' || entry->d_name[9] > '9') continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char comm[256];
        fgets(comm, sizeof(comm), f);
        fclose(f);

        // strip newline
        comm[strcspn(comm, "\n")] = 0;

        if (strcmp(comm, name) == 0) {
            closedir(dir);
            return atoi(entry->d_name);
        }
    }

    closedir(dir);
    return -1;
}

int main() {
    pid_t pid = find_pid("linux_64_client");
    if (pid == -1) {
        printf("could not find assaultcube process\n");
        return 1;
    }
    printf("found pid: %d\n", pid);

    // step 1: read the pointer at the static player1 address
    uintptr_t playerent_addr = 0;
    struct iovec local  = { &playerent_addr, sizeof(playerent_addr) };
    struct iovec remote = { (void *)PLAYER1_STATIC, sizeof(playerent_addr) };

    if (process_vm_readv(pid, &local, 1, &remote, 1, 0) == -1) {
        perror("process_vm_readv failed reading player1 pointer");
        return 1;
    }
    printf("playerent address: 0x%p\n", playerent_addr);

    // step 2: write 999 to health at playerent + 0x100
    int health = 999;
    local.iov_base  = &health;
    local.iov_len   = sizeof(health);
    remote.iov_base = (void *)(playerent_addr + HEALTH_OFFSET);
    remote.iov_len  = sizeof(health);

    if (process_vm_writev(pid, &local, 1, &remote, 1, 0) == -1) {
        perror("process_vm_writev failed writing health");
        return 1;
    }

    printf("health set to 999\n");
    return 0;
}
