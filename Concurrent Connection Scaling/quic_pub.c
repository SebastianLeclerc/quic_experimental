#include <nng/nng.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>
#include <nng/supplemental/util/platform.h>

#include "msquic.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <stdbool.h>

/* =====================
 * Traffic patterns
 * ===================== */

typedef enum {
    PATTERN_PERIODIC,
    PATTERN_SPORADIC,
    PATTERN_BURST
} traffic_pattern_t;

/* =====================
 * QUIC configuration
 * ===================== */

static conf_quic quic_conf = {
    .tls = {
        .enable = false,
        .verify_peer = true,
        .set_fail = true,
    },
    .multi_stream = false, //True if parallel streams, HoL avoidance
    .qos_first  = false,
    .qkeepalive = 10,
    .qconnect_timeout = 60,
    .qdiscon_timeout  = 30,
    .qidle_timeout    = 30,
};

/* =====================
 * Time helper
 * ===================== */

static inline uint64_t
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* =====================
 * Payload builder
 * ===================== */

static uint8_t *
build_message(uint64_t seq, size_t total_size, size_t *out_len)
{
    if (total_size < 16)
        return NULL;

    uint8_t *buf = malloc(total_size);
    if (!buf)
        return NULL;

    uint64_t ts = now_ns();

    memcpy(buf, &seq, 8);
    memcpy(buf + 8, &ts, 8);

    for (size_t i = 16; i < total_size; i++)
        buf[i] = rand() & 0xff;

    *out_len = total_size;
    return buf;
}

/* =====================
 * Fatal helper
 * ===================== */

static void
fatal(const char *where, int rv)
{
    fprintf(stderr, "%s: %s\n", where, nng_strerror(rv));
    exit(1);
}

/* =====================
 * Publisher loop
 * ===================== */

static void
run_publisher(
    const char *url,
    uint8_t qos,
    const char *topic,
    traffic_pattern_t pattern)
{
    int rv;
    nng_socket sock;

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &quic_conf)) != 0)
        fatal("nng_mqtt_quic_client_open_conf", rv);

    /* CONNECT */
    nng_msg *conn;
    nng_mqtt_msg_alloc(&conn, 0);
    nng_mqtt_msg_set_packet_type(conn, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(conn, 4);
    nng_mqtt_msg_set_connect_clean_session(conn, true);
    nng_sendmsg(sock, conn, NNG_FLAG_ALLOC);

    /* Experiment parameters (env‑only) */
    int     msgps    = getenv("MSGPS")    ? atoi(getenv("MSGPS"))    : 10;
    int     duration = getenv("DURATION") ? atoi(getenv("DURATION")) : 30;
    size_t  size     = getenv("SIZE")     ? atoi(getenv("SIZE"))     : 256;

    int burst_size =
        getenv("BURST_SIZE") ? atoi(getenv("BURST_SIZE")) : 100;
    int burst_period =
        getenv("BURST_PERIOD") ? atoi(getenv("BURST_PERIOD")) : 1;

    if (size < 16) {
        fprintf(stderr, "SIZE must be >= 16 bytes\n");
        exit(1);
    }

    uint64_t seq = 0;
    uint64_t sent = 0;

    uint64_t start_ns = now_ns();
    uint64_t end_ns   = start_ns + (uint64_t)duration * 1000000000ULL;
    uint64_t interval_ns =
        msgps > 0 ? (1000000000ULL / msgps) : 0;

    uint64_t next_ns = start_ns;
    uint64_t next_burst_ns = start_ns;

    while (now_ns() < end_ns) {

        if (pattern == PATTERN_PERIODIC) {

            size_t len;
            uint8_t *payload = build_message(seq++, size, &len);
            if (!payload) continue;

            nng_msg *pub;
            nng_mqtt_msg_alloc(&pub, 0);
            nng_mqtt_msg_set_packet_type(pub, NNG_MQTT_PUBLISH);
            nng_mqtt_msg_set_publish_qos(pub, qos);
            nng_mqtt_msg_set_publish_retain(pub, 0);
            nng_mqtt_msg_set_publish_topic(pub, topic);
            nng_mqtt_msg_set_publish_payload(pub, payload, len);

            nng_sendmsg(sock, pub, NNG_FLAG_ALLOC);
            sent++;
            free(payload);

            next_ns += interval_ns;
            struct timespec ts = {
                .tv_sec  = next_ns / 1000000000ULL,
                .tv_nsec = next_ns % 1000000000ULL
            };
            clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);
        }

        else if (pattern == PATTERN_SPORADIC) {

            double u = (double)rand() / RAND_MAX;
            double delay = -log(1.0 - u) / msgps;
            uint64_t ns = (uint64_t)(delay * 1e9);

            struct timespec ts = {
                .tv_sec  = ns / 1000000000ULL,
                .tv_nsec = ns % 1000000000ULL
            };
            nanosleep(&ts, NULL);

            size_t len;
            uint8_t *payload = build_message(seq++, size, &len);
            if (!payload) continue;

            nng_msg *pub;
            nng_mqtt_msg_alloc(&pub, 0);
            nng_mqtt_msg_set_packet_type(pub, NNG_MQTT_PUBLISH);
            nng_mqtt_msg_set_publish_qos(pub, qos);
            nng_mqtt_msg_set_publish_topic(pub, topic);
            nng_mqtt_msg_set_publish_payload(pub, payload, len);

            nng_sendmsg(sock, pub, NNG_FLAG_ALLOC);
            sent++;
            free(payload);
        }

        else if (pattern == PATTERN_BURST) {

            uint64_t now = now_ns();
            if (now >= next_burst_ns) {
                for (int i = 0; i < burst_size; i++) {
                    size_t len;
                    uint8_t *payload = build_message(seq++, size, &len);
                    if (!payload) continue;

                    nng_msg *pub;
                    nng_mqtt_msg_alloc(&pub, 0);
                    nng_mqtt_msg_set_packet_type(pub, NNG_MQTT_PUBLISH);
                    nng_mqtt_msg_set_publish_qos(pub, qos);
                    nng_mqtt_msg_set_publish_topic(pub, topic);
                    nng_mqtt_msg_set_publish_payload(pub, payload, len);

                    nng_sendmsg(sock, pub, NNG_FLAG_ALLOC);
                    sent++;
                    free(payload);
                }
                next_burst_ns = now +
                    (uint64_t)burst_period * 1000000000ULL;
            }
            struct timespec ts = {0, 5 * 1000000};
            nanosleep(&ts, NULL);
        }
    }

    printf("Sent %" PRIu64 " messages (%.2f msg/s)\n",
           sent, (double)sent / duration);
}

/* =====================
 * main
 * ===================== */

int
main(const int argc, const char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
            "Usage: %s <URL> <QOS> <TOPIC>\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    const char *url   = argv[1];
    uint8_t qos       = (uint8_t)atoi(argv[2]);
    const char *topic = argv[3];

    traffic_pattern_t pattern = PATTERN_PERIODIC;
    const char *p = getenv("PATTERN");
    if (p) {
        if (!strcmp(p, "sporadic")) pattern = PATTERN_SPORADIC;
        else if (!strcmp(p, "burst")) pattern = PATTERN_BURST;
    }

    run_publisher(url, qos, topic, pattern);
    return 0;
}
