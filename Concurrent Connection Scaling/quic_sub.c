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
#include <inttypes.h>
#include <stdatomic.h>

#define RING_SIZE 200000

typedef struct {
    char     topic[64];
    uint64_t recv_ts;
    uint64_t seq;
    uint64_t send_ts;
    uint32_t rnd_len;
} measurement_t;

static measurement_t ring[RING_SIZE];
static _Atomic uint32_t ring_write = 0;
static _Atomic uint32_t ring_read  = 0;

/* =====================
 * QUIC configuration
 * ===================== */

static conf_quic quic_conf = {
    .tls = {
        .enable = false,
        .verify_peer = true,
        .set_fail = true,
    },
    .multi_stream = false,
    .qos_first = false,
    .qkeepalive = 10,
    .qconnect_timeout = 60,
    .qdiscon_timeout = 30,
    .qidle_timeout = 30,
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
 * Logger thread
 * ===================== */

void
logger_thread(void *arg)
{
    (void)arg;

    
    char fname[128];
    snprintf(fname, sizeof(fname), "edge_%d.log", getpid());
    FILE *f = fopen(fname, "w");
    if (!f) {
        perror("fopen");
        exit(1);
    }

    fprintf(f, "topic,recv_ts,seq,send_ts,rnd_len\n");

    for (;;) {
        uint32_t r = atomic_load_explicit(&ring_read,  memory_order_relaxed);
        uint32_t w = atomic_load_explicit(&ring_write, memory_order_acquire);

        while (r != w) {
            measurement_t *m = &ring[r % RING_SIZE];

            fprintf(f, "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u\n",
                    m->topic,
                    m->recv_ts,
                    m->seq,
                    m->send_ts,
                    m->rnd_len);

            r++;
        }

        atomic_store_explicit(&ring_read, r, memory_order_release);
        fflush(f);
        nng_msleep(100);
    }
}

/* =====================
 * Receive callback
 * ===================== */

static int
msg_recv_cb(void *rmsg, void *arg)
{
    (void)arg;

    uint64_t recv_ts = now_ns();
    nng_msg *msg = rmsg;

    uint32_t payload_len;
    uint8_t *payload =
        nng_mqtt_msg_get_publish_payload(msg, &payload_len);

    if (payload_len < 16)
        return 0;

    uint32_t w = atomic_load_explicit(&ring_write, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&ring_read,  memory_order_acquire);

    if ((w - r) >= RING_SIZE)
        return 0;  // ring full

    measurement_t *m = &ring[w % RING_SIZE];

    /* fill m completely */
    memcpy(&m->seq, payload, 8);
    memcpy(&m->send_ts, payload + 8, 8);
    m->recv_ts = recv_ts;
    m->rnd_len = payload_len - 16;

    uint32_t topic_len;
    const char *topic =
        nng_mqtt_msg_get_publish_topic(msg, &topic_len);

    size_t copy_len =
        topic_len < sizeof(m->topic)-1
            ? topic_len
            : sizeof(m->topic)-1;
    memcpy(m->topic, topic, copy_len);
    m->topic[copy_len] = '\0';

    /* PUBLISH the slot */
    atomic_store_explicit(&ring_write, w + 1, memory_order_release);

    return 0;
}

/* =====================
 * main
 * ===================== */

int
main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
            "Usage: %s <URL> <QOS> <TOPIC>\n", argv[0]);
        return 1;
    }

    const char *url   = argv[1];
    uint8_t qos       = (uint8_t)atoi(argv[2]);
    const char *topic = argv[3];

    nng_socket sock;
    int rv;

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &quic_conf)) != 0)
        fprintf(stderr, "QUIC open failed\n");

    nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, NULL);

    /* CONNECT */
    nng_msg *conn;
    nng_mqtt_msg_alloc(&conn, 0);
    nng_mqtt_msg_set_packet_type(conn, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(conn, 4);
    nng_mqtt_msg_set_connect_clean_session(conn, true);
    nng_sendmsg(sock, conn, NNG_FLAG_ALLOC);

    nng_msleep(100);

    /* Logger */
    nng_thread *logger;
    nng_thread_create(&logger, logger_thread, NULL);

    /* SUBSCRIBE */
    nng_mqtt_topic_qos subs[] = {{
        .qos = qos,
        .topic = {
            .buf = (uint8_t *)topic,
            .length = strlen(topic),
        },
    }};

    nng_msg *sub;
    nng_mqtt_msg_alloc(&sub, 0);
    nng_mqtt_msg_set_packet_type(sub, NNG_MQTT_SUBSCRIBE);
    nng_mqtt_msg_set_subscribe_topics(sub, subs, 1);
    nng_sendmsg(sock, sub, NNG_FLAG_ALLOC);

    for (;;)
        nng_msleep(1000);
}
