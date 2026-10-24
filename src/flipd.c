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

// Указатели на функции NDK
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
        printf("Sent to FIFO: %s\n", cmd);
    } else {
        printf("Error: could not open FIFO\n");
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
            printf("Proximity event: %f\n", distance); // Отладка
            
            // Считаем, что если дистанция < 5.0, то телефон перевернут (или закрыт рукой)
            int currently_flipped = (distance < 5.0f);
            
            if (currently_flipped && !is_flipped) {
                long long now = get_time_ms();
                // Защита от дребезга (не чаще раза в 2 секунды)
                if (now - last_flip_time > 2000) {
                    is_flipped = 1;
                    last_flip_time = now;
                    send_command("wave");
                }
            } else if (!currently_flipped && is_flipped) {
                is_flipped = 0;
                printf("Phone lifted\n"); // Отладка
            }
        }
    }
    return 1; // Продолжаем слушать
}

int main() {
    void *libandroid = dlopen("libandroid.so", RTLD_NOW);
    if (!libandroid) {
        fprintf(stderr, "Failed to load libandroid.so\n");
        return 1;
    }

    getInstance_t getInstance = (getInstance_t)dlsym(libandroid, "ASensorManager_getInstance");
    getDefaultSensor_t getDefaultSensor = (getDefaultSensor_t)dlsym(libandroid, "ASensorManager_getDefaultSensor");
    createQueue_t createQueue = (createQueue_t)dlsym(libandroid, "ASensorManager_createEventQueue");
    enableSensor_t enableSensor = (enableSensor_t)dlsym(libandroid, "ASensorEventQueue_enableSensor");
    getEvents = (getEvents_t)dlsym(libandroid, "ASensorEventQueue_getEvents");

    if (!getInstance || !getDefaultSensor || !createQueue || !enableSensor || !getEvents) {
        fprintf(stderr, "Failed to resolve sensor functions\n");
        return 1;
    }

    ASensorManager* sensorManager = getInstance();
    
    // Инициализация Looper
    ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    if (!looper) {
        fprintf(stderr, "Failed to prepare ALooper\n");
        return 1;
    }

    // Создаем очередь событий ДО получения сенсора
    eventQueue = createQueue(sensorManager, looper, ALOOPER_POLL_CALLBACK, (ALooper_callbackFunc)sensor_callback, NULL);
    if (!eventQueue) {
        fprintf(stderr, "Failed to create event queue\n");
        return 1;
    }

    const ASensor* proximitySensor = getDefaultSensor(sensorManager, ASENSOR_TYPE_PROXIMITY);
    if (!proximitySensor) {
        fprintf(stderr, "No proximity sensor found\n");
        return 1;
    }

    // Включаем датчик (200000 мкс = 5 Гц)
    if (enableSensor(eventQueue, proximitySensor, 200000) != 0) {
        fprintf(stderr, "Failed to enable sensor\n");
        return 1;
    }

    printf("flipd started. Waiting for flip (Ctrl+C to stop)...\n");

    while (1) {
        // Ждем событий от Looper. 1000 мс — таймаут, чтобы не блокироваться навсегда,
        // если датчик вдруг перестанет присылать данные.
        ALooper_pollOnce(1000, NULL, NULL, NULL);
    }

    return 0;
}
