// radar_step2.c
//
// Combines: process discovery + remote memory reading (process_vm_readv)
// + a raylib window showing YOUR player as a rotating blue triangle,
// with health/position/yaw also printed as text.
//
// No bots yet -- this is "prove the whole chain works end to end" for
// just player1 first.
//
// This binary is NON-PIE (no ASLR), so nm's address is already the
// real runtime address -- no base-address lookup needed at all.
//
// === BEFORE COMPILING ===
// Run this on your real binary and fill in PLAYER1_ADDR below:
//     nm ~/Desktop/assaultcube/bin_unix/linux_64_client | grep " player1$"
// It prints something like:  0000000000603ca8 B player1
// That whole hex number IS the runtime address (prefix with 0x).
//
// === COMPILE ===
//   gcc radar_step2.c -o radar_step2 -lraylib -lm -lpthread -ldl
//
// === RUN ===
//   sudo ./radar_step2
// (sudo needed for process_vm_readv permissions, same reason gdb needed it)

#define _GNU_SOURCE
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <math.h>

// ==================== FILL THIS IN ====================
// From: nm bin_unix/linux_64_client | grep " player1$"
// This is the FULL runtime address already (non-PIE binary).
#define PLAYER1_ADDR 0x5a3518

// The process name to search for in /proc
#define PROCESS_NAME "linux_64_client"

// ==================== VERIFIED OFFSETS (from your gdb session) ====================
#define HEALTH_OFFSET 0x100   // 256
#define O_OFFSET      0x8     // position (vec: x,y,z as 3 floats)
#define YAW_OFFSET    0x38    // 56

#define WINDOW_SIZE 400

// ---------------------------------------------------------------------
// Find a running process's PID by matching /proc/<pid>/comm
// ---------------------------------------------------------------------
pid_t find_pid_by_name(const char *name) {
    DIR *proc = opendir("/proc");
    if (!proc) return -1;

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        // Only care about numeric directories (i.e. PIDs)
        if (!isdigit(entry->d_name[0])) continue;

        char comm_path[300];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);

        FILE *f = fopen(comm_path, "r");
        if (!f) continue;

        char comm[256] = {0};
        fgets(comm, sizeof(comm), f);
        fclose(f);

        // strip trailing newline
        comm[strcspn(comm, "\n")] = 0;

        if (strcmp(comm, name) == 0) {
            closedir(proc);
            return (pid_t)atoi(entry->d_name);
        }
    }
    closedir(proc);
    return -1;
}

// ---------------------------------------------------------------------
// Read `len` bytes from another process's memory at `remote_addr`
// ---------------------------------------------------------------------
ssize_t read_remote(pid_t pid, uint64_t remote_addr, void *local_buf, size_t len) {
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = local_buf;
    local[0].iov_len = len;

    remote[0].iov_base = (void *)remote_addr;
    remote[0].iov_len = len;

    return process_vm_readv(pid, local, 1, remote, 1, 0);
}

// ---------------------------------------------------------------------
// Small struct to hold what we read each frame
// ---------------------------------------------------------------------
typedef struct {
    int health;
    float x, y, z;
    float yaw;
    int valid; // 0 if the read failed this frame
} PlayerData;

PlayerData read_player_data(pid_t pid, uint64_t player1_static_addr) {
    PlayerData pd = {0};

    // Dereference #1: player1 is a pointer variable itself.
    // Reading 8 bytes at its address gives us the heap address
    // of the actual playerent object.
    uint64_t heap_addr = 0;
    ssize_t n = read_remote(pid, player1_static_addr, &heap_addr, sizeof(heap_addr));
    if (n != sizeof(heap_addr)) {
        printf("read_remote FAILED: returned %zd, errno=%d (%s)\n", n, errno, strerror(errno));
        printf("  pid=%d  addr=0x%lx\n", pid, player1_static_addr);
        return pd; // valid stays 0
    }
    printf("read_remote OK: heap_addr = 0x%lx\n", heap_addr);
    if (heap_addr == 0) return pd;

    // Dereference #2 (x3): read each field at heap_addr + offset
    int health = 0;
    float pos[3] = {0};
    float yaw = 0;

    read_remote(pid, heap_addr + HEALTH_OFFSET, &health, sizeof(health));
    read_remote(pid, heap_addr + O_OFFSET, pos, sizeof(pos)); // x,y,z all at once, contiguous
    read_remote(pid, heap_addr + YAW_OFFSET, &yaw, sizeof(yaw));

    pd.health = health;
    pd.x = pos[0];
    pd.y = pos[1];
    pd.z = pos[2];
    pd.yaw = yaw;
    pd.valid = 1;

    return pd;
}

int main(void) {
    // --- Step 1: find the game process ---
    pid_t game_pid = find_pid_by_name(PROCESS_NAME);
    if (game_pid < 0) {
        printf("Could not find process '%s'. Is the game running?\n", PROCESS_NAME);
        return 1;
    }
    printf("Found %s at PID %d\n", PROCESS_NAME, game_pid);

    uint64_t player1_addr = PLAYER1_ADDR;
    printf("player1 address: 0x%lx\n", player1_addr);

    // --- Step 2: open the raylib window ---
    InitWindow(WINDOW_SIZE, WINDOW_SIZE, "AC Radar - Player Only");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        PlayerData pd = read_player_data(game_pid, player1_addr);

        BeginDrawing();
        ClearBackground(BLACK);

        if (pd.valid) {
            // Draw the blue triangle at the center, rotated by our yaw.
            // AC's yaw: 0 = facing one axis; we just use it directly as
            // a rotation angle for now, adjust visually once you see it.
            Vector2 center = { WINDOW_SIZE / 2.0f, WINDOW_SIZE / 2.0f };
            float size = 15.0f;
            float angle_rad = pd.yaw * DEG2RAD;

            // Triangle points relative to center, before rotation:
            // tip points "up" (negative Y in screen space)
            Vector2 tip    = { 0, -size };
            Vector2 left   = { -size * 0.7f, size * 0.6f };
            Vector2 right  = { size * 0.7f, size * 0.6f };

            // Rotate each point by angle_rad, then translate to center
            Vector2 pts[3] = { tip, left, right };
            for (int i = 0; i < 3; i++) {
                float rx = pts[i].x * cosf(angle_rad) - pts[i].y * sinf(angle_rad);
                float ry = pts[i].x * sinf(angle_rad) + pts[i].y * cosf(angle_rad);
                pts[i].x = center.x + rx;
                pts[i].y = center.y + ry;
            }

            DrawTriangle(pts[0], pts[1], pts[2], BLUE);

            // Debug text
            DrawText(TextFormat("Health: %d", pd.health), 10, 10, 20, WHITE);
            DrawText(TextFormat("Pos: %.1f, %.1f, %.1f", pd.x, pd.y, pd.z), 10, 35, 20, WHITE);
            DrawText(TextFormat("Yaw: %.1f", pd.yaw), 10, 60, 20, WHITE);
        } else {
            DrawText("Failed to read player data", 10, 10, 20, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}