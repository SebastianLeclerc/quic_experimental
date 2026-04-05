/*
* MQTT-over-QUIC Publisher with Traffic Patterns
* Usage: pub <url> <qos> <topic> <bytes> <rate> <duration> <silent> -p|-s|-b
* Example: ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/1 10 10 30 1 -b
* Note: <rate> is capped at approx 10 000 (msg/s). <bytes> is capped at approx 100 000 (B).
*
* ---------------------------------------------------------------------------
* PERIODIC TRAFFIC (-p)
*
* Sends messages at a fixed, constant rate defined by <msg_per_sec>.
*
* If N is the number of messages per second, the inter-arrival time is constant:
*
*      Δt = 1 / N  seconds
*
* Example:
*      N = 100 msg/s → Δt = 0.01 s (10 ms)
*
* ---------------------------------------------------------------------------
* SPORADIC TRAFFIC (-s)
*
* Models *event-driven* or *alert-based* systems using a Poisson arrival process.
*
* Inter-arrival times follow an exponential distribution:
*
*      P(Δt > x) = e^(-λx)
*
* Where:
*      λ = msg_per_sec (expected arrival rate)
*
* Inter-arrival time sampling is performed using:
*
*      Δt = -ln(U) / λ
*
* where U is a uniform random variable U ∈ (0,1).
*
* ---------------------------------------------------------------------------
* BURSTY TRAFFIC (-b)
*
* Sends *bursts* of B messages every T seconds:
*
*      Every T seconds:
*          send B messages as fast as possible
*
* This approximates a *duty cycle* or *clustered event* pattern.
*
* Let:
*      B = burst_size, now 20 messages
*      T = burst_period, now 5 s
*
* The average rate is:
*
*      R_avg = B / T   messages per second
*
* Example:
*      B = 20, T = 5 → R_avg = 4 msg/s (but all in tight bursts)
*
* ---------------------------------------------------------------------------
* Payload is binary and contains:
*    [0..7]   : sequence number
*    [8..15]  : send timestamp in nanoseconds
*    [16..N]  : random binary payload up to approx. 100 000 (B).
* ---------------------------------------------------------------------------
*/

#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>

#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <math.h>

#define CONN 1
#define SUB  2
#define PUB  3

// =====================
// Traffic Patterns
// =====================
typedef enum {
    PATTERN_PERIODIC = 0,
    PATTERN_SPORADIC = 1,
    PATTERN_BURSTY   = 2
} traffic_pattern_t;

// =====================
// QUIC Configuration
// =====================
conf_quic config_user = {
    .tls = {
        .enable = false,
        .cafile = "",
        .certfile = "",
        .keyfile = "",
        .key_password = "",
        .verify_peer = true,
        .set_fail = true,
    },
    .multi_stream = false,
    .qos_first  = false,
    .qkeepalive = 10,
    .qconnect_timeout = 60,
    .qdiscon_timeout  = 30,
    .qidle_timeout    = 30,
};

// =====================
// Build binary payload
// =====================
uint8_t *build_message(uint64_t seq, size_t random_size, size_t *out_len)
{
    if (random_size < 16) {
        return NULL;  // payload too small for metadata
    }

    size_t msg_len = random_size;
    uint8_t *msg = malloc(msg_len);
    if (!msg)
        return NULL;

    // seq (8 bytes)
    msg[0] = (seq >> 56) & 0xFF;
    msg[1] = (seq >> 48) & 0xFF;
    msg[2] = (seq >> 40) & 0xFF;
    msg[3] = (seq >> 32) & 0xFF;
    msg[4] = (seq >> 24) & 0xFF;
    msg[5] = (seq >> 16) & 0xFF;
    msg[6] = (seq >> 8)  & 0xFF;
    msg[7] = seq & 0xFF;

    // timestamp (8 bytes)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t send_ns =
        (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    msg[8]  = (send_ns >> 56) & 0xFF;
    msg[9]  = (send_ns >> 48) & 0xFF;
    msg[10] = (send_ns >> 40) & 0xFF;
    msg[11] = (send_ns >> 32) & 0xFF;
    msg[12] = (send_ns >> 24) & 0xFF;
    msg[13] = (send_ns >> 16) & 0xFF;
    msg[14] = (send_ns >> 8)  & 0xFF;
    msg[15] = send_ns & 0xFF;

    // random padding
    for (size_t i = 0; i < msg_len - 16; i++) { //-16 due to lenght of seq and timestamp, fill rest with padding
        msg[16 + i] = rand() & 0xFF;
    }

    *out_len = msg_len;
    return msg;
}


// =====================
// MQTT Message Compose
// =====================
static nng_msg *mqtt_msg_compose(int type, int qos, char *topic,
                                 void *payload, size_t payload_len)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);

    if (type == CONN) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
        nng_mqtt_msg_set_connect_proto_version(msg, 4);
        nng_mqtt_msg_set_connect_keep_alive(msg, 10);
        nng_mqtt_msg_set_connect_clean_session(msg, true);
    }
    else if (type == PUB) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_dup(msg, 0);
        nng_mqtt_msg_set_publish_qos(msg, qos);
        nng_mqtt_msg_set_publish_retain(msg, 0);
        nng_mqtt_msg_set_publish_topic(msg, topic);

        if (payload && payload_len > 0)
            nng_mqtt_msg_set_publish_payload(msg, payload, payload_len);
    }

    return msg;
}

