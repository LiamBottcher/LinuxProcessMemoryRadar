#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/uio.h>

#define PROCESS_NAME "linux_64_client"

pid_t find_process(const char *name)
{
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *entry;

    while ((entry = readdir(dir))) {
        char *end;
        long pid = strtol(entry->d_name, &end, 10);

        if (*end) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char comm[256];

        if (fgets(comm, sizeof(comm), f)) {
            comm[strcspn(comm, "\n")] = 0;

            if (!strcmp(comm, name)) {
                fclose(f);
                closedir(dir);
                return pid;
            }
        }

        fclose(f);
    }

    closedir(dir);
    return -1;
}

uintptr_t find_base(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[1024];
    uintptr_t base;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, PROCESS_NAME)) {
            sscanf(line, "%lx", &base);
            fclose(f);
            return base;
        }
    }

    fclose(f);
    return 0;
}

uintptr_t read_ptr(pid_t pid, uintptr_t addr)
{
    uintptr_t value;

    struct iovec local = { &value, sizeof(value) };
    struct iovec remote = { (void *)addr, sizeof(value) };

    if (process_vm_readv(pid, &local, 1, &remote, 1, 0)
        != sizeof(value))
        return 0;

    return value;
}

int main(void)
{
    pid_t pid = find_process(PROCESS_NAME);
    if (pid < 0) {
        printf("Process not found\n");
        return 1;
    }

    uintptr_t base = find_base(pid);

    if (!base) {
        printf("Module not found\n");
        return 1;
    }

    // linux_64_client+0x19DFE8 -> 0x680 -> 0x14 -> 0x6C0 -> 0x600 -> 0x74C

    uintptr_t addr = read_ptr(pid, base + 0x19DFE8);
    addr = read_ptr(pid, addr + 0x680);
    addr = read_ptr(pid, addr + 0x14);
    addr = read_ptr(pid, addr + 0x6C0);
    addr = read_ptr(pid, addr + 0x600);

    int ammo;

    struct iovec local = { &ammo, sizeof(ammo) };
    struct iovec remote = { (void *)(addr + 0x74C), sizeof(ammo) };

    process_vm_readv(pid, &local, 1, &remote, 1, 0);

    printf("Ammo: %d\n", ammo);

    return 0;
}
