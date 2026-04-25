#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <stdbool.h>

#include "nng/mqtt/mqtt_client.h"
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"

typedef enum {
    TRAFFIC_PERIODIC = 0,
    TRAFFIC_SPORADIC = 1,
    TRAFFIC_BURST    = 2
} traffic_pattern_t;

static inline uint64_t
now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

uint8_t *
build_message(uint64_t seq, size_t random_size, size_t *out_len)
{
    if (random_size < 16) return NULL;

    uint8_t *buf = malloc(random_size);
    if (!buf) return NULL;

    uint64_t ts = now_ns();

    memcpy(buf + 0, &seq, sizeof(uint64_t));
    memcpy(buf + 8, &ts,  sizeof(uint64_t));

    for (size_t i = 16; i < random_size; i++) {
        buf[i] = rand() & 0xff;
    }

    *out_len = random_size;
    return buf;
}

static inline void
sleep_periodic(int msg_per_sec)
{
    if (msg_per_sec <= 0) return;
    nng_msleep(1000 / msg_per_sec);
}

static inline void
sleep_sporadic(double lambda)
{
    double u = ((double) rand() + 1.0) / ((double) RAND_MAX + 1.0);
    double delay = -log(u) / lambda;  // seconds
    nng_msleep((int)(delay * 1000.0));
}

void
fatal(const char *msg, int rv)
{
	fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

static void
disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	int reason = 0;
	nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
	printf("%s: disconnected!\n", __FUNCTION__);
	(void) ev;
	(void) arg;
}

static void
connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	int reason;
	nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
	printf("%s: connected!\n", __FUNCTION__);
	(void) ev;
	(void) arg;
}

int
client_connect(
    nng_socket *sock, nng_dialer *dialer, const char *url, bool verbose)
{
	int        rv;

	if ((rv = nng_mqtt_client_open(sock)) != 0) {
		fatal("nng_socket", rv);
	}

	nng_socket_set_bool(*sock, NNG_OPT_TCP_NODELAY, true);

	if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
		fatal("nng_dialer_create", rv);
	}

	nng_msg *connmsg;
	nng_mqtt_msg_alloc(&connmsg, 0);
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
	nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
	nng_mqtt_msg_set_connect_user_name(connmsg, "nng_mqtt_client");
	nng_mqtt_msg_set_connect_password(connmsg, "secrets");
	nng_mqtt_msg_set_connect_will_msg(
	    connmsg, (uint8_t *) "bye-bye", strlen("bye-bye"));
	nng_mqtt_msg_set_connect_will_topic(connmsg, "will_topic");
	nng_mqtt_msg_set_connect_clean_session(connmsg, true);

	nng_mqtt_set_connect_cb(*sock, connect_cb, sock);
	nng_mqtt_set_disconnect_cb(*sock, disconnect_cb, connmsg);

	uint8_t buff[1024] = { 0 };

	printf("Connecting to server ...\n");
	nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
	nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);

	return (0);
}

int
client_publish(nng_socket sock, const char *topic, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	int rv;

	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);

	//printf("Publishing to '%s' ...\n", topic);
	if ((rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK)) != 0) {
		fatal("nng_sendmsg", rv);
	}

	return rv;
}

struct pub_params {
    nng_socket *sock;
    const char *topic;
    uint8_t qos;

    size_t payload_size;
    int msg_per_sec;
    int duration_sec;

    traffic_pattern_t pattern;

    int burst_size;
    int burst_period_sec;

    uint64_t sent;
    uint64_t bytes;
};

void
publish_cb(void *arg)
{
    struct pub_params *p = arg;
    uint64_t seq = 0;
    uint64_t start = now_ns();

    while ((now_ns() - start) < (uint64_t)p->duration_sec * 1e9) {

        if (p->pattern == TRAFFIC_PERIODIC) {
            size_t len;
            uint8_t *msg = build_message(seq++, p->payload_size, &len);
			if (!msg) continue;
            client_publish(*p->sock, p->topic, msg, len, p->qos, false);
            p->sent++;
            p->bytes += len;
            free(msg);
            sleep_periodic(p->msg_per_sec);

        } else if (p->pattern == TRAFFIC_SPORADIC) {
            size_t len;
            uint8_t *msg = build_message(seq++, p->payload_size, &len);
			if (!msg) continue;
            client_publish(*p->sock, p->topic, msg, len, p->qos, false);
            p->sent++;
            p->bytes += len;
            free(msg);
            sleep_sporadic((double)p->msg_per_sec);

        } else if (p->pattern == TRAFFIC_BURST) {
            for (int i = 0; i < p->burst_size; i++) {
                size_t len;
                uint8_t *msg = build_message(seq++, p->payload_size, &len);
				if (!msg) continue;
                client_publish(*p->sock, p->topic, msg, len, p->qos, false);
                p->sent++;
                p->bytes += len;
                free(msg);
            }
            nng_msleep(p->burst_period_sec * 1000);
        }
    }

    printf("Sent %" PRIu64 " msgs, %" PRIu64 " bytes\n",
           p->sent, p->bytes);
}

struct pub_params params;


int
main(const int argc, const char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <URL> <QOS> <TOPIC>\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    /* Traffic pattern */
    const char *pattern_env = getenv("PATTERN");
    if (pattern_env && strcmp(pattern_env, "burst") == 0)
        params.pattern = TRAFFIC_BURST;
    else if (pattern_env && strcmp(pattern_env, "sporadic") == 0)
        params.pattern = TRAFFIC_SPORADIC;
    else
        params.pattern = TRAFFIC_PERIODIC;

    /* Parse CLI */
    const char *url   = argv[1];
    uint8_t qos       = (uint8_t) atoi(argv[2]);
    const char *topic = argv[3];

    /* Experiment parameters (env-only) */
    params.payload_size = getenv("SIZE")     ? atoi(getenv("SIZE"))     : 256;
    params.msg_per_sec  = getenv("MSGPS")    ? atoi(getenv("MSGPS"))    : 10;
    params.duration_sec = getenv("DURATION") ? atoi(getenv("DURATION")) : 30;

    params.burst_size =
        getenv("BURST_SIZE") ? atoi(getenv("BURST_SIZE")) : 100;
    params.burst_period_sec =
        getenv("BURST_PERIOD") ? atoi(getenv("BURST_PERIOD")) : 1;

    if (params.payload_size < 16) {
        fprintf(stderr, "ERROR: SIZE must be >= 16 bytes\n");
        return 1;
    }

    /* Init socket */
    nng_socket sock;
    nng_dialer dialer;
    client_connect(&sock, &dialer, url, false);
	nng_msleep(100);   // allow CONNACK
    params.sock  = &sock;
    params.topic = topic;
    params.qos   = qos;
    params.sent  = 0;
    params.bytes = 0;

    /* Run publisher */
    nng_thread *thr;
    nng_thread_create(&thr, publish_cb, &params);
    nng_thread_destroy(thr);

    return 0;
}