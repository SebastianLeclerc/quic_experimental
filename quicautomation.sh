#!/bin/bash
set -u   # keep safety, remove -e for experiments

CLOUD="cloud@192.168.0.30"
SENSOR="ubuntu@79.76.50.54"
SSHKEY="-i ssh-key-2026-04-05.key"

SENSOR_CMD="sudo taskset -c 1 chrt -f 60 ./pinit pub mqtt-quic://80.216.216.58:14567 0 sensor/1 100 1 1 1 -p"
EDGE_CMD="sudo taskset -c 2 chrt -f 60 ./sinit sub mqtt-quic://192.168.0.34:14567 0 sensor/# 0"
CLOUD_CMD="sudo ./sinit sub mqtt-quic://192.168.0.34:14567 0 sensor/# 0"


PAUSE=1
echo "Start"
for i in {1..50}; do
    # Start edge (local, background)
    $EDGE_CMD 2>&1 &
    EDGE_PID=$!
    sleep $PAUSE

    # Start cloud (remote, background, record remote PID)
    ssh $CLOUD "nohup $CLOUD_CMD > poop.log 2>&1 & echo \$! > cloud.pid"
    sleep $PAUSE

    # Start sensor (remote, foreground — exits on its own)
    ssh $SSHKEY $SENSOR "$SENSOR_CMD"
    sleep $PAUSE

    # Stop cloud
    ssh $CLOUD "kill \$(cat cloud.pid) || true"
    ssh $CLOUD "rm -f cloud.pid"
    ssh $CLOUD "rm -f poop.log"

    # Stop edge
    kill $EDGE_PID || true
    wait $EDGE_PID 2>/dev/null || true

    sleep 2
done
echo "Finished"