/*
* MQTT-over-QUIC Subscriber
* Usage: sub <url> <qos> <topic> <silent>
* Example: ./sub sub mqtt-quic://192.168.0.29:14567 0 sensor/# 0
*
* Uses keep alive thread to maintain connection > 20 s (default disconnect)
* Logs recv_ts,seq,send_ts,rnd_len into messages.log in the same folder
*/

#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>

#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>

#define CONN 1
#define SUB  2
#define PUB  3

#define RING_SIZE 200000

typedef struct {
    uint64_t recv_ts;
    uint64_t seq;
    uint64_t send_ts;
    uint32_t rnd_len;
} measurement_t;

static measurement_t ring[RING_SIZE];
static volatile int ring_write = 0;
static nng_socket *g_sock = NULL;

// --------------------------
// QUIC configuration
// --------------------------
conf_quic config_user = {
    .tls = {
        .enable = false,
        .cafile = "",
        .certfile = "",
        .keyfile  = "",
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

// --------------------------
// Keep-alive thread
// --------------------------
void *ping_thread(void *arg) {
    nng_socket *sock = arg;
    while (1) {
        nng_msg *msg;
        nng_mqtt_msg_alloc(&msg, 0);
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PINGREQ);
        nng_sendmsg(*sock, msg, NNG_FLAG_ALLOC);
        nng_msleep(5000); // every 5 seconds
    }
}

// --------------------------
// Logger thread
// --------------------------
void *logger_thread(void *arg)
{
    FILE *f = fopen("messages.log", "w");
    fprintf(f, "recv_ts,seq,send_ts,rnd_len\n");

    int last = 0;
    while (1) {
        int w = ring_write;
        while (last != w) {
            measurement_t *m = &ring[last];
            fprintf(f, "%llu,%llu,%llu,%u\n",
                (unsigned long long)m->recv_ts,
                (unsigned long long)m->seq,
                (unsigned long long)m->send_ts,
                m->rnd_len);
            last = (last + 1) % RING_SIZE;
        }
        fflush(f);
        nng_msleep(100);
    }
}

// --------------------------
// MQTT message builder
// --------------------------
static nng_msg *
mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);

    if (type == CONN) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);

        nng_mqtt_msg_set_connect_proto_version(msg, 4);
        
        nng_mqtt_msg_set_connect_keep_alive(msg, 10);

        nng_mqtt_msg_set_connect_clean_session(msg, true);
    } 
    else if (type == SUB) {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

        nng_mqtt_topic_qos subscriptions[] = {
            {
                .qos   = qos,
                .topic = {
                    .buf    = (uint8_t *) topic,
                    .length = strlen(topic),
                },
            },
        };
        nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, 1);
    }

    return msg;
}

// --------------------------
// QUIC callbacks
// --------------------------
static int connect_cb(void *rmsg, void *arg)
{
    printf("[Connected][%s]...\n", (char *)arg);
    return 0;
}

static int disconnect_cb(void *rmsg, void *arg)
{
    printf("[Disconnected][%s]...\n", (char *)arg);
    return 0;
}

// --------------------------
// Lightweight receive callback
// --------------------------
static int msg_recv_cb(void *rmsg, void *arg)
{
    int silent = *(int *)arg;

    nng_msg *msg = rmsg;
    uint32_t topicsz, payloadsz;

    const uint8_t *topic   = nng_mqtt_msg_get_publish_topic(msg, &topicsz);
    const uint8_t *payload = nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t recv_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    if (payloadsz < 16)
        return 0;

    uint64_t seq =
        ((uint64_t)payload[0] << 56) |
        ((uint64_t)payload[1] << 48) |
        ((uint64_t)payload[2] << 40) |
        ((uint64_t)payload[3] << 32) |
        ((uint64_t)payload[4] << 24) |
        ((uint64_t)payload[5] << 16) |
        ((uint64_t)payload[6] << 8)  |
         (uint64_t)payload[7];

    uint64_t send_ns =
        ((uint64_t)payload[8] << 56) |
        ((uint64_t)payload[9] << 48) |
        ((uint64_t)payload[10] << 40) |
        ((uint64_t)payload[11] << 32) |
        ((uint64_t)payload[12] << 24) |
        ((uint64_t)payload[13] << 16) |
        ((uint64_t)payload[14] << 8)  |
         (uint64_t)payload[15];

    uint32_t rnd_len = payloadsz - 16;

    int w = ring_write;
    ring[w].recv_ts = recv_ns;
    ring[w].seq     = seq;
    ring[w].send_ts = send_ns;
    ring[w].rnd_len = rnd_len;
    ring_write = (w + 1) % RING_SIZE;

    return 0;
}

// --------------------------
// Client
// --------------------------
int client(int type, const char *url, const char *qos, const char *topic, int silent)
{
    nng_socket sock;
    int rv, q;
    nng_msg *msg;
    const char *arg = "CLIENT FOR QUIC";

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0)
        printf("error in quic client open.\n");

    // Callbacks
    nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg);
    nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg);

    if (silent == 0)
        nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, (void *)arg);
    else {
        int *sp = malloc(sizeof(int));
        *sp = silent;
        nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, sp);
    }

    g_sock = &sock;

    // CONNECT
    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

    pthread_t ping;
    pthread_create(&ping, NULL, ping_thread, &sock);

    // SUBSCRIBE
    if (qos) {
        q = atoi(qos);
        if (q < 0 || q > 2) q = 0;
    } else q = 0;

    msg = mqtt_msg_compose(SUB, q, (char *)topic, NULL);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

    // loop forever
    for (;;)
        nng_msleep(1000);
}

// --------------------------
// Main
// --------------------------
int main(int argc, char **argv)
{
    mlockall(MCL_CURRENT | MCL_FUTURE);

    pthread_t t;
    pthread_create(&t, NULL, logger_thread, NULL);

    if (argc != 6 || strncmp(argv[1], "sub", 3) != 0) {
        printf("Usage: sub <url> <qos> <topic> <silent>\n");
        return 0;
    }

    int silent = atoi(argv[5]);
    client(SUB, argv[2], argv[3], argv[4], silent);
    return 0;
}
