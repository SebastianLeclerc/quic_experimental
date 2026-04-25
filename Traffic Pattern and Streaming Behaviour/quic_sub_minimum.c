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

#define RING_SIZE 200000

typedef struct {
    char     topic[64];
    uint64_t recv_ts;
    uint64_t seq;
    uint64_t send_ts;
    uint32_t rnd_len;
} measurement_t;

static measurement_t ring[RING_SIZE];
static volatile uint32_t ring_write = 0;
static volatile uint32_t ring_read  = 0;

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

    FILE *f = fopen("edge.log", "w");
    if (!f) {
        perror("fopen");
        exit(1);
    }

    fprintf(f, "topic,recv_ts,seq,send_ts,rnd_len\n");

    while (1) {
        while (ring_read != ring_write) {
            measurement_t *m = &ring[ring_read % RING_SIZE];
            fprintf(f, "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%u\n",
                    m->topic,
                    m->recv_ts,
                    m->seq,
                    m->send_ts,
                    m->rnd_len);
            ring_read++;
        }
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

    if ((ring_write - ring_read) >= RING_SIZE)
        return 0;

    uint64_t seq, send_ts;
    memcpy(&seq, payload, 8);
    memcpy(&send_ts, payload + 8, 8);

    uint32_t rnd_len = payload_len - 16;

    uint32_t topic_len;
    const char *topic =
        nng_mqtt_msg_get_publish_topic(msg, &topic_len);

    measurement_t *m = &ring[ring_write % RING_SIZE];
    size_t copy_len =
        topic_len < sizeof(m->topic)-1
            ? topic_len
            : sizeof(m->topic)-1;

    memcpy(m->topic, topic, copy_len);
    m->topic[copy_len] = '\0';

    m->recv_ts = recv_ts;
    m->seq     = seq;
    m->send_ts = send_ts;
    m->rnd_len = rnd_len;

    ring_write++;
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