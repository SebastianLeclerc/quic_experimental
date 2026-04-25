#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdbool.h>

#include "nng/nng.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/supplemental/util/platform.h"

/* =====================
 * Logging ring buffer
 * ===================== */

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
    (void) arg;

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
 * Error helper
 * ===================== */

static void
fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
    exit(1);
}

/* =====================
 * MQTT connect
 * ===================== */

static void
connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
    (void) p; (void) ev; (void) arg;
    printf("connected\n");
}

static int
client_connect(nng_socket *sock, nng_dialer *dialer, const char *url)
{
    int rv;

    if ((rv = nng_mqtt_client_open(sock)) != 0)
        fatal("nng_mqtt_client_open", rv);

    /* Disable Nagle to reduce latency */
    nng_socket_set_bool(*sock, NNG_OPT_TCP_NODELAY, true);

    if ((rv = nng_dialer_create(dialer, *sock, url)) != 0)
        fatal("nng_dialer_create", rv);

    nng_msg *connmsg;
    nng_mqtt_msg_alloc(&connmsg, 0);
    nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
    nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
    nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
    nng_mqtt_msg_set_connect_clean_session(connmsg, true);

    nng_mqtt_set_connect_cb(*sock, connect_cb, sock);

    nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
    nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);

    return 0;
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

    const char *url   = argv[1];
    uint8_t qos       = (uint8_t)atoi(argv[2]);
    const char *topic = argv[3];

    nng_socket sock;
    nng_dialer dialer;

    client_connect(&sock, &dialer, url);

    /* Give broker time for CONNACK */
    nng_msleep(100);

    /* Start logger AFTER connect */
    nng_thread *logger;
    nng_thread_create(&logger, logger_thread, NULL);

    /* Subscribe synchronously */
    nng_mqtt_topic_qos subs[] = {
        {
            .qos = qos,
            .topic = {
                .buf = (uint8_t *)topic,
                .length = strlen(topic),
            },
        },
    };

    int rv = nng_mqtt_subscribe(sock, subs, 1, NULL);
    if (rv != 0)
        fatal("nng_mqtt_subscribe", rv);

    /* Allow SUBACK path */
    nng_msleep(100);

    /* =====================
     * Receive loop
     * ===================== */

    while (1) {
        nng_msg *msg;
        if ((rv = nng_recvmsg(sock, &msg, 0)) != 0)
            fatal("nng_recvmsg", rv);

        if (nng_mqtt_msg_get_packet_type(msg) != NNG_MQTT_PUBLISH) {
            nng_msg_free(msg);
            continue;
        }

        uint64_t recv_ts = now_ns();

        uint32_t payload_len;
        uint8_t *payload =
            nng_mqtt_msg_get_publish_payload(msg, &payload_len);

        uint64_t seq = 0, send_ts = 0;
        if (payload_len >= 16) {
            memcpy(&seq, payload, 8);
            memcpy(&send_ts, payload + 8, 8);
        }

        uint32_t rnd_len =
            (payload_len >= 16) ? payload_len - 16 : 0;

        /* Ring buffer overflow protection */
        if ((ring_write - ring_read) < RING_SIZE) {

            measurement_t *m =
                &ring[ring_write % RING_SIZE];

            uint32_t topic_len = 0;
            const char *topic_str =
                nng_mqtt_msg_get_publish_topic(msg, &topic_len);

            size_t copy_len =
                topic_len < sizeof(m->topic)-1
                    ? topic_len
                    : sizeof(m->topic)-1;

            memcpy(m->topic, topic_str, copy_len);
            m->topic[copy_len] = '\0';

            m->recv_ts = recv_ts;
            m->seq     = seq;
            m->send_ts = send_ts;
            m->rnd_len = rnd_len;

            ring_write++;
        }

        nng_msg_free(msg);
    }

    return 0;
}