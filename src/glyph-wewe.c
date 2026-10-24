#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#define SYSFS_BASE "/sys/devices/platform/soc/11e80000.i2c/i2c-3/3-003a/leds/aw20036_led"
#define MODE_FILE  SYSFS_BASE "/operating_mode"
#define BRIGHT_FILE SYSFS_BASE "/all_brightness"
#define SINGLE_FILE SYSFS_BASE "/single_brightness"
#define HWID_FILE   SYSFS_BASE "/hwid"

#define I2C_UNBIND "/sys/bus/i2c/drivers/aw20036_led/unbind"
#define I2C_BIND   "/sys/bus/i2c/drivers/aw20036_led/bind"
#define I2C_DEV    "3-003a"

#define FIFO_PATH  "/data/local/tmp/glyph_fifo"
#define PID_PATH   "/data/local/tmp/glyph_daemon.pid"

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t cancel_animation = 0;

static void handle_sig(int sig) {
    if (sig == SIGTERM || sig == SIGINT) running = 0;
    if (sig == SIGUSR1) cancel_animation = 1; 
}

static void print_disclaimer(void) {
    fprintf(stderr, "WARNING: Use at your own risk. The author is not responsible for any damage.\n");
}

static int write_sysfs(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: open(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t len = strlen(value);
    ssize_t w = write(fd, value, len);
    int saved_errno = errno;
    close(fd);
    
    if (w != len) {
        fprintf(stderr, "Error: write(%s) failed: %s\n", path, strerror(saved_errno));
        return -1;
    }
    return 0;
}

static int read_sysfs(const char *path, char *buf, size_t buflen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, buflen - 1);
    close(fd);
    if (r < 0) return -1;
    buf[r] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

static int set_manual_mode(void) {
    return write_sysfs(MODE_FILE, "1");
}

static int set_brightness(int value) {
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    return write_sysfs(BRIGHT_FILE, buf);
}

static int clear_all_leds(void) {
    int ret = 0;
    if (set_brightness(0) != 0) ret = -1;
    for(int i = 0; i <= 25; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d 0", i);
        if (write_sysfs(SINGLE_FILE, buf) != 0) ret = -1;
    }
    return ret; 
}

/* ===== Команды ===== */

static int cmd_start(void) {
    if (set_manual_mode() != 0) return 1;
    if (set_brightness(255) != 0) return 1;
    printf("OK: Glyphs ON (255)\n");
    return 0;
}

static int cmd_stop(void) {
    cancel_animation = 1;
    usleep(50000);
    if (clear_all_leds() != 0) return 1;
    printf("OK: Glyphs OFF\n");
    return 0;
}

static int cmd_brightness(int value) {
    cancel_animation = 1;
    if (set_manual_mode() != 0) return 1;
    if (set_brightness(value) != 0) return 1;
    printf("OK: Brightness = %d\n", value);
    return 0;
}

static int cmd_led(int id, int brightness) {
    if (id < 0 || id > 25) {
        fprintf(stderr, "Error: LED ID must be 0-25\n");
        return 1;
    }
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;

    cancel_animation = 1;
    set_manual_mode();
    set_brightness(0); 
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%d %d", id, brightness);
    if (write_sysfs(SINGLE_FILE, buf) != 0) return 1;
    printf("OK: LED %d -> %d\n", id, brightness);
    return 0;
}

static int cmd_wave(void) {
    cancel_animation = 0;
    if (set_manual_mode() != 0) return 1;
    if (clear_all_leds() != 0) return 1;

    for (int loop = 0; loop < 3; loop++) {
        for (int i = 23; i >= 0; i--) {
            if (cancel_animation) {
                printf("Wave cancelled\n");
                clear_all_leds();
                return 0;
            }
            if (i % 5 == 0) {
                char mode[8];
                if (read_sysfs(MODE_FILE, mode, sizeof(mode)) == 0 && strcmp(mode, "1") != 0) {
                    set_manual_mode();
                }
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%d 255", i);
            write_sysfs(SINGLE_FILE, buf);
            usleep(30000);
            if (i < 23) {
                snprintf(buf, sizeof(buf), "%d 0", i + 1);
                write_sysfs(SINGLE_FILE, buf);
            }
        }
        write_sysfs(SINGLE_FILE, "0 0");
    }
    printf("OK: Wave done\n");
    return 0;
}

/* Заполнение верхней полосы (0-24) */
static int cmd_fill(int count) {
    if (count < 0) count = 0;
    if (count > 24) count = 24;
    
    cancel_animation = 1;
    set_manual_mode();
    set_brightness(0); 
    
    for (int i = 0; i < 24; i++) {
        char buf[32];
        int val = (i < count) ? 255 : 0;
        snprintf(buf, sizeof(buf), "%d %d", i, val);
        write_sysfs(SINGLE_FILE, buf);
    }
    printf("OK: Filled %d LEDs\n", count);
    return 0;
}

static int cmd_status(void) {
    char mode[64] = "?";
    char hwid[64] = "?";
    read_sysfs(MODE_FILE, mode, sizeof(mode));
    read_sysfs(HWID_FILE, hwid, sizeof(hwid));
    printf("Mode: %s\nHWID: %s\n", mode, hwid);
    return 0;
}

static int cmd_recover(void) {
    fprintf(stderr, "WARNING: Driver recovery may freeze the device. Hold Power 15-30s if unresponsive.\n");
    printf("Attempting driver recovery...\n");
    
    write_sysfs(I2C_UNBIND, I2C_DEV);
    usleep(500000); 
    
    if (write_sysfs(I2C_BIND, I2C_DEV) != 0) {
        fprintf(stderr, "Error: bind failed. Driver might be stuck.\n");
        return 1;
    }
    
    usleep(1000000); 
    
    char hwid[16];
    if (read_sysfs(HWID_FILE, hwid, sizeof(hwid)) == 0) {
        printf("Recovery successful. HWID: %s\n", hwid);
        set_manual_mode();
        return 0;
    } else {
        fprintf(stderr, "Error: chip not responding after rebind.\n");
        return 1;
    }
}

static int cmd_flip(int enable) {
    if (enable) {
        system("killall flipd 2>/dev/null");
        system("setsid nohup /system/bin/flipd >/dev/null 2>&1 &");
        printf("OK: Flip to Glyph ENABLED\n");
    } else {
        system("killall flipd 2>/dev/null");
        printf("OK: Flip to Glyph DISABLED\n");
    }
    return 0;
}

static int execute_line(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return 0;

    char *cmd = line;
    char *arg1 = strchr(line, ' ');
    if (arg1) { *arg1 = '\0'; arg1++; }
    char *arg2 = NULL;
    if (arg1) {
        arg2 = strchr(arg1, ' ');
        if (arg2) { *arg2 = '\0'; arg2++; }
    }

    if (strcmp(cmd, "start") == 0) return cmd_start();
    if (strcmp(cmd, "stop") == 0) return cmd_stop();
    if (strcmp(cmd, "wave") == 0) return cmd_wave();
    if (strcmp(cmd, "fill") == 0 && arg1) return cmd_fill(atoi(arg1));
    if (strcmp(cmd, "status") == 0) return cmd_status();
    if (strcmp(cmd, "recover") == 0) return cmd_recover();
    if (strcmp(cmd, "flip") == 0 && arg1) return cmd_flip(strcmp(arg1, "on") == 0);
    if (strcmp(cmd, "brightness") == 0 && arg1) return cmd_brightness(atoi(arg1));
    if (strcmp(cmd, "led") == 0 && arg1 && arg2) return cmd_led(atoi(arg1), atoi(arg2));
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 1;
}

static int cmd_daemon(void) {
    struct stat st;
    if (stat(FIFO_PATH, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) return 1;
        if ((st.st_mode & 0002) == 0) chmod(FIFO_PATH, 0666);
    } else {
        if (mkfifo(FIFO_PATH, 0666) != 0) return 1;
    }

    FILE *pf = fopen(PID_PATH, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    int fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (fd < 0) return 1;

    char buf[512];
    int pos = 0;

    while (running) {
        ssize_t r = read(fd, buf + pos, sizeof(buf) - pos - 1);
        if (r > 0) {
            pos += r;
            buf[pos] = '\0';
            char *start = buf;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = '\0';
                if (strlen(start) > 0) execute_line(start);
                start = nl + 1;
            }
            size_t rem = strlen(start);
            if (start != buf) memmove(buf, start, rem + 1);
            pos = rem;
            if (pos >= (int)sizeof(buf) - 1) pos = 0; 
        }
        char mode[8];
        if (read_sysfs(MODE_FILE, mode, sizeof(mode)) == 0) {
            if (strcmp(mode, "1") != 0) set_manual_mode();
        }
        usleep(500000);
    }
    close(fd);
    unlink(PID_PATH);
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <start|stop|status|wave|fill N|recover|flip on/off|brightness N|led ID N|daemon>\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "status") != 0 && strcmp(cmd, "daemon") != 0) {
        print_disclaimer();
    }

    if (strcmp(cmd, "start") == 0) return cmd_start();
    if (strcmp(cmd, "stop") == 0) return cmd_stop();
    if (strcmp(cmd, "wave") == 0) return cmd_wave();
    if (strcmp(cmd, "status") == 0) return cmd_status();
    if (strcmp(cmd, "recover") == 0) return cmd_recover();
    if (strcmp(cmd, "daemon") == 0) return cmd_daemon();
    if (strcmp(cmd, "brightness") == 0 && argc >= 3) return cmd_brightness(atoi(argv[2]));
    if (strcmp(cmd, "fill") == 0 && argc >= 3) return cmd_fill(atoi(argv[2]));
    if (strcmp(cmd, "led") == 0 && argc >= 4) return cmd_led(atoi(argv[2]), atoi(argv[3]));
    if (strcmp(cmd, "flip") == 0 && argc >= 3) return cmd_flip(strcmp(argv[2], "on") == 0);
    
    usage(argv[0]);
    return 1;
}
