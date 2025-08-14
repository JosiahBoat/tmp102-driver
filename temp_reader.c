#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifndef I2C_BUS
#define I2C_BUS "/dev/i2c-5"           // Verify with: i2cdetect -y 5 (or 3)
#endif
#define TMP102_ADDR 0x48
#define TEMP_REG    0x00

#ifndef FIFO_PATH
#define FIFO_PATH   "/tmp/temp_pipe"   // You can also use "/run/temp_pipe"
#endif
#ifndef LOG_FILE
#define LOG_FILE    "/var/log/temp_log.txt"
#endif

static volatile sig_atomic_t keep_running = 1;
static void on_sigint(int s){ (void)s; keep_running = 0; }

static float tmp102_to_celsius(unsigned raw12) {
    // 12-bit two's complement
    if (raw12 & 0x800) {               // negative
        raw12 |= 0xF000;               // sign extend to 16 bits
        int16_t s = (int16_t)raw12;
        return s * 0.0625f;
    }
    return raw12 * 0.0625f;
}

static void iso8601_utc(char *buf, size_t n) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    struct tm tm;
    gmtime_r(&ts.tv_sec, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

int main(int argc, char **argv) {
    int i2c_fd = -1, fifo_fd = -1, log_fd = -1;
    int period_ms = (argc > 1) ? atoi(argv[1]) : 1000; // default 1s
    if (period_ms < 50) period_ms = 50;

    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    // Ensure FIFO exists (harmless if it already does)
    mkfifo(FIFO_PATH, 0666);

    // Open I2C
    i2c_fd = open(I2C_BUS, O_RDWR | O_CLOEXEC);
    if (i2c_fd < 0) { perror("open I2C"); return 1; }
    if (ioctl(i2c_fd, I2C_SLAVE, TMP102_ADDR) < 0) { perror("I2C_SLAVE"); close(i2c_fd); return 1; }

    // Open FIFO non-blocking so we don't stall if the GUI isn't running yet
    fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fifo_fd < 0 && errno != ENXIO) { perror("open FIFO"); close(i2c_fd); return 1; }

    // Open log file
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0) { perror("open log"); if (fifo_fd>=0) close(fifo_fd); close(i2c_fd); return 1; }

    char json[160], ts[32];
    unsigned char buf[2];
    while (keep_running) {
        // TMP102: write register pointer then read 2 bytes
        unsigned char reg = TEMP_REG;
        if (write(i2c_fd, &reg, 1) != 1 || read(i2c_fd, buf, 2) != 2) {
            perror("I2C read");
        } else {
            unsigned raw = ((unsigned)buf[0] << 8) | buf[1];
            unsigned raw12 = raw >> 4;
            float temp_c = tmp102_to_celsius(raw12);

            iso8601_utc(ts, sizeof ts);
            int len = snprintf(json, sizeof json,
                               "{\"ts\":\"%s\",\"temp_c\":%.2f,\"sensor\":\"tmp102\",\"addr\":\"0x48\"}\n",
                               ts, temp_c);

            // Write to FIFO if a reader is present
            if (fifo_fd >= 0) {
                ssize_t w = write(fifo_fd, json, len);
                if (w < 0 && (errno == EPIPE || errno == ENXIO)) {
                    // Reader disappeared; reopen later
                    close(fifo_fd);
                    fifo_fd = -1;
                }
            } else {
                // Try to reopen FIFO occasionally
                fifo_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                if (fifo_fd < 0 && errno != ENXIO) {
                    // real error; keep going
                }
            }

            // Always append to log (same JSON line)
            (void)write(log_fd, json, len);
        }

        // Sleep
        struct timespec req = { .tv_sec = period_ms/1000, .tv_nsec = (period_ms%1000)*1000000L };
        nanosleep(&req, NULL);
    }

    if (log_fd >= 0)  close(log_fd);
    if (fifo_fd >= 0) close(fifo_fd);
    if (i2c_fd >= 0)  close(i2c_fd);
    return 0;
}
