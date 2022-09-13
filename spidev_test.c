/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BUF_MAX_SIZE 256

static const char *s_device   = "/dev/spidev1.0";
static uint8_t     s_mode     = 0;
static uint8_t     s_bits     = 8;
static uint32_t    s_speed    = 1000000;
static uint16_t    s_delay_us = 20;
static uint8_t     s_size;
static uint8_t     s_tx_buf[BUF_MAX_SIZE];
static uint8_t     s_rx_buf[BUF_MAX_SIZE];

static void print_usage(const char *prog)
{
    printf("Usage: %s [-DsbdlHOLC3] [X] \n", prog ? prog : "");
    printf("  -D --device   device to use (default /dev/spidev1.0)\n"
           "  -s --speed    max speed (Hz)\n"
           "  -d --delay    delay (usec)\n"
           "  -b --bpw      bits per word \n"
           "  -l --loop     loopback\n"
           "  -H --cpha     clock phase\n"
           "  -O --cpol     clock polarity\n"
           "  -L --lsb      least significant bit first\n"
           "  -C --cs-high  chip select active high\n"
           "  -3 --3wire    SI/SO signals shared\n"
           "  -X --xdata    hexadecimal data\n"
           "./spidev_test -D /dev/spidev1.0 -s 1000000 -b 8 -X 0xaa 0xbb 0xcc\n");

    if (prog)
    {
        exit(EXIT_FAILURE);
    }
}

static void pabort(const char *s)
{
    print_usage(NULL);
    perror(s);
    abort();
}

static void transfer(int fd)
{
    int i;

    struct spi_ioc_transfer transfer = {
        .tx_buf        = (unsigned long)s_tx_buf,
        .rx_buf        = (unsigned long)s_rx_buf,
        .len           = s_size,
        .delay_usecs   = s_delay_us,
        .speed_hz      = s_speed,
        .bits_per_word = s_bits,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &transfer) < 1)
    {
        pabort("Failed to send spi message");
    }

    printf("TX: ");
    for (i = 0; i < s_size; i++)
    {
        printf("%.2x ", s_tx_buf[i]);
    }
    printf("\r\n");

    printf("RX: ");
    for (i = 0; i < s_size; i++)
    {
        printf("%.2x ", s_rx_buf[i]);
    }
    printf("\r\n");
}

static void parse_opts(int argc, char *argv[])
{
    int   i, index;
    char *p;

    while (1)
    {
        int                        c;
        static const struct option opts[] = {
            {"device", 1, 0, 'D'},  {"speed", 1, 0, 's'}, {"delay", 1, 0, 'd'}, {"bpw", 1, 0, 'b'},
            {"loop", 0, 0, 'l'},    {"cpha", 0, 0, 'H'},  {"cpol", 0, 0, 'O'},  {"lsb", 0, 0, 'L'},
            {"cs-high", 0, 0, 'C'}, {"3wire", 0, 0, '3'}, {"no-cs", 0, 0, 'N'}, {"ready", 0, 0, 'R'},
            {"Xdata", 1, 0, 'X'},   {NULL, 0, 0, 0},
        };

        c = getopt_long(argc, argv, "D:s:d:b:lHOLC3NRX", opts, NULL);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case 'D':
            s_device = optarg;
            break;
        case 's':
            s_speed = (uint32_t)atoi(optarg);
            break;
        case 'd':
            s_delay_us = (uint16_t)atoi(optarg);
            break;
        case 'b':
            s_bits = (uint8_t)atoi(optarg);
            break;
        case 'l':
            s_mode |= SPI_LOOP;
            break;
        case 'H':
            s_mode |= SPI_CPHA;
            break;
        case 'O':
            s_mode |= SPI_CPOL;
            break;
        case 'L':
            s_mode |= SPI_LSB_FIRST;
            break;
        case 'C':
            s_mode |= SPI_CS_HIGH;
            break;
        case '3':
            s_mode |= SPI_3WIRE;
            break;
        case 'N':
            s_mode |= SPI_NO_CS;
            break;
        case 'R':
            s_mode |= SPI_READY;
            break;
        case 'X':
            i      = 0;
            s_size = argc - optind;

            if (s_size > BUF_MAX_SIZE)
            {
                printf("The hex data is too long");
                exit(EXIT_FAILURE);
            }

            for (index = optind; index < argc; index++, i++)
            {
                s_tx_buf[i] = (uint8_t)strtol(argv[index], &p, 0);
            }
            break;
        default:
            print_usage(argv[0]);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int fd;

    parse_opts(argc, argv);

    fd = open(s_device, O_RDWR);
    if (fd < 0)
    {
        pabort("Failed open SPI device");
    }

    /*
     * spi mode
     */
    if (ioctl(fd, SPI_IOC_WR_MODE, &s_mode) == -1)
    {
        pabort("Failed to set spi mode");
    }

    if (ioctl(fd, SPI_IOC_RD_MODE, &s_mode) == -1)
    {
        pabort("Failed to get spi mode");
    }

    /*
     * bits per word
     */
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &s_bits) == -1)
    {
        pabort("Failed to set bits per word");
    }

    if (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &s_bits) == -1)
    {
        pabort("Failed to get bits per word");
    }

    /*
     * max speed hz
     */
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &s_speed) == -1)
    {
        pabort("Failed to set max speed hz");
    }

    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &s_speed) == -1)
    {
        pabort("Failed to get max speed hz");
    }

    printf("spi mode: %d\n", s_mode);
    printf("bits per word: %d\n", s_bits);
    printf("max speed: %d Hz (%d KHz)\n", s_speed, s_speed / 1000);

    transfer(fd);

    close(fd);

    return 0;
}
