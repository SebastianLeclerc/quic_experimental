#!/usr/bin/env bash

runs=1

containers=("RSA2048serverOnlyAuth" "RSA4096serverOnlyAuth" "ECDSAP256serverOnlyAuth" "ECDSAP384serverOnlyAuth" "Ed25519serverOnlyAuth")

for c in "${containers[@]}"; do

#Start container
sudo docker start "$c"
sleep 5

#Create a results folder
mkdir "$c"

###################################

echo "START QUIC"

for ((i=1; i<=$runs; i++)); do

#Start edge QUIC sub
sudo ./sinit sub mqtt-quic://192.168.0.34:14567 0 sensor/# 0 2>&1 &
EDGE_PID=$!
sleep 1

#Start sensor QUIC pub
ssh -q cloud@192.168.0.30 << 'EOF' >/dev/null 2>&1
echo $(date +%s%N) >> sensor.log
timeout 1s ./quic_client pub mqtt-quic://192.168.0.34:14567 0 sensor/1 "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean commodo ligula eget dolor. Aenean m"
EOF
sleep 1

#Kill edge sub
kill $EDGE_PID || true
wait $EDGE_PID 2>/dev/null || true

sleep 1

done
echo "FIN QUIC"

###################################

echo "START TCP"

#Copy docker certs to edge and sensor for TCP TLS
sudo docker cp "$c":/opt/emqx/etc/certs/cert.pem .
scp -q cert.pem cloud@192.168.0.30:/home/cloud/cert.pem

sleep 1

#Test both TLS 1.2
for ((i=1; i<=$runs; i++)); do

#Start edge TCP sub
(
  mosquitto_sub \
    -h 192.168.0.34 \
    -p 8883 \
    --cafile cert.pem \
    --tls-version tlsv1.2 \
    --insecure \
    -t sensor/1 \
    -C 1 \
  | while read payload; do
      TS_RECV=$(date +%s%N)
      echo "$TS_RECV" >> edge_tls1.2.log
    done
) </dev/null >/dev/null 2>&1 &

sleep 1

#Start sensor TCP pub
ssh cloud@192.168.0.30 'bash -s' <<'EOF'
(
  TS=$(date +%s%N)
  echo "$TS" >> /home/cloud/sensor_tls1.2.log
  mosquitto_pub \
    -h 192.168.0.34 \
    -p 8883 \
    --cafile /home/cloud/cert.pem \
    --tls-version tlsv1.2 \
    --insecure \
    -t sensor/1 \
    -m "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean commodo ligula eget dolor. Aenean m"
)
EOF

sleep 1

done

sleep 1

#Test both TLS 1.3
for ((i=1; i<=$runs; i++)); do

#Start edge TCP sub
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
      echo "$TS_RECV" >> edge_tls1.3.log
    done
) </dev/null >/dev/null 2>&1 &

sleep 1

#Start sensor TCP pub
ssh cloud@192.168.0.30 'bash -s' <<'EOF'
(
  TS=$(date +%s%N)
  echo "$TS" >> /home/cloud/sensor_tls1.3.log
  mosquitto_pub \
    -h 192.168.0.34 \
    -p 8883 \
    --cafile /home/cloud/cert.pem \
    --tls-version tlsv1.3 \
    --insecure \
    -t sensor/1 \
    -m "Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Aenean commodo ligula eget dolor. Aenean m"
)
EOF

sleep 1

done

echo "FIN TCP"

###################################
#Copy all files
scp -q cloud@192.168.0.30:/home/cloud/*.log "$c"
ssh cloud@192.168.0.30 "rm -f *.log"
ssh cloud@192.168.0.30 "rm -f cert.pem"

sleep 1

cp edge*.log "$c"
rm -f edge*.log
rm -f cert.pem

sleep 1

#Stop container
sudo docker stop "$c"

sleep 3

done

echo "All tests done!"
