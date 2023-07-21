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
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BUF_MAX_SIZE 1024

static const char *s_device   = "/dev/spidev1.0";
static uint8_t     s_mode     = 0;
static uint8_t     s_bits     = 8;
static uint32_t    s_speed    = 1000000;
static uint16_t    s_delay_us = 20;
static uint32_t    s_size     = 0;
static uint8_t     s_tx_buf[BUF_MAX_SIZE];
static uint8_t     s_rx_buf[BUF_MAX_SIZE];
static uint32_t    s_repeat      = 1;
static uint32_t    s_interva_ms  = 10;
static uint8_t     s_file_is_set = 0;
static char        s_file_path[128];

static uint8_t s_default_data[] = {0xfd, 0x01, 0x51, 0xa7};

static void print_usage(const char *prog)
{
    printf("Usage: %s [-DsbdlrifHOLC3] [X] \n", prog ? prog : "");
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
           "  -r --repeat   repeatly transmit frames\n"
           "  -i --interval repeat interval, in ms\n"
           "  -f --file     read spi frames from the file\n"
           "  -X --xdata    hexadecimal data\n\n"
           "Examples:\n"
           "  ./spidev_test -D /dev/spidev1.0 -s 1000000 -b 8 -r 2 -i 100 -X 0xaa 0xbb 0xcc\n"
           "  ./spidev_test -D /dev/spidev1.0 -s 1000000 -b 8 -f ./example.cfg\n");

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

static void strip(char *string)
{
    int count = 0;

    for (int i = 0; string[i]; i++)
    {
        if (string[i] != '\r' && string[i] != '\n')
        {
            string[count++] = string[i];
        }
    }

    string[count] = '\0';
}

static int hex_to_bin(const char *hex, uint8_t *bin, uint32_t bin_length)
{
    size_t      hexLength = strlen(hex);
    const char *hexEnd    = hex + hexLength;
    uint8_t    *cur       = bin;
    uint8_t     numChars  = hexLength & 1;
    uint8_t     byte      = 0;
    int         rval;

    if ((hexLength + 1) / 2 > bin_length)
    {
        return -1;
    }

    while (hex < hexEnd)
    {
        if ('A' <= *hex && *hex <= 'F')
        {
            byte |= 10 + (*hex - 'A');
        }
        else if ('a' <= *hex && *hex <= 'f')
        {
            byte |= 10 + (*hex - 'a');
        }
        else if ('0' <= *hex && *hex <= '9')
        {
            byte |= *hex - '0';
        }
        else if (*hex == ' ')
        {
            hex++;
            continue;
        }
        else
        {
            printf("Unknown Character (0x%02x|%c)", *hex, *hex);
            return -1;
        }

        hex++;
        numChars++;

        if (numChars >= 2)
        {
            numChars = 0;
            *cur++   = byte;
            byte     = 0;
        }
        else
        {
            byte <<= 4;
        }
    }

    rval = (int)(cur - bin);

    return rval;
}

static int config_file_get_next(int *iterator, uint8_t *value, uint32_t *value_length)
{
    char     line[BUF_MAX_SIZE + 1];
    FILE    *fp = NULL;
    long int pos;
    int      len;

    if ((iterator == NULL) || (value == NULL) || (value_length == NULL))
    {
        return -1;
    }

    if ((fp = fopen(s_file_path, "r")) == NULL)
    {
        return -1;
    }

    if (fseek(fp, *iterator, SEEK_SET) < 0)
    {
        return -1;
    }

    if (fgets(line, sizeof(line), fp) == NULL)
    {
        return -1;
    }

    if (strlen(line) + 1 == sizeof(line))
    {
        // The line is too long.
        return -1;
    }

    if ((pos = ftell(fp)) < 0)
    {
        return -1;
    }

    if (fp != NULL)
    {
        fclose(fp);
    }

    strip(line);
    len = hex_to_bin(line, value, *value_length);

    if (len < 0)
    {
        return -1;
    }

    *iterator     = (int)(pos);
    *value_length = len;
    return 0;
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
            {"device", 1, 0, 'D'},  {"speed", 1, 0, 's'},  {"delay", 1, 0, 'd'},    {"bpw", 1, 0, 'b'},
            {"loop", 0, 0, 'l'},    {"cpha", 0, 0, 'H'},   {"cpol", 0, 0, 'O'},     {"lsb", 0, 0, 'L'},
            {"cs-high", 0, 0, 'C'}, {"3wire", 0, 0, '3'},  {"no-cs", 0, 0, 'N'},    {"ready", 0, 0, 'R'},
            {"Xdata", 1, 0, 'X'},   {"repeat", 1, 0, 'r'}, {"interval", 1, 0, 'i'}, {"file", 1, 0, 'f'},
            {NULL, 0, 0, 0},
        };

        c = getopt_long(argc, argv, "D:r:i:s:d:b:f:lHOLC3NRX", opts, NULL);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
        case 'D':
            s_device = optarg;
            break;
        case 'f':
            s_file_is_set = 1;
            memset(s_file_path, 0, sizeof(s_file_path));
            strncpy(s_file_path, optarg, sizeof(s_file_path) - 1);
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
        case 'i':
            s_interva_ms = (uint32_t)atoi(optarg);
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
        case 'r':
            s_repeat = (uint32_t)atoi(optarg);
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

    if (s_size == 0)
    {
        memcpy(s_tx_buf, s_default_data, sizeof(s_default_data));
        s_size = sizeof(s_default_data);
    }
}

int main(int argc, char *argv[])
{
    int fd;
    int index    = 0;
    int iterator = 0;

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

    for (uint32_t i = 0; i < s_repeat; i++)
    {
        if (s_file_is_set)
        {
            s_size = sizeof(s_tx_buf);
            while (config_file_get_next(&iterator, s_tx_buf, &s_size) >= 0)
            {
                printf("\n%d.%d\n", i, index++);
                transfer(fd);
                usleep(s_interva_ms * 1000);
                s_size = sizeof(s_tx_buf);
            }
        }
        else
        {
            printf("\n%d\n", i++);
            transfer(fd);
            usleep(s_interva_ms * 1000);
        }
    }

    close(fd);

    return 0;
}
