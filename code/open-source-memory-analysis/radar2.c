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

// ==================== players vector (from nm + gdb ptype /o) ====================
// players is a global vector<playerent*> -- NOT a playerent itself.
// Real runtime address (non-PIE binary):
#define PLAYERS_ADDR 0x5a3520

// Internal layout of vector<playerent*>, confirmed via:
//   (gdb) ptype /o players
#define VEC_BUF_OFFSET  0    // playerent** buf   -- pointer to the array
#define VEC_ALEN_OFFSET 8    // int alen          -- allocated capacity
#define VEC_ULEN_OFFSET 12   // int ulen          -- actual used length (loop up to this)

#define MAX_BOTS 64  // safety cap in case ulen is ever garbage

// The one remaining unknown: how AC's world x/y axes relate to its yaw
// angle convention. Since position and facing now share the exact same
// angle formula, only a 90-degree-increment offset can be wrong. Try
// 0, then 90, 180, 270 if bots still appear rotated relative to your cone.
#define POSITION_ROTATION_OFFSET_DEG 90.0f

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

// ---------------------------------------------------------------------
// Read fields out of a playerent object we already have the heap
// address for. Reused for both player1 and every bot -- same offsets
// apply to bots too, since botent inherits playerent with no reordering.
// ---------------------------------------------------------------------
PlayerData read_playerent_fields(pid_t pid, uint64_t heap_addr) {
    PlayerData pd = {0};
    if (heap_addr == 0) return pd;

    int health = 0;
    float pos[3] = {0};
    float yaw = 0;

    read_remote(pid, heap_addr + HEALTH_OFFSET, &health, sizeof(health));
    read_remote(pid, heap_addr + O_OFFSET, pos, sizeof(pos));
    read_remote(pid, heap_addr + YAW_OFFSET, &yaw, sizeof(yaw));

    pd.health = health;
    pd.x = pos[0];
    pd.y = pos[1];
    pd.z = pos[2];
    pd.yaw = yaw;
    pd.valid = 1;

    return pd;
}

PlayerData read_player_data(pid_t pid, uint64_t player1_static_addr) {
    // Dereference #1: player1 is a pointer variable itself.
    // Reading 8 bytes at its address gives us the heap address
    // of the actual playerent object.
    uint64_t heap_addr = 0;
    ssize_t n = read_remote(pid, player1_static_addr, &heap_addr, sizeof(heap_addr));
    if (n != sizeof(heap_addr)) {
        PlayerData pd = {0};
        return pd;
    }
    return read_playerent_fields(pid, heap_addr);
}

// ---------------------------------------------------------------------
// Read the bot list out of the global `players` vector.
// Two dereferences: vector -> buf array -> each playerent* -> fields.
// Fills `out` with up to MAX_BOTS entries, returns how many were read.
// ---------------------------------------------------------------------
int read_bots(pid_t pid, uint64_t players_static_addr, PlayerData *out) {
    // Read the vector's buf pointer and ulen (used length) directly
    // from the static struct -- no extra dereference needed for these
    // two fields since `players` itself is a static global struct.
    uint64_t buf_ptr = 0;
    int ulen = 0;

    read_remote(pid, players_static_addr + VEC_BUF_OFFSET, &buf_ptr, sizeof(buf_ptr));
    read_remote(pid, players_static_addr + VEC_ULEN_OFFSET, &ulen, sizeof(ulen));

    if (buf_ptr == 0 || ulen <= 0) return 0;
    if (ulen > MAX_BOTS) ulen = MAX_BOTS; // safety cap

    int count = 0;
    for (int i = 0; i < ulen; i++) {
        // Dereference #1: read the i-th playerent* out of the array
        uint64_t bot_ptr = 0;
        ssize_t n = read_remote(pid, buf_ptr + (i * sizeof(uint64_t)), &bot_ptr, sizeof(bot_ptr));
        if (n != sizeof(bot_ptr) || bot_ptr == 0) continue; // empty/stale slot

        // Dereference #2: read that bot's actual fields
        PlayerData pd = read_playerent_fields(pid, bot_ptr);
        if (pd.valid) {
            out[count++] = pd;
        }
    }

    return count;
}



