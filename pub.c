#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>

#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CONN 1
#define SUB 2
#define PUB 3

static nng_socket *g_sock;

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
    .qos_first = false,
    .qkeepalive = 10,
    .qconnect_timeout = 60,
    .qdiscon_timeout = 30,
    .qidle_timeout = 30,
};

static void fatal(const char *msg, int rv)
{
    fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

static nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
    // Mqtt connect message
    nng_msg *msg;
    nng_mqtt_msg_alloc(&msg, 0);

    if (type == CONN)
    {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);
        nng_mqtt_msg_set_connect_proto_version(msg, 4);
        nng_mqtt_msg_set_connect_keep_alive(msg, 10);
        nng_mqtt_msg_set_connect_clean_session(msg, true);
    }
    else if (type == PUB)
    {
        nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_PUBLISH);
        nng_mqtt_msg_set_publish_dup(msg, 0);
        nng_mqtt_msg_set_publish_qos(msg, qos);
        nng_mqtt_msg_set_publish_retain(msg, 0);
        nng_mqtt_msg_set_publish_topic(msg, topic);
	nng_mqtt_msg_set_publish_payload(
            msg, (uint8_t *)payload, strlen(payload)); //closer to publish
    }
    return msg;
}

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

static int msg_send_cb(void *rmsg, void *arg)
{
    printf("[Msg Sent][%s]...\n", (char *)arg);
    return 0;
}


char *build_message(uint64_t seq, size_t random_size)
{
    // Allocate random data buffer
    char *random = malloc(random_size + 1);
    if (!random)
        return NULL;

    for (size_t i = 0; i < random_size; i++)
        random[i] = 'A' + (rand() % 26); // random letters

    random[random_size] = '\0';

    // Allocate final message string
    // Worst case: seq(20 chars) + time(20 chars) + random + commas + null
    size_t msg_len = 50 + random_size;
    char *msg = malloc(msg_len);
    if (!msg)
    {
        free(random);
        return NULL;
    }

    // Generate timestamp
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t send_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    snprintf(msg, msg_len, "%llu,%llu,%s",
             (unsigned long long)seq,
             (unsigned long long)send_ns,
             random);

    free(random);
    return msg; // caller must free()
}

int client(const char *url, const char *qos, const char *topic, size_t random_bytes, int msg_per_sec, int duration_sec, int silent)
{
    nng_socket sock;
    int rv, sz, q;
    nng_msg *msg;
    const char *arg = "CLIENT FOR QUIC";

    if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0)
    {
        printf("error in quic client open.\n");
    }

    if (silent == 0) 
    {
        if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg) ||
            0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg) ||
            0 != nng_mqtt_quic_set_msg_send_cb(&sock, msg_send_cb, (void *)arg))
        {
            printf("error in quic client cb set.\n");
        }
    }
    g_sock = &sock;

    // MQTT Connect...
    msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
    nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

    if (qos)
    {
        q = atoi(qos);
        if (q < 0 || q > 2)
        {
            printf("Qos should be in range(0~2).\n");
            q = 0;
        }
    }

    // The message being sent
    uint64_t seq = 0;

    // interval between messages
    uint64_t interval_ns = 1000000000ULL / msg_per_sec;

    // get current time (for absolute-timer sleep)
    struct timespec next;
    clock_gettime(CLOCK_REALTIME, &next);

    // stop time
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += duration_sec;

    while (1)
    {
        // check if we are past end time
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if ((now.tv_sec > end.tv_sec) ||
            (now.tv_sec == end.tv_sec && now.tv_nsec >= end.tv_nsec))
        {
            break;
        }

        // Build payload
        char *payload = build_message(seq++, random_bytes);

        nng_msg *msg = mqtt_msg_compose(PUB, q, (char *)topic, payload);
        nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);
        free(payload);

        // schedule next send time
        next.tv_nsec += interval_ns;
        while (next.tv_nsec >= 1000000000ULL)
        {
            next.tv_nsec -= 1000000000ULL;
            next.tv_sec++;
        }

        // sleep until next send timestamp
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);
    }

    return (0);
}

int main(int argc, char **argv)
{
    srand(time(NULL));
    int rc;

    if (argc < 3)
    {
        goto error;
    }

    else if (0 == strncmp(argv[1], "pub", 3) && argc == 9)
    {
        size_t random_size = strtoull(argv[5], NULL, 10);
        int msg_per_sec = atoi(argv[6]);
        int duration_sec = atoi(argv[7]);

        if (msg_per_sec < 1)
            msg_per_sec = 1;
        if (duration_sec < 1)
            duration_sec = 1;

        int silent = atoi(argv[8]);
        srand(time(NULL));

        client(argv[2], argv[3], argv[4], random_size, msg_per_sec, duration_sec, silent);
    }

    else
    {
        goto error;
    }

    return 0;

error:
    fprintf(stderr, "Usage: pub  <url> <qos> <topic> <bytes> <rate> <duration> <silent>\n");
    return 0;
}
