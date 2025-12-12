// unlocker_cn.c - Genshin unlocker for linux
//
// To compile:
// gcc -O2 -o unlocker_cn unlocker_cn.c -Wall -Wextra
//
// To run without sudo (one-time setup):
// 1. Compile the program.
// 2. Grant capability: `sudo setcap cap_sys_ptrace+ep ./unlocker_cn`
// 3. Run as a normal user.
//
// Usage: ./unlocker_cn <TRACE_TIME> <FPS> <FPS_WRITE_INTERVAL> wine <Game> [GameArgs...]
// Example: ./unlocker_cn 30 120 5000 wine /path/to/YuanShen.exe

// WARN: DO NOT USE THIS ON PROTON
//       Proton implements detection for ptrace, which allows internal windows programs to potentially detect ptrace.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define MAX_PIDS 4096
#define ANY_ (int16_t)-1

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

pid_t traced_pids[MAX_PIDS];
int pid_count = 0;

int ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_suffix > len_str)
        return 0;
    return !strncmp(str + len_str - len_suffix, suffix, len_suffix);
}

int verify_process(pid_t pid)
{
    char path[64];
    FILE *fp;
    char cmdline[2048] = {0};

    if (pid < 1)
        return 0;
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    fp = fopen(path, "r");
    if (!fp)
        return 0;

    size_t read_len = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
    fclose(fp);
    if (read_len == 0)
        return 0;

    return ends_with(cmdline, "GenshinImpact.exe") || ends_with(cmdline, "YuanShen.exe");
}

int read_process_memory(pid_t pid, uintptr_t addr, void *buf, size_t size)
{
    struct iovec local = {.iov_base = buf, .iov_len = size};
    struct iovec remote = {.iov_base = (void *)addr, .iov_len = size};
    ssize_t bytes_read = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (bytes_read == -1)
    {
        fprintf(stderr, "Error: Read %zu bytes at %p failed: %s\n", size, (void *)addr, strerror(errno));
        return 0;
    }
    return bytes_read == (ssize_t)size;
}

int write_process_memory(pid_t pid, uintptr_t addr, const void *buf, size_t size)
{
    struct iovec local = {.iov_base = (void *)buf, .iov_len = size};
    struct iovec remote = {.iov_base = (void *)addr, .iov_len = size};
    ssize_t bytes_written = process_vm_writev(pid, &local, 1, &remote, 1, 0);
    if (bytes_written == -1)
    {
        fprintf(stderr, "Error: Write %zu bytes at %p failed: %s\n", size, (void *)addr, strerror(errno));
        return 0;
    }
    return bytes_written == (ssize_t)size;
}

void *find_pattern_in_process(pid_t pid, uintptr_t start, size_t len, const int16_t *pattern, size_t plen)
{
    uint8_t buf[4096];
    for (size_t i = 0; i < len; i += sizeof(buf) - plen)
    {
        if (!read_process_memory(pid, start + i, buf, sizeof(buf)))
            break;
        for (size_t j = 0; j < sizeof(buf) - plen; ++j)
        {
            int found = 1;
            for (size_t k = 0; k < plen; ++k)
            {
                if (pattern[k] != ANY_ && pattern[k] != buf[j + k])
                {
                    found = 0;
                    break;
                }
            }
            if (found)
                return (void *)(start + i + j);
        }
    }
    return NULL;
}

uintptr_t find_fps_var_address(pid_t pid)
{
    char maps_path[64];
    FILE *fp = NULL;
    char *line = NULL;
    size_t len = 0;
    uint8_t *setter_call = NULL;
    uintptr_t fps_var_addr = 0;

    int16_t pattern[] = {0xB9, 0x3C, 0x00, 0x00, 0x00, 0xE8, ANY_, ANY_, ANY_, ANY_, 0x80};
    size_t pattern_len = sizeof(pattern) / sizeof(pattern[0]);

    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    fp = fopen(maps_path, "r");
    if (!fp)
        return 0;

    uintptr_t last_end = 0;
    int game_module_found = 0;

    while (getline(&line, &len, fp) != -1)
    {
        uintptr_t start, end;
        char perms[5], path[1024] = {0};

        if (sscanf(line, "%lx-%lx %4s %*x %*s %*d %1023[^\n]%*c", &start, &end, perms, path) < 3)
            continue;

        if (!game_module_found)
        {
            if (ends_with(path, "GenshinImpact.exe") || ends_with(path, "YuanShen.exe"))
            {
                game_module_found = 1;
            }
            else
            {
                continue;
            }
        }
        if (last_end != 0 && start != last_end)
            break;

        if (strchr(perms, 'x'))
        {
            setter_call = find_pattern_in_process(pid, start, end - start, pattern, pattern_len);
            if (setter_call)
                break;
        }
        last_end = end;
    }

    if (!setter_call)
        goto out_cleanup;

    uint8_t instr_bytes[7];
    uint8_t *current_addr = setter_call + 5;
    for (int i = 0; i < 5; ++i)
    {
        if (!read_process_memory(pid, (uintptr_t)current_addr, instr_bytes, sizeof(instr_bytes)))
            goto out_cleanup;
        if (instr_bytes[0] == 0xE8 || instr_bytes[0] == 0xE9)
        {
            int32_t offset;
            memcpy(&offset, &instr_bytes[1], 4);
            current_addr += offset + 5;
        }
        else
        {
            break;
        }
    }

    if (instr_bytes[0] == 0x89 && instr_bytes[1] == 0x0D)
    {
        int32_t rip_offset;
        memcpy(&rip_offset, &instr_bytes[2], 4);
        fps_var_addr = (uintptr_t)(current_addr + 6 + rip_offset);
    }

out_cleanup:
    if (fp)
        fclose(fp);
    if (line)
        free(line);
    return fps_var_addr;
}

