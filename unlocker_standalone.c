/*
 * unlocker.c
 * 
 * Compile with: 
 * gcc -o unlocker unlocker.c -nostdlib -fno-builtin -fno-stack-protector -O2
 * 
 * Usage: ./unlocker <FPS> [INTERVAL_MS]
 */

/* ================= TYPES & DEFINITIONS ================= */

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned long uintptr_t;
typedef long intptr_t;
typedef int pid_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef unsigned long uint64_t;

#define NULL ((void*)0)
#define ANY_ (int16_t)-1

// Syscall numbers (x86_64)
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_nanosleep 35
#define SYS_exit 60
#define SYS_getdents64 217
#define SYS_process_vm_readv 310
#define SYS_process_vm_writev 311

// File flags
#define O_RDONLY 0
#define O_DIRECTORY 0x10000

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

// Directory entry structure for getdents64
struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

#define DT_DIR 4

/* ================= ENTRY POINT (ASSEMBLY) ================= */

__asm__(
    ".text\n"
    ".global _start\n"
    "_start:\n"
    "   xor %rbp, %rbp\n"       // Clear frame pointer
    "   pop %rdi\n"             // Pop argc into rdi
    "   mov %rsp, %rsi\n"       // argv is now at the top of stack, move to rsi
    "   and $-16, %rsp\n"       // Align stack to 16 bytes
    "   call main\n"            // Call main(argc, argv)
    "   mov %rax, %rdi\n"       // Move return value to rdi
    "   mov $60, %rax\n"        // SYS_exit
    "   syscall\n"
);

/* ================= SYSCALL WRAPPERS ================= */

static inline long syscall1(long n, long a1) {
    unsigned long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2) {
    unsigned long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    unsigned long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    unsigned long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    __asm__ volatile (
        "syscall" 
        : "=a"(ret) 
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) 
        : "rcx", "r11", "memory"
    );
    return ret;
}

void sys_exit(int code) {
    syscall1(SYS_exit, code);
    __builtin_unreachable();
}

ssize_t sys_write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_write, fd, (long)buf, count);
}

int sys_open(const char *filename, int flags) {
    return syscall3(SYS_open, (long)filename, flags, 0);
}

int sys_close(int fd) {
    return syscall1(SYS_close, fd);
}

ssize_t sys_read(int fd, void *buf, size_t count) {
    return syscall3(SYS_read, fd, (long)buf, count);
}

int sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    return syscall2(SYS_nanosleep, (long)req, (long)rem);
}

int sys_getdents64(int fd, struct linux_dirent64 *dirp, unsigned int count) {
    return syscall3(SYS_getdents64, fd, (long)dirp, count);
}

ssize_t sys_process_vm_readv(pid_t pid, const struct iovec *lvec, unsigned long liovcnt,
                             const struct iovec *rvec, unsigned long riovcnt,
                             unsigned long flags) {
    return syscall6(SYS_process_vm_readv, pid, (long)lvec, liovcnt, (long)rvec, riovcnt, flags);
}

ssize_t sys_process_vm_writev(pid_t pid, const struct iovec *lvec, unsigned long liovcnt,
                              const struct iovec *rvec, unsigned long riovcnt,
                              unsigned long flags) {
    return syscall6(SYS_process_vm_writev, pid, (long)lvec, liovcnt, (long)rvec, riovcnt, flags);
}

/* ================= STRING & UTILS ================= */

size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int my_strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}

void my_memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

void my_memmove(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
}

int my_atoi(const char *s) {
    int res = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

uintptr_t my_strtoul_hex(char **str) {
    uintptr_t res = 0;
    char *s = *str;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9') res = (res << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') res = (res << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') res = (res << 4) | (c - 'A' + 10);
        else break;
        s++;
    }
    *str = s;
    return res;
}

void print(const char *s) {
    sys_write(1, s, my_strlen(s));
}

void print_err(const char *s) {
    sys_write(2, s, my_strlen(s));
}

// Convert int to string
void my_itoa_dec(int num, char* buffer) {
    char temp[20];
    int i = 0;
    if (num == 0) { buffer[0] = '0'; buffer[1] = 0; return; }
    int neg = (num < 0);
    if (neg) num = -num;
    while (num > 0) {
        temp[i++] = (num % 10) + '0';
        num /= 10;
    }
    if (neg) temp[i++] = '-';
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = 0;
}

