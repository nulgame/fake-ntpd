#include <arpa/inet.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// 01/01/1900 is 1st NTP day
#define UTC_NTP 2208988800U // 1970 - 1900
#define BUF_SIZ (48)
#define TIME_TICK_SECS (10)

typedef struct timeval tv_t;

uint32_t time_stamp[2];
time_t new_time; // just for local logging only
int debug = 0;
int speed = 1; // 1 or 3 x TIME_TICK_SECS

/*
 *  See: https://www.rfc-editor.org/rfc/rfc5905 and
    https://github.com/jagd/ntp/blob/master/server.c

    UDP datagram, NTPv4
    +-----------+------------+-----------------------+------------------------+
    | Name      | Formula    | Description           | data type              |
    +-----------+------------+-----------------------+------------------------+
    | leap      | leap       | leap indicator (LI)   | char
    | version   | version    | version number (VN)   | char
    | mode      | mode       | mode           (MD)   | char
    | stratum   | stratum    | stratum               | char
    | poll      | poll       | poll exponent         | char
    | precision | rho        | precision exponent    | signed char            |
    | rootdelay | delta_r    | root delay            | unsigned int
    | rootdisp  | epsilon_r  | root dispersion       | unsigned int
    | refid     | refid      | reference ID          | char
    | reftime   | reftime    | reference timestamp   | ull unsigned long long |
    | org       | T1         | origin timestamp      | ull
    | rec       | T2         | receive timestamp     | ull
    | xmt       | T3         | transmit timestamp    | ull
    | dst       | T4         | destination timestamp | ull
    | keyid     | keyid      | key ID                | int
    | dgst      | dgst       | message digest        | unsigned long
    +-----------+------------+-----------------------+------------------------+
    Figure 7: Packet Header Variables

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |LI | VN  |Mode |    Stratum     |     Poll      |  Precision   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Root Delay  (32)                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Root Dispersion (32)                     |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |                      Reference ID  (32)                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   +                      Reference Timestamp (64)                 +
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   +                      Origin Timestamp (64)                    +
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   +                      Receive Timestamp (64)                   +
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   +                      Transmit Timestamp (64)                  +
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   .         Optional   Extension Field 1 (variable)               .

   Figure 8: Packet Header Format
*/

void get_fake_time64_le(uint32_t ts[])
{
    ts[0] = time_stamp[0]; // time_stamp is global
    ts[1] = 0;
}

void dec_to_bin(uint8_t num, char **buf, uint8_t buf_siz)
{
    memset(*buf, 0, 8);
    int digits[7];
    short i = 7;

    while (num > 0) {
        digits[i--] = num % 2;
        num = num / 2;
    }

    while (i >= 0)
        digits[i--] = 0; // fill up to 8 characters

    for (int j = 0; j < 8; j++)
        (*buf)[j] = digits[j] == 1 ? '1' : '.';

    (*buf)[8] = '\0';
}