// =====================
// The Publisher Client
// =====================
int client(const char *url, const char *qos_str, const char *topic,
           size_t random_bytes, int msg_per_sec, int duration_sec,
           int silent, traffic_pattern_t pattern)
{
    uint64_t sent_count = 0;
    nng_socket sock;
    int rv;

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0)
        printf("error in quic client open.\n");

    // CONNECT
    nng_msg *msg = mqtt_msg_compose(CONN, 0, NULL, NULL, 0);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

    int qos = qos_str ? atoi(qos_str) : 0;
    if (qos < 0 || qos > 2) qos = 0;

    uint64_t seq = 0;

    struct timespec next;
    clock_gettime(CLOCK_REALTIME, &next);
    uint64_t interval_ns = 1000000000ULL / msg_per_sec;

    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += duration_sec;

    // Sporadic setup
    double lambda = (double)msg_per_sec;

    // Bursty setup
    int burst_size = 20;
    int burst_period = 5;
    uint64_t burst_period_ns = (uint64_t)burst_period * 1000000000ULL;

    struct timespec ts_now;
    clock_gettime(CLOCK_REALTIME, &ts_now);
    uint64_t now_ns = ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec;
    uint64_t next_burst_ns = now_ns + burst_period_ns;


    while (1) {
        clock_gettime(CLOCK_REALTIME, &ts_now);
        now_ns = ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec;

        // Break if duration exceeded
        if ((ts_now.tv_sec > end.tv_sec) ||
            (ts_now.tv_sec == end.tv_sec && ts_now.tv_nsec >= end.tv_nsec))
            break;

        // ===================================
        // PATTERN: PERIODIC
        // ===================================
        if (pattern == PATTERN_PERIODIC) {
            size_t payload_len;
            uint8_t *payload = build_message(seq++, random_bytes, &payload_len);

            msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload, payload_len);
            nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
            sent_count++;
            free(payload);

            next.tv_nsec += interval_ns;
            while (next.tv_nsec >= 1000000000ULL) {
                next.tv_nsec -= 1000000000ULL;
                next.tv_sec++;
            }
            clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);
            continue;
        }

        // ===================================
        // PATTERN: SPORADIC (Poisson arrivals)
        // ===================================
        if (pattern == PATTERN_SPORADIC) {
            double U = (double)rand() / (double)RAND_MAX;
            double inter = -log(1.0 - U) / lambda;
            uint64_t sleep_ns = (uint64_t)(inter * 1e9);

            struct timespec ts = {
                .tv_sec = sleep_ns / 1000000000ULL,
                .tv_nsec = sleep_ns % 1000000000ULL
            };
            nanosleep(&ts, NULL);

            clock_gettime(CLOCK_REALTIME, &ts_now);
            if (ts_now.tv_sec > end.tv_sec ||
                (ts_now.tv_sec == end.tv_sec &&
                ts_now.tv_nsec >= end.tv_nsec)) {
                break; //No late send so we get extra msg
            }

            size_t payload_len;
            uint8_t *payload = build_message(seq++, random_bytes, &payload_len);

            msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload, payload_len);
            nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
            sent_count++;
            free(payload);
            continue;
        }

        // ===================================
        // PATTERN: BURSTY (burst_size msgs every burst_period seconds)
        // ===================================
        if (pattern == PATTERN_BURSTY) {
            if (now_ns >= next_burst_ns) {
                for (int i = 0; i < burst_size; i++) {
                    size_t payload_len;
                    uint8_t *payload = build_message(seq++, random_bytes, &payload_len);

                    msg = mqtt_msg_compose(PUB, qos, (char *)topic, payload, payload_len);
                    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
                    sent_count++;
                    free(payload);
                }
                next_burst_ns = now_ns + burst_period_ns;
            }

            // prevent CPU burn
            struct timespec ts_sleep = {0, 5 * 1000000};
            nanosleep(&ts_sleep, NULL);
            continue;
        }
    }
    printf("Total messages sent: %lu\n", sent_count);
    double rate = (double)sent_count / duration_sec;
    printf("Average send rate: %.2f msgs/s\n", rate);
    return 0;
}


// =====================
// MAIN
// =====================
int main(int argc, char **argv)
{
    mlockall(MCL_CURRENT | MCL_FUTURE);

    // Start of experiment
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t send_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    printf("Start: %lu ns\n", send_ns);

    srand(time(NULL));

    if (argc < 10) {
        fprintf(stderr,
         "Usage: pub <url> <qos> <topic> <bytes> <rate> <duration> <silent> -p|-s|-b\n");
        return 0;
    }

    size_t random_size = strtoull(argv[5], NULL, 10);
    int msg_per_sec     = atoi(argv[6]);
    int duration_sec    = atoi(argv[7]);
    int silent          = atoi(argv[8]);

    // Pattern selection
    traffic_pattern_t pattern = PATTERN_PERIODIC;
    if (strcmp(argv[9], "-p") == 0) pattern = PATTERN_PERIODIC;
    else if (strcmp(argv[9], "-s") == 0) pattern = PATTERN_SPORADIC;
    else if (strcmp(argv[9], "-b") == 0) pattern = PATTERN_BURSTY;

    client(argv[2], argv[3], argv[4],
           random_size, msg_per_sec, duration_sec,
           silent, pattern);

    return 0;
}