// Helper to check if string contains only digits
int is_digits(const char *s) {
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

// Helper to construct /proc path
void build_proc_path(const char *pid_str, const char *suffix, char *out_buf) {
    const char *prefix = "/proc/";
    char *p = out_buf;
    while (*prefix) *p++ = *prefix++;
    while (*pid_str) *p++ = *pid_str++;
    while (*suffix) *p++ = *suffix++;
    *p = 0;
}

/* ================= PID FINDER LOGIC ================= */

int check_filename(const char *path) {
    size_t len = my_strlen(path);
    const char *target1 = "GenshinImpact.exe";
    const char *target2 = "YuanShen.exe";
    size_t l1 = my_strlen(target1);
    size_t l2 = my_strlen(target2);

    if (len >= l1 && my_strncmp(path + len - l1, target1, l1) == 0) return 1;
    if (len >= l2 && my_strncmp(path + len - l2, target2, l2) == 0) return 1;
    return 0;
}

int check_pid_cmdline(const char *pid_str) {
    char path[64];
    build_proc_path(pid_str, "/cmdline", path);

    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) return 0;

    char cmdline[512];
    ssize_t ret = sys_read(fd, cmdline, sizeof(cmdline)-1);
    sys_close(fd);

    if (ret <= 0) return 0;
    cmdline[ret] = 0;

    return check_filename(cmdline);
}

pid_t find_game_pid() {
    int fd = sys_open("/proc", O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        print_err("Error: Failed to open /proc\n");
        return 0;
    }

    char buf[1024];
    int nread;
    pid_t found_pid = 0;

    while ((nread = sys_getdents64(fd, (struct linux_dirent64 *)buf, sizeof(buf))) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            
            // d_type == DT_DIR (4) and name is numeric
            if (d->d_type == DT_DIR && is_digits(d->d_name)) {
                if (check_pid_cmdline(d->d_name)) {
                    found_pid = my_atoi(d->d_name);
                    goto done;
                }
            }
            bpos += d->d_reclen;
        }
    }

done:
    sys_close(fd);
    return found_pid;
}

/* ================= MEMORY SCAN LOGIC ================= */

int read_process_memory(pid_t pid, uintptr_t addr, void* buf, size_t size) {
    struct iovec local = { .iov_base = buf, .iov_len = size };
    struct iovec remote = { .iov_base = (void*)addr, .iov_len = size };
    ssize_t bytes = sys_process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return bytes == (ssize_t)size;
}

int write_process_memory(pid_t pid, uintptr_t addr, const void* buf, size_t size) {
    struct iovec local = { .iov_base = (void*)buf, .iov_len = size };
    struct iovec remote = { .iov_base = (void*)addr, .iov_len = size };
    ssize_t bytes = sys_process_vm_writev(pid, &local, 1, &remote, 1, 0);
    return bytes == (ssize_t)size;
}

void* find_pattern_in_process(pid_t pid, uintptr_t start, size_t len, const int16_t* pattern, size_t plen) {
    uint8_t buf[4096];
    size_t chunk_size = sizeof(buf);
    
    for (size_t i = 0; i < len; i += chunk_size - plen) {
        size_t read_size = chunk_size;
        if (i + read_size > len) read_size = len - i;

        if (!read_process_memory(pid, start + i, buf, read_size)) break;
        
        for (size_t j = 0; j <= read_size - plen; ++j) {
            int found = 1;
            for (size_t k = 0; k < plen; ++k) {
                if (pattern[k] != ANY_ && pattern[k] != buf[j + k]) { 
                    found = 0; 
                    break; 
                }
            }
            if (found) return (void*)(start + i + j);
        }
    }
    return NULL;
}

