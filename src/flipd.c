#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <time.h>
#include <android/sensor.h>
#include <android/looper.h>

#define FIFO_PATH "/data/local/tmp/glyph_fifo"

typedef ASensorManager* (*getInstance_t)();
typedef const ASensor* (*getDefaultSensor_t)(ASensorManager*, int);
typedef ASensorEventQueue* (*createQueue_t)(ASensorManager*, ALooper*, int, ALooper_callbackFunc, void*);
typedef int (*enableSensor_t)(ASensorEventQueue*, const ASensor*, int32_t);
typedef ssize_t (*getEvents_t)(ASensorEventQueue*, ASensorEvent*, size_t);

static ASensorEventQueue* eventQueue = NULL;
static getEvents_t getEvents = NULL;
static int is_flipped = 0;
static long long last_flip_time = 0;

void send_command(const char *cmd) {
    int fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
        write(fd, cmd, strlen(cmd));
        write(fd, "\n", 1);
        close(fd);
    }
}

long long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int sensor_callback(int fd, int events, void* data) {
    ASensorEvent event;
    while (getEvents(eventQueue, &event, 1) > 0) {
        if (event.type == ASENSOR_TYPE_PROXIMITY) {
            float distance = event.data[0];
            int currently_flipped = (distance < 5.0f);
            
            if (currently_flipped && !is_flipped) {
                long long now = get_time_ms();
                if (now - last_flip_time > 2000) {
                    is_flipped = 1;
                    last_flip_time = now;
                    send_command("wave"); // По умолчанию шлем wave
                }
            } else if (!currently_flipped && is_flipped) {
                is_flipped = 0;
            }
        }
    }
    return 1;
}

int main() {
    // 1. Защита от убийства системой (OOM Killer)
    int oom_fd = open("/proc/self/oom_score_adj", O_WRONLY);
    if (oom_fd >= 0) {
        write(oom_fd, "-1000", 5);
        close(oom_fd);
    }

    void *libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (!libandroid) { fprintf(stderr, "Failed to load libandroid.so\n"); return 1; }

    getInstance_t getInstance = (getInstance_t)dlsym(libandroid, "ASensorManager_getInstance");
    getDefaultSensor_t getDefaultSensor = (getDefaultSensor_t)dlsym(libandroid, "ASensorManager_getDefaultSensor");
    createQueue_t createQueue = (createQueue_t)dlsym(libandroid, "ASensorManager_createEventQueue");
    enableSensor_t enableSensor = (enableSensor_t)dlsym(libandroid, "ASensorEventQueue_enableSensor");
    getEvents = (getEvents_t)dlsym(libandroid, "ASensorEventQueue_getEvents");

    if (!getInstance || !getDefaultSensor || !createQueue || !enableSensor || !getEvents) return 1;

    ASensorManager* sensorManager = getInstance();
    ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    eventQueue = createQueue(sensorManager, looper, ALOOPER_POLL_CALLBACK, (ALooper_callbackFunc)sensor_callback, NULL);
    
    const ASensor* proximitySensor = getDefaultSensor(sensorManager, ASENSOR_TYPE_PROXIMITY);
    if (!proximitySensor) return 1;

    enableSensor(eventQueue, proximitySensor, 200000); 
    printf("flipd started. Waiting for flip...\n");

    while (1) {
        ALooper_pollOnce(1000, NULL, NULL, NULL);
    }
    return 0;
}
