#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

float get_cpu_usage()
{
    static long long prev_idle = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1;
    long long user, nice, system, idle, iowait, irq, softirq, steal;
    fscanf(fp, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
           &user, &nice, &system, &idle,
           &iowait, &irq, &softirq, &steal);
    fclose(fp);
    long long total = user + nice + system + idle +
                      iowait + irq + softirq + steal;
    long long total_diff = total - prev_total;
    long long idle_diff = idle - prev_idle;
    float cpu_usage = 0.0;
    if (prev_total != 0)
        cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
    prev_total = total;
    prev_idle = idle;
    return cpu_usage;
}

float get_cpu_temp()
{
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!fp)
        return -1;
    int temp;
    fscanf(fp, "%d", &temp);
    fclose(fp);
    return temp / 1000.0;
}

float get_gpu_usage()
{
    FILE *fp = popen(
        "nvidia-smi --query-gpu=utilization.gpu "
        "--format=csv,noheader,nounits 2>/dev/null",
        "r");
    if (!fp)
        return -1;
    float usage = -1;
    fscanf(fp, "%f", &usage);
    pclose(fp);
    return usage;
}

/*
 * On Linux, an STM32 USB CDC (Virtual COM Port) enumerates as a normal
 * tty device — typically /dev/ttyACM0 (not /dev/ttyUSB0, which is for
 * USB-to-UART bridge chips like the CP2102/FTDI).
 *
 * The open()/termios calls below work identically to a real UART.
 * Baud rate settings (cfsetospeed/ispeed) are effectively ignored by
 * USB CDC devices since data moves over USB packets, not an actual
 * serial clock — but they're kept here so this code also still works
 * unmodified if you ever point it at a real UART adapter.
 */
int usb_cdc_init(const char *port)
{
    int fd = open(port, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror("open");
        return -1;
    }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, B115200);   /* ignored by CDC, but set a sane value */
    cfsetispeed(&tty, B115200);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

int main()
{
    char tx[64];

    /* Change to your STM32's CDC device node.
     * Find it with: ls /dev/ttyACM*  (or `dmesg | tail` right after plugging in)
     */
    const char *port = "/dev/ttyACM1";

    int usb = usb_cdc_init(port);
    if (usb < 0)
    {
        fprintf(stderr, "Failed to open %s — is the STM32 plugged in and enumerated?\n", port);
        return -1;
    }

    get_cpu_usage();    // Prime the CPU calculation (first call is always 0)

    while (1)
    {
        sleep(1);

        float cpu = get_cpu_usage();
        float gpu = get_gpu_usage();
        float temp = get_cpu_temp();

        system("clear");
        printf("CPU : %.2f %%\n", cpu);
        printf("GPU : %.2f %%\n", gpu);
        printf("TEMP: %.2f C\n", temp);

        sprintf(tx, "%d,%d,%d\n",
                (int)cpu,
                (int)gpu,
                (int)temp);

        // snprintf(tx, sizeof(tx), "%.2f,%.2f,%.2f\n",
        //         cpu,
        //         gpu,
        //         temp);

        write(usb, tx, strlen(tx));
        printf("USB TX : %s", tx);
    }

    close(usb);
    return 0;
}
