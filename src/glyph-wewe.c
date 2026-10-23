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

#define FIFO_PATH  "/data/local/tmp/glyph_fifo"
#define PID_PATH   "/data/local/tmp/glyph_daemon.pid"

static volatile sig_atomic_t running = 1;

static void handle_sig(int sig) {
    (void)sig;
    running = 0;
}

static int write_sysfs(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t len = strlen(value);
    ssize_t w = write(fd, value, len);
    close(fd);
    return (w == len) ? 0 : -1;
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

/* Сброс всех individual LED в 0 */
static int clear_all_leds(void) {
    set_brightness(0); 
    for(int i = 0; i <= 25; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d 0", i);
        write_sysfs(SINGLE_FILE, buf);
    }
    return 0; 
}

/* ===== Команды ===== */

static int cmd_start(void) {
    if (set_manual_mode() != 0) return 1;
    if (set_brightness(255) != 0) return 1;
    printf("OK: Glyphs ON (255)\n");
    return 0;
}

static int cmd_stop(void) {
    if (clear_all_leds() != 0) return 1;
    printf("OK: Glyphs OFF\n");
    return 0;
}

static int cmd_brightness(int value) {
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

    set_manual_mode();
    char buf[32];
    snprintf(buf, sizeof(buf), "%d %d", id, brightness);
    if (write_sysfs(SINGLE_FILE, buf) != 0) return 1;
    printf("OK: LED %d -> %d\n", id, brightness);
    return 0;
}

/* Анимация: Бегущая волна (сверху вниз) */
static int cmd_wave(void) {
    set_manual_mode();
    clear_all_leds();

    for (int loop = 0; loop < 3; loop++) {
        for (int i = 23; i >= 0; i--) {
            // Проверяем режим каждые 5 шагов, чтобы Nothing не сбил анимацию
            if (i % 5 == 0) {
                char mode[8];
                if (read_sysfs(MODE_FILE, mode, sizeof(mode)) == 0 && strcmp(mode, "1") != 0) {
                    set_manual_mode();
                }
            }
            
            char buf[32];
            snprintf(buf, sizeof(buf), "%d 255", i);
            write_sysfs(SINGLE_FILE, buf);
            usleep(30000); // 30 мс
            
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

/* Анимация: Прогресс-бар (0-100) */
static int cmd_progress(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    set_manual_mode();
    clear_all_leds();

    int leds_to_light = (percent * 24) / 100;
    
    for (int i = 0; i < 24; i++) {
        char buf[32];
        int val = (i < leds_to_light) ? 255 : 0;
        snprintf(buf, sizeof(buf), "%d %d", i, val);
        write_sysfs(SINGLE_FILE, buf);
    }
    printf("OK: Progress %d%%\n", percent);
    return 0;
}

static int cmd_status(void) {
    char mode[64] = "?";
    read_sysfs(MODE_FILE, mode, sizeof(mode));
    printf("Mode: %s\n", mode);
    return 0;
}

/* Выполнить одну команду из строки */
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
    if (strcmp(cmd, "status") == 0) return cmd_status();
    if (strcmp(cmd, "brightness") == 0 && arg1) return cmd_brightness(atoi(arg1));
    if (strcmp(cmd, "progress") == 0 && arg1) return cmd_progress(atoi(arg1));
    if (strcmp(cmd, "led") == 0 && arg1 && arg2) return cmd_led(atoi(arg1), atoi(arg2));
    
    fprintf(stderr, "Unknown command: %s\n", cmd);
    return 1;
}

static int cmd_daemon(void) {
    if (access(FIFO_PATH, F_OK) != 0) mkfifo(FIFO_PATH, 0666);

    FILE *pf = fopen(PID_PATH, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    int fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (fd < 0) return 1;

    char buf[1024];
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
                execute_line(start);
                start = nl + 1;
            }
            size_t rem = strlen(start);
            if (start != buf) memmove(buf, start, rem + 1);
            pos = rem;
        }
        
        // Сторож operating_mode
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
    fprintf(stderr, "Usage: %s <start|stop|status|wave|brightness N|progress N|led ID N|daemon|oneshot ...>\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "start") == 0) return cmd_start();
    if (strcmp(cmd, "stop") == 0) return cmd_stop();
    if (strcmp(cmd, "wave") == 0) return cmd_wave();
    if (strcmp(cmd, "status") == 0) return cmd_status();
    if (strcmp(cmd, "daemon") == 0) return cmd_daemon();
    if (strcmp(cmd, "brightness") == 0 && argc >= 3) return cmd_brightness(atoi(argv[2]));
    if (strcmp(cmd, "progress") == 0 && argc >= 3) return cmd_progress(atoi(argv[2]));
    if (strcmp(cmd, "led") == 0 && argc >= 4) return cmd_led(atoi(argv[2]), atoi(argv[3]));
    
    if (strcmp(cmd, "oneshot") == 0 && argc >= 3) {
        char line[512] = {0};
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(line, " ", sizeof(line) - strlen(line) - 1);
            strncat(line, argv[i], sizeof(line) - strlen(line) - 1);
        }
        return execute_line(line);
    }
    usage(argv[0]);
    return 1;
}
