#!/usr/bin/env bash
set -e

# Kill background jobs (subscribers) on exit or Ctrl-C
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT

EDGE_IP=192.168.0.34
CLOUD_IP=192.168.0.30
TOPIC=test

MQTT_SUB=./mqtt_sub
QUIC_SUB=./quic_sub

MQTT_PUB=/home/cloud/comparison/mqtt_pub
QUIC_PUB=/home/cloud/comparison/quic_pub

RESULTS_DIR=results
mkdir -p "$RESULTS_DIR"

run_test() {
    local transport=$1
    local url_sub=$2
    local url_pub=$3
    local sub_bin=$4
    local pub_bin=$5

    echo "=== ${transport^^} | PATTERN=$PATTERN MSGPS=$MSGPS SIZE=$SIZE DURATION=$DURATION ==="

    rm -f edge.log

    $sub_bin "$url_sub" 0 "$TOPIC" > "${transport}_sub.log" 2>&1 &
    EDGE_PID=$!

    sleep 2

    ssh -q cloud@"$CLOUD_IP" \
        "export PATTERN=$PATTERN MSGPS=$MSGPS SIZE=$SIZE DURATION=$DURATION \
                BURST_SIZE=$BURST_SIZE BURST_PERIOD=$BURST_PERIOD && \
         $pub_bin \"$url_pub\" 0 \"$TOPIC\""

    sleep 1

    kill $EDGE_PID 2>/dev/null || true
    wait $EDGE_PID 2>/dev/null || true

    mv edge.log "$RESULTS_DIR/${transport}_${PATTERN}_p${MSGPS}_s${SIZE}.log"
    rm -f "${transport}_sub.log"

    sleep 1
}

########################################################################
# BASELINE
########################################################################

export PATTERN=periodic
export MSGPS=1
export SIZE=128
export DURATION=30
export BURST_SIZE=0
export BURST_PERIOD=0

run_test tcp \
    "mqtt-tcp://$EDGE_IP:1883" \
    "mqtt-tcp://$EDGE_IP:1883" \
    "$MQTT_SUB" \
    "$MQTT_PUB"

run_test quic \
    "mqtt-quic://$EDGE_IP:14567" \
    "mqtt-quic://$EDGE_IP:14567" \
    "$QUIC_SUB" \
    "$QUIC_PUB"

########################################################################
# RATE SWEEP (security‑cost primary experiment)
########################################################################

export PATTERN=periodic
export SIZE=64
export DURATION=30

for MSGPS in 10 100 1000 5000 10000; do
    run_test tcp \
        "mqtt-tcp://$EDGE_IP:1883" \
        "mqtt-tcp://$EDGE_IP:1883" \
        "$MQTT_SUB" \
        "$MQTT_PUB"

    run_test quic \
        "mqtt-quic://$EDGE_IP:14567" \
        "mqtt-quic://$EDGE_IP:14567" \
        "$QUIC_SUB" \
        "$QUIC_PUB"
done

########################################################################
# SIZE SWEEP (crypto amortization)
########################################################################

export PATTERN=periodic
export MSGPS=1000
export DURATION=30

for SIZE in 32 64 128 512 1024; do
    run_test tcp \
        "mqtt-tcp://$EDGE_IP:1883" \
        "mqtt-tcp://$EDGE_IP:1883" \
        "$MQTT_SUB" \
        "$MQTT_PUB"

    run_test quic \
        "mqtt-quic://$EDGE_IP:14567" \
        "mqtt-quic://$EDGE_IP:14567" \
        "$QUIC_SUB" \
        "$QUIC_PUB"
done

########################################################################
# BURSTY TRAFFIC
########################################################################

export PATTERN=burst
export SIZE=64
export DURATION=30
export BURST_SIZE=50
export BURST_PERIOD=1

for MSGPS in 50 100 200 400; do
    run_test tcp \
        "mqtt-tcp://$EDGE_IP:1883" \
        "mqtt-tcp://$EDGE_IP:1883" \
        "$MQTT_SUB" \
        "$MQTT_PUB"

    run_test quic \
        "mqtt-quic://$EDGE_IP:14567" \
        "mqtt-quic://$EDGE_IP:14567" \
        "$QUIC_SUB" \
        "$QUIC_PUB"
done

########################################################################
# SPORADIC TRAFFIC
########################################################################

export PATTERN=sporadic
export MSGPS=10
export SIZE=64
export DURATION=60
export BURST_SIZE=0
export BURST_PERIOD=0

run_test tcp \
    "mqtt-tcp://$EDGE_IP:1883" \
    "mqtt-tcp://$EDGE_IP:1883" \
    "$MQTT_SUB" \
    "$MQTT_PUB"

run_test quic \
    "mqtt-quic://$EDGE_IP:14567" \
    "mqtt-quic://$EDGE_IP:14567" \
    "$QUIC_SUB" \
    "$QUIC_PUB"

echo "=== ALL EXPERIMENTS COMPLETED ==="