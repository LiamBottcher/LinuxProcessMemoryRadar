#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/uio.h>

unsigned long read_ptr(pid_t pid, unsigned long addr)
{
    unsigned long value = 0;
    struct iovec local = { &value, sizeof(value) };
    struct iovec remote = { (void *)addr, sizeof(value) };

    process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return value;
}

float read_float(pid_t pid, unsigned long addr)
{
    float value = 0;
    struct iovec local = { &value, sizeof(value) };
    struct iovec remote = { (void *)addr, sizeof(value) };

    process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return value;
}

pid_t find_pid(void)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type != DT_DIR)
            continue;

        pid_t pid = atoi(entry->d_name);
        if (pid <= 0)
            continue;

        char path[64];
        char name[256];

        snprintf(path, sizeof(path), "/proc/%d/comm", pid);

        FILE *file = fopen(path, "r");
        if (!file)
            continue;

        if (fgets(name, sizeof(name), file))
        {
            name[strcspn(name, "\n")] = 0;

            if (strcmp(name, "linux_64_client") == 0)
            {
                fclose(file);
                closedir(dir);
                return pid;
            }
        }

        fclose(file);
    }

    closedir(dir);
    return -1;
}

unsigned long find_module_base(pid_t pid)
{
    char path[64];
    char line[512];

    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *file = fopen(path, "r");
    if (!file)
        return 0;

    while (fgets(line, sizeof(line), file))
    {
        if (strstr(line, "linux_64_client"))
        {
            unsigned long base;
            sscanf(line, "%lx-", &base);
            fclose(file);
            return base;
        }
    }

    fclose(file);
    return 0;
}

int main(void)
{
    pid_t pid = find_pid();

    if (pid == -1)
    {
        printf("AssaultCube not found.\n");
        return 1;
    }

    unsigned long base = find_module_base(pid);

    if (!base)
    {
        printf("linux_64_client base not found.\n");
        return 1;
    }

    unsigned long p = base + 0x19E280;

    p = read_ptr(pid, p + 0x670);
    p = read_ptr(pid, p + 0xD0);
    p = read_ptr(pid, p + 0x768);
    p = read_ptr(pid, p + 0x7E8);

    float x = read_float(pid, p + 0x4E4);
    float y = read_float(pid, p + 0x4E8);

    printf("X: %.2f\n", x);
    printf("Y: %.2f\n", y);

    return 0;
}
