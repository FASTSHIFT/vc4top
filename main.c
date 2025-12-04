/*
    vc4top show utilization of Broadcom's VC4 GPU.
    Copyright (C) 2017 Jonas Pfeil

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include "vc4_defines.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* Configuration (in milliseconds) */
static int sampling_interval_ms = 100;
static int update_interval_ms = 500;

static volatile uint32_t* base = NULL;
static volatile int running = 1;

struct counter_status {
    int idle_count;
    long long counts[16];
    double timediff;
};

static void counter_status_sub(struct counter_status* result,
    const struct counter_status* a,
    const struct counter_status* b)
{
    int i;
    result->idle_count = a->idle_count - b->idle_count;
    result->timediff = a->timediff - b->timediff;
    for (i = 0; i < 16; ++i) {
        result->counts[i] = a->counts[i] - b->counts[i];
    }
}

static void enable_counter(int id)
{
    uint32_t oldval = VC4_READ(base, V3D_PCTRE);
    VC4_WRITE(base, V3D_PCTRE, oldval | (1 << id));
}

static void set_counter_source(int counter, int source)
{
    VC4_WRITE(base, V3D_PCTRS0 + 8 * counter, source);
}

static uint32_t get_counter(int counter)
{
    return VC4_READ(base, V3D_PCTR0 + 8 * counter);
}

static void clear_counter(int counter)
{
    VC4_WRITE(base, V3D_PCTRC, 1 << counter);
}

/* Detected by observation, if the GPU is gated, deadbeef is returned */
#define POWEROFF_VALUE 0xdeadbeef
#define ACTIVE_COUNTERS 7

static void start_counters(void)
{
    int i;
    /* Activate counter mechanism */
    enable_counter(31);
    for (i = 0; i < ACTIVE_COUNTERS; ++i) {
        enable_counter(i);
        clear_counter(i);
        set_counter_source(i, 13 + i);
    }
}

static void signal_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* Get current time in seconds */
static double get_time_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

/* Sleep for specified milliseconds */
static void sleep_ms(int ms)
{
    usleep(ms * 1000);
}

/* Print a simple progress bar */
static void print_bar(const char* label, double percent, int width)
{
    int filled, i;

    filled = (int)(percent / 100.0 * width);
    if (filled < 0)
        filled = 0;
    if (filled > width)
        filled = width;

    printf("%-12s [", label);
    for (i = 0; i < width; ++i) {
        if (i < filled)
            printf("#");
        else
            printf(".");
    }
    printf("] %5.1f %%\n", percent);
}

static void print_stats(const struct counter_status* diff)
{
    double timediff = diff->timediff;

    /* counts[0] = Idle cycles, counts[1-6] = active cycles of different types */
    long long idle_cycles = diff->counts[0];
    long long vertex_cycles = diff->counts[1];
    long long fragment_cycles = diff->counts[2];
    long long valid_cycles = diff->counts[3];
    long long tmu_stall = diff->counts[4];
    long long sb_stall = diff->counts[5];
    long long vary_stall = diff->counts[6];

    /* Total cycles = idle + all active types */
    double total_cycles = (double)(idle_cycles + vertex_cycles + fragment_cycles + valid_cycles + tmu_stall + sb_stall + vary_stall);

    /* Calculate how much time GPU was powered off (returning 0xDEADBEEF) */
    double poweroff_ratio = (double)diff->idle_count * sampling_interval_ms / update_interval_ms;

    double idle_pct, vertex_pct, fragment_pct;
    double valid_pct, stall_tmu_pct, stall_sb_pct, stall_vary_pct;
    double frequency;

    /* Clear screen and move cursor to top */
    printf("\033[2J\033[H");

    /* Header */
    printf("============================================\n");
    printf("       VC4 GPU Performance Monitor          \n");
    printf("============================================\n");

    if (total_cycles < 1) {
        /* GPU was completely powered off */
        printf("  GPU Status: POWERED OFF (idle)\n");
        printf("  Power-off samples: %d / %d\n", diff->idle_count,
            (int)(update_interval_ms / sampling_interval_ms));
        printf("============================================\n");
        printf("Press Ctrl+C to exit\n");
        fflush(stdout);
        return;
    }

    /* Estimate frequency from valid instruction cycles
     * VC4 QPU runs at ~250-400MHz typically
     * valid_cycles represents cycles where QPUs executed instructions
     * This is an approximation based on QPU utilization */
    frequency = valid_cycles / timediff / 1e6; /* Convert to MHz */

    /* If we have very few cycles, GPU might be mostly idle */
    if (frequency < 0.1) {
        frequency = 0;
    }

    /* Calculate percentages based on total cycles */
    idle_pct = idle_cycles / total_cycles * 100.0;
    vertex_pct = vertex_cycles / total_cycles * 100.0;
    fragment_pct = fragment_cycles / total_cycles * 100.0;
    valid_pct = valid_cycles / total_cycles * 100.0;
    stall_tmu_pct = tmu_stall / total_cycles * 100.0;
    stall_sb_pct = sb_stall / total_cycles * 100.0;
    stall_vary_pct = vary_stall / total_cycles * 100.0;

    /* Adjust for power-off time */
    if (poweroff_ratio > 0) {
        double active_ratio = 1.0 - poweroff_ratio;
        idle_pct = idle_pct * active_ratio + poweroff_ratio * 100.0;
        vertex_pct *= active_ratio;
        fragment_pct *= active_ratio;
        valid_pct *= active_ratio;
        stall_tmu_pct *= active_ratio;
        stall_sb_pct *= active_ratio;
        stall_vary_pct *= active_ratio;
    }

    /* Status */
    if (poweroff_ratio > 0.5) {
        printf("  GPU Status: MOSTLY IDLE (%.0f%% power-gated)\n", poweroff_ratio * 100);
    } else if (poweroff_ratio > 0) {
        printf("  GPU Status: ACTIVE (%.0f%% power-gated)\n", poweroff_ratio * 100);
    } else {
        printf("  GPU Status: ACTIVE\n");
    }
    printf("  QPU Activity: %.2f M cycles/s\n", valid_cycles / timediff / 1e6);
    printf("--------------------------------------------\n");

    /* Performance bars */
    print_bar("Idle", idle_pct, 20);
    print_bar("Vertex", vertex_pct, 20);
    print_bar("Fragment", fragment_pct, 20);
    print_bar("Valid", valid_pct, 20);
    printf("--------------------------------------------\n");
    printf("  Stalls:\n");
    print_bar("TMU", stall_tmu_pct, 20);
    print_bar("Scoreboard", stall_sb_pct, 20);
    print_bar("Varying", stall_vary_pct, 20);
    printf("============================================\n");
    printf("Press Ctrl+C to exit\n");
    fflush(stdout);
}

