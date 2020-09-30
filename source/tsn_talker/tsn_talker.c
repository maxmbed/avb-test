/*
 * Copyright (c) 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Intel Corporation nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <alloca.h>
#include <argp.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <sndfile.h>

#include "log.h"

#define MAGIC 0xCC

static uint8_t ifname[IFNAMSIZ];
static uint8_t macaddr[ETH_ALEN];
static int priority = -1;
static size_t size = 1500;
static uint64_t seq;
static int delay = 0;
static char wav_file_path[PATH_MAX];

static struct argp_option options[] = {
    {"dst-addr", 'd', "MACADDR", 0, "Stream Destination MAC address" },
    {"delay", 'D', "NUM", 0, "Delay (in us) between packet transmission" },
    {"ifname", 'i', "IFNAME", 0, "Network Interface" },
    {"prio", 'p', "NUM", 0, "SO_PRIORITY to be set in socket" },
    {"packet-size", 's', "NUM", 0, "Size of packets to be transmitted" },
    {"wav file path", 'w', "WAV", 0, "The audio file to send in .wav format"},
    { 0 }
};

static error_t parser(int key, char *arg, struct argp_state *state)
{
    int res;

    switch (key) {
    case 'd':
        res = sscanf(arg, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    &macaddr[0], &macaddr[1], &macaddr[2],
                    &macaddr[3], &macaddr[4], &macaddr[5]);
        if (res != 6) {
            err("Invalid address\n");
            exit(EXIT_FAILURE);
        }

        break;
    case 'D':
        delay = atoi(arg);
        break;
    case 'i':
        strncpy((char*)ifname, arg, sizeof(ifname) - 1);
        break;
    case 'p':
        priority = (size_t)atoi(arg);
        break;
    case 's':
        size = (size_t)atoi(arg);
        break;
    case 'w':
        strncpy(wav_file_path, arg, sizeof(wav_file_path) - 1);
        break;
    }

    return 0;
}

static struct argp argp = { options, parser };

typedef struct snd_file {

    SF_INFO     info;
    SNDFILE*    handler;
    int*        buffer;
} snd_file_t;

int audio_open_file(snd_file_t* sndfile, char* file_path) {

    sndfile->handler = sf_open(wav_file_path, SFM_READ, &sndfile->info);
    if (sndfile->handler == NULL) {
        err("Open audio file\n");
        return -1;
    }

    sndfile->buffer = malloc(sndfile->info.frames*sndfile->info.channels);
    if (sndfile->buffer == NULL) {
        err("Allocate audio buffer: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

long int audio_read_file(snd_file_t* sndfile, sf_count_t num_frames) {

    sf_count_t frame_count;

    frame_count = sf_readf_int(sndfile->handler, sndfile->buffer, num_frames);
//    if (frame_count != num_frames) {
//        err("Read audio file\n");
//        return -1;
//    }
    return frame_count;
}

int main(int argc, char *argv[])
{
    int fd, res;
    struct ifreq req;
    uint8_t *data;
    struct sockaddr_ll sk_addr = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_TSN),
        .sll_halen = ETH_ALEN,
    };
    snd_file_t sndfile;

    argp_parse(&argp, argc, argv, 0, NULL, NULL);

    if (audio_open_file(&sndfile, wav_file_path) < 0) {
        return -1;
    }

    fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_TSN));
    if (fd < 0) {
        err("Couldn't open socket: %s\n", strerror(errno));
        return 1;
    }

    strncpy(req.ifr_name, (char*)ifname, sizeof(req.ifr_name));
    res = ioctl(fd, SIOCGIFINDEX, &req);
    if (res < 0) {
        err("Couldn't get interface index %s\n", strerror(errno));
        goto err;
    }

    sk_addr.sll_ifindex = req.ifr_ifindex;
    memcpy(&sk_addr.sll_addr, macaddr, ETH_ALEN);

    if (priority != -1) {
        res = setsockopt(fd, SOL_SOCKET, SO_PRIORITY, &priority,
                            sizeof(priority));
        if (res < 0) {
            err("Couldn't set priority: %s\n", strerror(errno));
            goto err;
        }

    }

    data = alloca(size);
    memset(data, MAGIC, size);
    long int counter = 0;
    info("Sending packets...\n");

    while (audio_read_file(&sndfile, size) > 0) {

        uint64_t *seq_ptr = (uint64_t *) &data[0];
        ssize_t n;

        *seq_ptr = seq++;

        info("section %ld/%ld\n", counter, sndfile.info.frames);
        n = sendto(fd, sndfile.buffer, size, 0, (struct sockaddr *) &sk_addr,
                            sizeof(sk_addr));
        if (n < 0)
            err("Failed to send data: %s\n", strerror(errno));

        if (delay > 0)
            usleep((unsigned int)delay);

        counter = counter + size;
    }

    close(fd);
    sf_close(sndfile.handler);
    return 0;

err:
    close(fd);
    sf_close(sndfile.handler);
    return 1;
}
