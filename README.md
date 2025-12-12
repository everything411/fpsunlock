# Genshin Impact FPS Unlocker for Linux

**We will defy this world with a power from beyond.**

**WARNING: since v4.8.0 the game regularly reports measured FPS to the server. Use this tool at your own risk.**

**This small native Linux tool allows you to set a custom FPS limit for Genshin Impact running under Wine or Proton**. It is a native application and does not require Wine to run itself, but it targets the game process running within a Wine/Proton environment.

**The current version was tested with Genshin Impact Version Luna I (v6.0), and should work with future versions too**. If it breaks after an update, please open an issue on this repository's issue tracker.

## Usage

### 1. One-Time Setup: Permissions
This tool needs special permissions to read and write another process's memory. Instead of requiring `sudo` for every run, the recommended approach is to grant the compiled binary the `CAP_SYS_PTRACE` capability.

**This only needs to be done once after compiling:**
```bash
sudo setcap cap_sys_ptrace+ep ./unlocker
```
After this, you can run the unlocker as a normal user.

### 2. Find the Game's PID
The game must be running before you start the unlocker. You will need its Process ID (PID):
```bash
wine GenshinImpact.exe &
sleep 5 && /path/to/unlocker $! 120 5000
```
The `sleep` is used to ensure Genshin Impact is fully launched, preventing failures due to the game process not being ready.

Alternatively, you can use `pgrep` to find the PID of either `YuanShen.exe` or `GenshinImpact.exe` if you prefer not to use `$!`.

### 3. Run the Unlocker
The command line interface is `./unlocker <PID> <FPS> [INTERVAL_MS]`, where:
-   `<PID>` - The Process ID of the game you found in the previous step (required).
-   `<FPS>` - The target framerate value (e.g., `120`).
-   When `FPS <= 0` - A special mode that **only reads and displays** the current FPS limit without making any changes. This is useful for testing.
-   `[INTERVAL_MS]` - The delay between periodic operations in milliseconds (optional, default is 5000). A negative value will cause the unlocker to perform the action only once and then exit.

**Examples:**
-   Set the FPS limit to 144 and rewrite it every 5 seconds:
    ```bash
    ./unlocker 12345 144
    ```
-   Set the FPS limit to 120 only once and exit:
    ```bash
    ./unlocker 12345 120 -1
    ```
-   Monitor the current in-game FPS limit every second without changing it:
    ```bash
    ./unlocker 12345 -1 1000
    ```

**DO NOT PUT THE FPS UNLOCKER INTO THE GAME DIRECTORY.**

## CN Server Workaround (unlocker_cn)

**Note: This section applies specifically to the CN server. Global server users usually do not need this workaround.**

Since **Version Luna III (v6.2)**, the CN server version of Genshin Impact has exhibited instability when running under Wine. The anti-cheat mechanism appears to behave inconsistently, leading to likely race conditions or something that cause the game to crash on startup.

However, `strace` attaching to the game process effectively "slows down" the initialization, allowing the game to launch correctly. We provide a helper program, `unlocker_cn`, which simulates this behavior to ensure stability.

**WARN: DO NOT USE THIS ON PROTON!** Proton implements detection for ptrace, which allows internal windows programs to potentially detect ptrace.

### Usage
```bash
./unlocker_cn <TRACE_TIME> <FPS> <FPS_WRITE_INTERVAL> wine <Game> [GameArgs...]
```

Typically, setting TRACE_TIME to 30 seconds and FPS_WRITE_INTERVAL to 5000 milliseconds is sufficient. The FPS parameter only accepts values between 61 and 165; values outside this range will disable the FPS unlock feature.

## Safety
**This program is technically breaking the game's Terms of Service**, although I am not aware of any bans caused just by changing the FPS limit. **Use at your own risk.** If you somehow manage to receive a ban, please report it on the issue tracker.

While this tool operates by directly writing memory from a Linux process - leaving no trace of external processes within the Wine environment - the FPS modification itself creates detectable changes. The game servers can still detect that the FPS limit has been altered through unknown means.

## Building
To build the unlocker, you only need a C compiler like `gcc`.
```bash
gcc -o unlocker unlocker.c -Wall -Wextra
```
The compiled file will be located at `./unlocker`. Remember to run the `setcap` command (see Usage) on the new binary.

## Mechanism
**This FPS unlocker does not inject any code into the game, and instead relies on reading and writing process memory via native Linux system calls (`process_vm_readv(2)` and `process_vm_writev(2)`).**

The algorithm is as follows:
1.  Takes the game's Process ID (PID) as a command-line argument.
2.  Parses the process's memory maps (`/proc/[pid]/maps`) to find the game's main executable regions in virtual memory.
3.  Scans only these executable regions for a specific byte pattern (signature) to locate the code responsible for setting the FPS limit.
4.  Traces the assembly instructions from the pattern to calculate the precise memory address of the FPS limit variable.
5.  Periodically overwrites this variable with the target value. If periodic writes are disabled or a write fails (e.g., the game is closed), the unlocker will exit.

## Other games
**For 3rd and SR, using an unlocker program is not required. You can set the FPS values in the registry directly.**

License: MIT