static void counter_loop(void)
{
    double last_sampling_time, last_update_time, now;
    struct counter_status current, last_update, diff;
    int idling = 1;
    int i;

    memset(&current, 0, sizeof(current));
    memset(&last_update, 0, sizeof(last_update));

    last_sampling_time = get_time_sec();
    last_update_time = last_sampling_time;

    start_counters();

    while (running) {
        sleep_ms(sampling_interval_ms);
        now = get_time_sec();
        last_sampling_time = now;

        /* Update all counters */
        if (get_counter(0) == POWEROFF_VALUE) {
            clear_counter(0); /* Reset counter */
            current.idle_count++;
            idling = 1;
        } else {
            if (idling) {
                idling = 0;
                start_counters();
            }
            for (i = 0; i < ACTIVE_COUNTERS; ++i) {
                uint32_t value = get_counter(i);
                current.counts[i] += value;
            }
            /* Simply start the counters always after reading */
            start_counters();
        }

        if ((now - last_update_time) * 1000 > update_interval_ms) {
            counter_status_sub(&diff, &current, &last_update);
            diff.timediff = now - last_update_time;
            last_update_time = now;
            last_update = current;

            print_stats(&diff);
        }

        for (i = 0; i < 16; ++i)
            clear_counter(i);
    }
}

static void print_usage(const char* prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -s <ms>   Sampling interval in milliseconds (default: 100)\n");
    printf("  -u <ms>   Update interval in milliseconds (default: 500)\n");
    printf("  -h        Show this help message\n");
}

int main(int argc, char* argv[])
{
    int fd = 0;
    int opt;

    /* Parse command line arguments */
    while ((opt = getopt(argc, argv, "s:u:h")) != -1) {
        switch (opt) {
        case 's':
            sampling_interval_ms = atoi(optarg);
            break;
        case 'u':
            update_interval_ms = atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Set up signal handler for graceful exit */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Open /dev/mem and map VC4 registers */
    const char* dev_mem = "/dev/mem";
    fd = open(dev_mem, O_RDWR | O_SYNC);
    if (fd < 0) {
        printf("Open of %s failed: %d, are you root?\n", dev_mem, errno);
        return -1;
    }

    base = (volatile uint32_t*)mmap(
        NULL,
        VC4_BLOCK_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        VC4_BASE);

    if (base == MAP_FAILED) {
        printf("Mapping register file failed: %d\n", errno);
        close(fd);
        return -2;
    }
    close(fd);

    printf("Starting VC4 GPU monitor...\n");
    printf("Sampling interval: %d ms\n", sampling_interval_ms);
    printf("Update interval: %d ms\n", update_interval_ms);

    /* Run the counter loop in the main thread */
    counter_loop();

    /* Clean up */
    munmap((void*)base, VC4_BLOCK_SIZE);

    printf("\nExiting...\n");
    return 0;
}