uintptr_t find_fps_var_address(pid_t pid) {
    char path[64];
    char pid_str[16];
    my_itoa_dec(pid, pid_str);
    build_proc_path(pid_str, "/maps", path);

    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        print_err("Error: Open maps failed\n");
        return 0;
    }

    int16_t pattern[] = { 0xB9, 0x3C, 0x00, 0x00, 0x00, 0xE8, ANY_, ANY_, ANY_, ANY_, 0x80 };
    size_t pattern_len = sizeof(pattern)/sizeof(pattern[0]);
    
    uintptr_t fps_var_addr = 0;
    uint8_t *setter_call = NULL;
    int game_module_found = 0;
    uintptr_t last_end = 0;

    char buf[4096];
    size_t buf_pos = 0;
    ssize_t bytes_read;
    
    while ((bytes_read = sys_read(fd, buf + buf_pos, sizeof(buf) - buf_pos)) > 0) {
        size_t total_len = buf_pos + bytes_read;
        char *line_start = buf;
        char *ptr = buf;
        char *end_ptr = buf + total_len;

        while (ptr < end_ptr) {
            if (*ptr == '\n') {
                *ptr = 0; 
                char *curr = line_start;
                uintptr_t start = my_strtoul_hex(&curr);
                if (*curr == '-') {
                    curr++;
                    uintptr_t end = my_strtoul_hex(&curr);
                    while (*curr == ' ') curr++;
                    char *perms = curr;
                    while (*curr && *curr != ' ') curr++;
                    
                    char *path_start = curr;
                    while (*path_start && *path_start != '/') path_start++; 
                    
                    int is_game = 0;
                    if (*path_start == '/') {
                        if (check_filename(path_start)) is_game = 1;
                    }

                    if (!game_module_found) {
                        if (is_game) game_module_found = 1;
                    }
                    
                    if (game_module_found) {
                        if (!is_game && last_end != 0 && start != last_end) {
                             goto finish_scan; 
                        }
                        int executable = 0;
                        for (char *c = perms; c < curr; c++) {
                            if (*c == 'x') executable = 1;
                        }
                        if (executable) {
                             setter_call = (uint8_t*)find_pattern_in_process(pid, start, end - start, pattern, pattern_len);
                             if (setter_call) {
                                 goto pattern_found;
                             }
                        }
                        last_end = end;
                    }
                }
                line_start = ptr + 1;
            }
            ptr++;
        }
        size_t remaining = end_ptr - line_start;
        if (remaining > 0) {
            my_memmove(buf, line_start, remaining);
        }
        buf_pos = remaining;
    }

    goto finish_scan;

pattern_found:
    sys_close(fd);
    
    uint8_t instr_bytes[7];
    uint8_t *current_addr = setter_call + 5;
    for (int i = 0; i < 5; ++i) { 
        if (!read_process_memory(pid, (uintptr_t)current_addr, instr_bytes, sizeof(instr_bytes))) return 0;
        if (instr_bytes[0] == 0xE8 || instr_bytes[0] == 0xE9) { // JMP or CALL
            int32_t offset = *(int32_t*)(instr_bytes + 1);
            current_addr += offset + 5;
        } else {
            break;
        }
    }

    if (instr_bytes[0] == 0x89 && instr_bytes[1] == 0x0D) {
        int32_t rip_offset = *(int32_t*)(instr_bytes + 2);
        fps_var_addr = (uintptr_t)(current_addr + 6 + rip_offset);
        return fps_var_addr;
    } else {
        print_err("Error: Invalid instruction found\n");
        return 0;
    }

finish_scan:
    sys_close(fd);
    print_err("Error: Pattern not found\n");
    return 0;
}

/* ================= MAIN ================= */

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: ./unlocker <FPS> [INTERVAL_MS]\nNote: Run 'sudo setcap cap_sys_ptrace+ep ./unlocker' once to run without sudo.\n");
        return 1;
    }

    int32_t target_fps = my_atoi(argv[1]);
    int64_t interval = 5000; 

    if (argc >= 3) {
        interval = my_atoi(argv[2]);
    }

    if (target_fps < 1) {
        print_err("Error: FPS must be > 0\n");
        return 1;
    }

    pid_t pid = find_game_pid();

    if (pid == 0) {
        print_err("Error: GenshinImpact.exe or YuanShen.exe not found.\n");
        return 1;
    }

    uintptr_t fps_addr = find_fps_var_address(pid);
    if (!fps_addr) {
        return 1;
    }

    print("Unlocker active. Press Ctrl+C to stop.\n");

    struct timespec req;
    req.tv_sec = interval / 1000;
    req.tv_nsec = (interval % 1000) * 1000000;

    // Write Loop
    while (1) {
        if (!write_process_memory(pid, fps_addr, &target_fps, sizeof(target_fps))) {
            print_err("Error: Write failed (process exited?)\n");
            break;
        }
        if (interval > 0) {
            sys_nanosleep(&req, NULL);
        } else {
            break;
        }
    }

    return 0;
}