void log_request(struct sockaddr_in *client_addr)
{
    time_t t = time(NULL);
    char buf[255];
    memset(buf, 0, 255);

    sprintf(buf + strlen(buf), "\n-> Connection from %s:%d\n",
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
    sprintf(buf + strlen(buf), "   good time: %s", ctime(&t));
    sprintf(buf + strlen(buf), "   fake time: %s", ctime(&new_time));

    printf("%s", buf);
}

void dump_data(uint8_t *buffer, size_t buf_siz, const char *title)
{
    if (!debug)
        return;

    char bits[8 + 1]; // debug
    char *p = bits;   // debug
    char debug[1024];
    memset(debug, 0, 1024);

    // sprintf(debug, "DEBUG-->[%s]:\n", title);
    for (int i = 0; i < buf_siz;) {
        if ((i % 4) == 0)
            sprintf(debug + strlen(debug), "[%02d]  ", i);
        for (int j = 0; j < 4; j++, i++) {
            dec_to_bin(buffer[i], &p, 9); // debug
            sprintf(debug + strlen(debug), "%s [%4d]  ", bits, buffer[i]);
        }
        sprintf(debug + strlen(debug), "\n");
    }
    printf("TRACE-->[%s]:\n%s<--TRACE\n", title, debug);
}

int prepare_reply(unsigned char recv_buf[], unsigned char send_buf[],
                  uint32_t recv_time[])
{
    uint32_t *ptr;

    // LI = 0 VN = 4, mode = 4 (ntp service)
    send_buf[0] = (recv_buf[0] & 0x38) + 4;

    // Stratum = 1 (primary reference)
    send_buf[1] = 0x01;

    // Reference ID = 'LOCL" local server L=4C O=4F C=43 L=4C
    *(uint32_t *)&send_buf[12] = htonl(0x4C4F434C);

    send_buf[2] = recv_buf[2]; // copy 'poll' from request msg

    // Precision in microsecond based on gettimeofday()
    send_buf[3] = (signed char)(-6); // 2^(-6) sec

    ptr = (uint32_t *)&send_buf[4]; // from here align byes to dword

    // just for simplicity:
    *ptr++ = 0; // root delay
    *ptr++ = 0; // root dispersion

    ptr++; // reference ID was entered earlier

    // Incorrect Reference TimeStamp, sync 1 min earlier
    get_fake_time64_le(ptr);
    *ptr = htonl(*ptr - 60); // -1 min
    ptr++;
    *ptr = htonl(*ptr); // -1 min
    ptr++;

    // Originate Time = Transmit Time @ Client
    *ptr++ = *(uint32_t *)&recv_buf[40];
    *ptr++ = *(uint32_t *)&recv_buf[44];

    // Receive Time @ Server
    *ptr++ = htonl(recv_time[0]);
    *ptr++ = htonl(recv_time[1]);

    // Transmit time
    get_fake_time64_le(ptr);
    *ptr = htonl(*ptr);
    ptr++;
    *ptr = htonl(*ptr);

    return 0;
}

int base_check(uint8_t byte0)
{
#define MASK_7B (0x7)
    // Mode mask    .....111  : >> 0 = .....111
    // Version mask ..111...  : >> 3 = .....111

    //  recived byte 0
    //          LI VER MOD
    //      MSB .. ... ... LSB
    //          LI
    //             VER = 4 (current ntp protocol version - VN)
    //                 MOD = 3-> client request, 4-> server replay
    //  +-------+----------------------------------------+
    //  | Value | Meaning                                |
    //  +-------+----------------------------------------+
    //  | 0     | no warning                             |
    //  | 1     | last minute of the day has 61 seconds  |
    //  | 2     | last minute of the day has 59 seconds  |
    //  | 3     | unknown (clock unsynchronized)         |
    //  +-------+----------------------------------------+
    //  Figure 9: Leap Indicator

    if ((byte0 & MASK_7B) != 0x3) { // not a ntp client (3)
        fprintf(stderr, "INVALID-REQUEST: NOT-A-NTP-CLIENT");
        return 1;
    }

    if (((byte0 >> 3) & MASK_7B) != 0x4) { // not a good version (4)
        fprintf(stderr, "INVALID-REQUEST: REQUIRED-NTP-VERSION = 4");
        return 2;
    }
    return 0;
}

void ntp_service(const char *ip, unsigned short port)
{
    struct sockaddr_in service_addr, client_addr;
    socklen_t addr_size;
    unsigned char recv_buf[BUF_SIZ];
    unsigned char send_buf[BUF_SIZ];
    uint32_t recv_time[2];

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("SOCKET-ERROR");
        exit(1);
    }

    memset(&service_addr, 0, sizeof(service_addr));
    service_addr.sin_family = AF_INET;
    service_addr.sin_port = htons(port);
    service_addr.sin_addr.s_addr = inet_addr(ip);

    int n = bind(s, (struct sockaddr *)&service_addr, sizeof(service_addr));
    if (n < 0) {
        perror("BIND-ERROR");
        exit(1);
    }

    while (1) {
        memset(recv_buf, 0, BUF_SIZ);
        addr_size = sizeof(client_addr);
        // incloming request
        recvfrom(s, recv_buf, BUF_SIZ, 0, (struct sockaddr *)&client_addr,
                 &addr_size);

        // client IP
        getpeername(s, (struct sockaddr *)&client_addr, &addr_size);
        log_request(&client_addr); // on screen

        n = base_check(recv_buf[0]);
        if (n != 0) {
            sendto(s, "ERR", 3, 0, (struct sockaddr *)&client_addr, addr_size);
            continue;
        }

        dump_data(recv_buf, BUF_SIZ, "client sent"); // debug

        get_fake_time64_le(recv_time);

        n = prepare_reply(recv_buf, send_buf, recv_time);

        dump_data(send_buf, BUF_SIZ, "server sent");

        if (sendto(s, send_buf, sizeof(send_buf), 0,
                   (struct sockaddr *)&client_addr, addr_size) < BUF_SIZ) {
            perror("SENDTO-ERROR");
        }
    }
    close(s);
}

void *fake_clock(void *arg)
{
    if (debug)
        printf("Clock thread is running.\n");

    long secs = 0;     // fake clock
    long secs_log = 0; // local log
    tv_t tv;           // storing fake seconds

    gettimeofday(&tv, NULL); // tv is global
    secs = tv.tv_sec + UTC_NTP;
    secs_log = tv.tv_sec;

    while (1) {
        secs += (TIME_TICK_SECS * speed); // clock tick :-)
        time_stamp[0] = secs;
        time_stamp[1] = 0;

        secs_log += (TIME_TICK_SECS * speed); // clock tick :-)
        new_time = secs_log;                  // log
        // sleep(1);
        usleep(500000); // half sec
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int rc = 0;
    uint8_t port = 123;   // default port for NTP
    char *ip = "0.0.0.0"; // allow all ifaces

    int optch;
    static char optstring[] = "dx"; // -d for tracing bits
                                    // -x need for speed 1min per 1 sec
    while ((optch = getopt(argc, argv, optstring)) != -1)
        switch (optch) {
            case 'd':
                debug = 1;
                break;
            case 'x':
                speed = 3;
                break;
            default:
                break;
        }

    printf("fake ntpd service is starting on port: UDP/%d ..\n", port);

    // Start clock
    pthread_t clock_thread;
    pthread_create(&clock_thread, NULL, fake_clock, NULL);

    sleep(1);
    // Run service
    ntp_service(ip, port);

    pthread_join(clock_thread, NULL);

    return rc;
}
