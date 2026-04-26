#!/usr/bin/env bash

BROKER_EDGE="mqtt-tcp://192.168.0.34:1883"
CLOUD_HOST="cloud@192.168.0.30"

CONCURRENCIES=(1 5 10 15 20)
CORES=(1 2 3)

for N in "${CONCURRENCIES[@]}"; do
for RUN in $(seq 1 20); do
echo "=== Running QUIC test with $N concurrent sensors ==="

#### Start subscriber locally
./mqtt_sub "$BROKER_EDGE" 0 "test/#" &
SUB_PID=$!

sleep 1

#### Start publishers on cloud
ssh "$CLOUD_HOST" bash -s <<EOF
export PATTERN=periodic
export MSGPS=1
export SIZE=100
export DURATION=1

for i in \$(seq 1 $N); do
core=\$(( (i - 1) % 3 + 1 ))
sudo -E taskset -c \$core /home/cloud/concurrent/mqtt_pub $BROKER_EDGE 0 test/\$i &
done

wait
EOF

sleep 2

#### Stop subscriber
kill "$SUB_PID"
wait "$SUB_PID" 2>/dev/null

#### Collect logs
mv edge_*.log ${N}

echo "=== Done $N concurrent ==="
done
done