// ---------------------------------------------------------------------
// Draw one radar blip as a "cone": a colored triangle whose pointed tip
// sits at the entity's exact position (tucked under the white circle)
// and whose flat side flares outward in the direction they're facing.
// A small white circle sits on top at the tip, marking the exact position.
// ---------------------------------------------------------------------
void draw_blip(Vector2 center, float angle_rad, Color color,
               float circle_radius, float cone_length, float cone_half_width) {
    // Facing direction as a unit vector. angle_rad=0 means "up" on screen,
    // matching the same rotation convention used elsewhere in this file.
    Vector2 dir = { sinf(angle_rad), -cosf(angle_rad) };

    // Perpendicular to dir, used for the flat base's width
    Vector2 perp = { -dir.y, dir.x };

    Vector2 tip = center; // the point -- exactly at the entity's position

    Vector2 base_center = { center.x + dir.x * cone_length,
                             center.y + dir.y * cone_length };
    Vector2 left  = { base_center.x + perp.x * cone_half_width,
                       base_center.y + perp.y * cone_half_width };
    Vector2 right = { base_center.x - perp.x * cone_half_width,
                       base_center.y - perp.y * cone_half_width };

    DrawTriangle(tip, left, right, color);
    DrawCircleV(center, circle_radius, WHITE); // drawn last, covers the tip
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
    uint64_t players_addr = PLAYERS_ADDR;
    printf("player1 address: 0x%lx\n", player1_addr);
    printf("players address: 0x%lx\n", players_addr);

    // --- Step 2: open the raylib window ---
    InitWindow(WINDOW_SIZE, WINDOW_SIZE, "AC Radar");
    SetTargetFPS(60);

    // How many game units of distance = 1 radar pixel. Tune by feel.
    const float SCALE = 1.2f;
    const float RADAR_RADIUS = WINDOW_SIZE / 2.0f - 20.0f; // keep dots inside the window

    PlayerData bots[MAX_BOTS];

    while (!WindowShouldClose()) {
        PlayerData pd = read_player_data(game_pid, player1_addr);
        int bot_count = 0;
        if (pd.valid) {
            bot_count = read_bots(game_pid, players_addr, bots);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        if (pd.valid) {
            Vector2 center = { WINDOW_SIZE / 2.0f, WINDOW_SIZE / 2.0f };
            float size = 15.0f;
            float angle_rad = pd.yaw * DEG2RAD;

            // --- Draw each bot first (so player blip stays on top) ---
            for (int i = 0; i < bot_count; i++) {
                // Relative position: how far is this bot from me, in game units.
                float dx = bots[i].x - pd.x;
                float dy = bots[i].y - pd.y;
                float mag = sqrtf(dx * dx + dy * dy);

                // Bearing angle from raw world offset, using the exact same
                // angle->screen formula as the facing cones (dir = sin/-cos
                // below). This guarantees position and facing share one
                // consistent convention -- the only unknown left is a
                // 90-degree-increment offset, tuned via the constant above.
                float bearing_rad = atan2f(dy, dx) + (POSITION_ROTATION_OFFSET_DEG * DEG2RAD);

                Vector2 dir = { sinf(bearing_rad), -cosf(bearing_rad) };

                float radar_dist = mag * SCALE;
                if (radar_dist > RADAR_RADIUS) radar_dist = RADAR_RADIUS; // clamp far bots to the edge

                Vector2 bot_center = { center.x + dir.x * radar_dist,
                                        center.y + dir.y * radar_dist };

                float bot_angle_rad = bots[i].yaw * DEG2RAD; // world-locked, no player-yaw adjustment

                draw_blip(bot_center, bot_angle_rad, RED, 3.5f, 22.0f, 10.0f);
            }

            // --- Draw player (always dead center) ---
            draw_blip(center, angle_rad, BLUE, 4.0f, 24.0f, 11.0f);

            // Debug text
            DrawText(TextFormat("Health: %d", pd.health), 10, 10, 20, WHITE);
            DrawText(TextFormat("Pos: %.1f, %.1f, %.1f", pd.x, pd.y, pd.z), 10, 35, 20, WHITE);
            DrawText(TextFormat("Yaw: %.1f", pd.yaw), 10, 60, 20, WHITE);
            DrawText(TextFormat("Bots: %d", bot_count), 10, 85, 20, WHITE);
        } else {
            DrawText("Failed to read player data", 10, 10, 20, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
