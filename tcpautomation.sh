#!/usr/bin/env bash
echo "START"

for ((i=1; i<=50; i++)); do

### CLOUD

ssh -i ../ssh-key-2026-04-05.key ubuntu@79.76.50.54 'bash -s' <<'EOF'
(
  mosquitto_sub \
    -h 80.216.216.58 \
    -p 8883 \
    --cafile /home/ubuntu/dockercerts/cert.pem \
    --tls-version tlsv1.3 \
    --insecure \
    -t sensor/1 \
    -C 1 \
  | while read payload; do
      TS_RECV=$(date +%s%N)
      echo "$TS_RECV" >> /home/ubuntu/dockercerts/cloud.log
    done
) </dev/null >/dev/null 2>&1 &
EOF

sleep 1

### EDGE

(
  mosquitto_sub \
    -h 192.168.0.34 \
    -p 8883 \
    --cafile cert.pem \
    --tls-version tlsv1.3 \
    --insecure \
    -t sensor/1 \
    -C 1 \
  | while read payload; do
      TS_RECV=$(date +%s%N)
      echo "$TS_RECV" >> edge.log
    done
) </dev/null >/dev/null 2>&1 &

sleep 1

### SENSOR

ssh cloud@192.168.0.30 'bash -s' <<'EOF'
TS=$(date +%s%N)
echo "$TS" >> /home/cloud/dockercerts/sensor.log

mosquitto_pub \
  -h 192.168.0.34 \
  -p 8883 \
  --cafile /home/cloud/dockercerts/cert.pem \
  --tls-version tlsv1.3 \
  --insecure \
  -t sensor/1 \
  -m "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean commodo ligula eget dolor. Aenean m"
EOF

sleep 1

done

echo "FIN"