void add_pid(pid_t pid)
{
    for (int i = 0; i < pid_count; i++)
    {
        if (traced_pids[i] == pid)
            return;
    }
    if (pid_count < MAX_PIDS)
    {
        traced_pids[pid_count++] = pid;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 6)
    {
        fprintf(stderr, "Usage: %s <TRACE_TIME> <FPS> <FPS_WRITE_INTERVAL> wine <Game> [GameArgs...]\n", argv[0]);
        return 1;
    }
    int trace_time = atoi(argv[1]);
    int target_fps = atoi(argv[2]);
    int fps_interval = atoi(argv[3]);
    int enable_unlock = 1;

    if (target_fps <= 60 || target_fps > 165)
    {
        printf("FPS %d out of range (61-165). Protection ONLY.\n", target_fps);
        enable_unlock = 0;
    }

    pid_t child = fork();
    if (child < 0)
        return 1;

    if (child == 0)
    {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
            exit(1);
        signal(SIGSTOP, SIG_IGN);
        execvp(argv[4], &argv[4]);
        exit(1);
    }

    int status;
    time_t start_time = time(NULL);

    if (waitpid(child, &status, 0) < 0)
        return 1;

    unsigned long options = PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACEEXIT |
                            PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC | PTRACE_O_TRACESYSGOOD;
    ptrace(PTRACE_SETOPTIONS, child, 0, options);

    add_pid(child);
    ptrace(PTRACE_SYSCALL, child, 0, 0);

    // --- Phase 1: ptrace ---
    int counter = 0;
    pid_t last_pid = -1;
    while (1)
    {
        if (unlikely((++counter & 0xFFFF) == 0) && time(NULL) - start_time > trace_time)
        {
            break;
        }

        pid_t p = waitpid(-1, &status, __WALL);

        if (unlikely(p < 0))
        {
            if (errno == ECHILD)
                break;
            continue;
        }

        if (p != last_pid)
        {
            add_pid(p);
            last_pid = p;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            if (p == last_pid)
            {
                last_pid = -1;
            }
            continue;
        }

        if (likely(WIFSTOPPED(status)))
        {
            int sig = WSTOPSIG(status);
            int injection_sig = 0;

            if (likely(sig & 0x80))
            {
            }
            else
            {
                if ((status >> 16) == 0)
                {
                    if (sig != SIGSTOP)
                    {
                        injection_sig = sig;
                    }
                }
            }

            ptrace(PTRACE_SYSCALL, p, 0, injection_sig);
        }
    }

    // --- Phase 2: detach All ---
    printf("Detaching...\n");
    for (int i = 0; i < pid_count; i++)
    {
        pid_t p = traced_pids[i];

        kill(p, SIGSTOP);
        int s;
        waitpid(p, &s, __WALL);

        ptrace(PTRACE_DETACH, p, 0, 0);
        kill(p, SIGCONT);
    }

    // --- Phase 3: unlock fps ---
    pid_t game_pid = 0;
    uintptr_t fps_addr = 0;

    if (enable_unlock)
    {
        sleep(1);
        for (int i = 0; i < pid_count; i++)
        {
            pid_t p = traced_pids[i];
            if (kill(p, 0) == 0 && verify_process(p))
            {
                fps_addr = find_fps_var_address(p);
                if (fps_addr)
                {
                    game_pid = p;
                    printf("Unlocking FPS: %d for PID: %d at %p\n", target_fps, p, (void *)fps_addr);
                    break;
                }
            }
        }
        if (game_pid != 0 && fps_addr != 0)
        {
            while (kill(game_pid, 0) == 0)
            {
                write_process_memory(game_pid, fps_addr, &target_fps, sizeof(target_fps));
                usleep(fps_interval * 1000);
            }
        }
    }

    int wstatus;
    while (waitpid(child, &wstatus, 0) != -1 || errno != ECHILD)
    {
    }

    return 0;
}
