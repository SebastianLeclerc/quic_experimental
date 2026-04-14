# Info
Setup for modified MQTT over QUIC project using EMQX, NanoSDK, Oracle Cloud, and local RPi's patched with PREEMPT_RT as Sensor and Edge

# Hardware
Sensor: RPi 4 Model B (4GB) ...Wi-Fi 802.11ac... Edge: RPI 5 Model B (4GB) ...Wi-Fi 802.11ac to ISP... Cloud: Oracle Cloud VM (_or_ local RPI 5 Model B (4GB))

Local router is a Sagemcom Broadband SAS (version 3.0_CU)

# Operating system & real-time optimization
Operating System (OS): RPi OS Lite (64-bit). Basic setup with user/pass, SSH, automatic Wi-Fi connection (check and reserve IP in router).

After installing OS, followed instructions at to download, natively build, customize and install a new kernel (with PREEMPT_RT): [https://www.raspberrypi.com/documentation/computers/linux_kernel.html](https://www.suse.com/c/cpu-isolation-practical-example-part-5/)

<!-- https://www.youtube.com/watch?v=nDJETVboL4Y -->

Before "Build" step, changed .config to enable PREEMPT_RT=y via GUI (```sudo apt install libncurses-dev -y && make menuconfig```), then built, configured, installed, rebooted.

After complete and rebooted, verified that PREEMPT_RT is enabled (```uname -a #Shows this```). Note, it can happen that e.g., ```apt upgrade``` overwrites the kernel, in that case just reinstall it via:

```
cd linux
make
sudo make modules_install
KERNEL=kernel_2712 # For RPI 5 Model B, see documentation above otherwise.
sudo cp /boot/firmware/$KERNEL.img /boot/firmware/$KERNEL-backup.img
sudo cp arch/arm64/boot/Image.gz /boot/firmware/$KERNEL.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/
sudo reboot
```

Run ```rtoptimization.sh``` to optimize core 1-3 for running RT tasks.

Run program in core 1-3, using schedule, and priority*: ```sudo taskset -c [1-3] chrt -[e.g, f, r, etc.] [0-99] ./my_program arg```

*Keep priority High, e.g. probably, 30-60, higher than all kernel non-RT tasks, not starving network

Limitation: Some libraries, kernel, OS, network stack, etc. (e.g., NNG, QUIC, kernel socket, NIC) will still cause unexpected delays.

# Sensor
Installed Mosquitto MQTT Client [https://mosquitto.org/download/](https://mosquitto.org/download/) and NanoSDK client [https://github.com/emqx/NanoSDK
](https://github.com/emqx/NanoSDK). Avoid "NanoMQT" because it adds another translation layer (TCP->QUIC).

```
sudo apt install -y mosquitto-clients
git clone https://github.com/emqx/NanoSDK
sudo apt install cmake
sudo apt install ninja-build
cd NanoSDK
git submodule update --init --recursive 
mkdir build && cd build
cmake -G Ninja -DBUILD_SHARED_LIBS=OFF -DNNG_ENABLE_QUIC=ON ..
ninja
sudo ninja install; sudo ldconfig
```
Compile demo script with correct links and test it:
```
cd ~/NanoSDK/demo/quic_mqtt
gcc -O2 quic_client.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -lpthread -ldl -o quic_client
./quic_client conn 'mqtt-quic://192.168.0.34:14567' #Default QUIC port, simple connection test to MQTT broker.
```
Verify connection in Edge, see below.

Copy pub.c, compile it, and test it out!
```
gcc -O2 pub.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -lpthread -ldl -lm -o pub
```

# Edge
Install docker [https://docs.docker.com/engine/install/debian/](https://docs.docker.com/engine/install/debian/)

Install EMQX MQTT broker [https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)

Install Mosquitto MQTT Client ```sudo apt install -y mosquitto-clients``` for TCP-testing.

And setup MQTT over QUIC. Note that container does not autostart on boot, default config accepts 0.0.0.0:1883, 0.0.0.0:8883, etc.

Useful commands, files:
```
sudo rfkill block wifi #Kill wifi

sudo tcpdump -i wlan0 -w quic_capture.pcap udp port 14567 #Verification of QUIC via packet capture for WireShark

sudo docker update --cpuset-cpus="1" CONTAINERNAME; sudo docker restart CONTAINERNAME #Pin to core 1, restart.
sudo docker exec -it CONTAINERNAME sh #Go into the container
#Following are inside the container
emqx ctl clients list #Should show the client IP
emqx ctl listeners #Should show QUIC enable
/opt/emqx/etc/base.hocon #Needs QUIC listener
```

To enable measurement in edge:
EMQX uses different timestamps than C "CLOCK_REALTIME". Therefore easier to use local MQTT subscriber in the edge, rather than, e.g., setting up ingress/egress rules to timestamp data.

# Cloud
Setup a Oracle Cloud VM ("Always Free-eligible"): Canonical Ubuntu 22.04 Minimal, VM.Standard.E2.1.Micro

Setup the VM network (Paravirtualized) with a Ephemeral Public IP. Downloaded the VM's SSH keys.

Then configured the VM, installing neccessary packages to run sub.c:
```
ssh -i SSH-KEY-FILE.key USER@IP
sudo apt update
sudo apt upgrade -y
sudo reboot now
sudo apt install nano
sudo apt install -y mosquitto-clients
#Copy sub.c to ~
sudo apt install gcc
sudo apt install git
git clone https://github.com/emqx/NanoSDK ; cd NanoSDK
git submodule update --init --recursive
mkdir build && cd build
sudo apt install cmake
sudo apt install ninja-build
sudo apt install -y build-essential
sudo apt install -y libssl-dev pkg-config
cmake -G Ninja -DBUILD_SHARED_LIBS=OFF -DNNG_ENABLE_QUIC=ON ..
ninja
sudo ninja install; sudo ldconfig
cd ~
gcc -O2 sub.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -pthread -ldl -o sub
```

Configured network between VM and edge:
```
sudo apt install iputils-ping #For general ping reachability
sudo apt install netcat-openbsd #For nc IP:Port reachability
#Check local Public IP via curl
curl https://api.ipify.org
#Added Port Forwarding Rule in local router for: UDP:14567, NTP:123, MQTT:8883, SSH:22 Internal/External IP:Port
#Connectivity test:
sudo tcpdump -n -i any udp port 14567 #Start a listener on the edge
nc -uvz IP PORT #Send traffic to edge
```

If network is OK can now test demo/sub program
```
cd ~/NanoSDK/demo/quic_mqtt
./quic_client conn mqtt-quic://IP:PORT
#And test sub.c program
#Subscribe to wildcard "sensor/#" with QoS 0 silent-mode
./sub sub mqtt-quic://IP:14567 0 sensor/# 0
CTRL+C #Interrupt and save statistics after sensors finished sending.
cat messages.log #Contains: topic,recv_ts,seq,send_ts,rnd_len
```
# Time Synchronization
Configure the edge to be the NTP time source

On all nodes
```
sudo apt update
sudo apt install chrony -y
```

Configure edge: ```/etc/chrony/chrony.conf```
```
server ntp.se.pool.ntp.org iburst
server 0.europe.pool.ntp.org iburst
server 1.europe.pool.ntp.org iburst
server time.google.com iburst
#allow 0.0.0.0/0
allow 192.168.0.0/24
allow CLOUD_IP/0
local stratum 10
makestep 1 3
rtcsync
driftfile /var/lib/chrony/chrony.drift
log tracking measurements statistics
logdir /var/log/chrony
cmdallow 127.0.0.1
cmdallow 192.168.0.0/24
```

Other nodes: ```/etc/chrony/chrony.conf```
```
server LOCAL_OR_PUBLIC_IP iburst prefer
server ntp.se.pool.ntp.org iburst
server 0.europe.pool.ntp.org iburst
makestep 1 3
rtcsync
driftfile /var/lib/chrony/chrony.drift
```

Set timezone in cloud to  same as RPi ```sudo timedatectl set-timezone Europe/Stockholm```

In router, add port forwarding rule for NTP, port 123, Internal/External IP

Verification
```
chronyc sources -v #Shows time source marked with ^*
chronyc clients #Shows NTP clients
sudo systemctl restart chrony #After changes to the conf
curl https://api.ipify.org #Check public IP
```

# Security
EMQX by default use X.509 certificate, RSA 2048-bit public key, signed with sha256WithRSAEncryption and a corresponding private RSA 2048-bit private key. Generally secure until Quantum Computing.

EMQX by defauly only uses server-only authentication. Not secure.

QUIC requires TLS 1.3, meaning only approved ciphers are allowed: 
- Symmetric: AES‑128‑GCM, AES‑256‑GCM, CHACHA20‑POLY1305
- Asymmetric: ECDHE only (P‑256, P‑384, P‑521)
- Certificate RSA (typically 2048–4096 bit), ECDSA (P‑256, P‑384)

Start another container with other encryption
```
sudo docker run -d --name CONTAINERNAME \
  -p 1883:1883 -p 8083:8083 \
  -p 8084:8084 -p 8883:8883 \
  -p 18083:18083 \
  -p 14567:14567/udp \
  -e EMQX_LISTENERS__QUIC__DEFAULT__keyfile="etc/certs/key.pem" \
  -e EMQX_LISTENERS__QUIC__DEFAULT__certfile="etc/certs/.pem" \
  -e EMQX_LISTENERS__QUIC__DEFAULT__ENABLED=true \
emqx/emqx:5.8.8

#Go into the container and enable QUIC
sudo docker exec -it CONTAINERNAME sh
cat >> /opt/emqx/etc/base.hocon << 'EOF'
listeners.quic.default {
  enabled = true
  bind = "0.0.0.0:14567"
  keyfile = "etc/certs/key.pem"
  certfile = "etc/certs/cert.pem"
}
EOF

#Change the encryption by generating key, and self-signed cert for testing
sudo docker exec -it CONTAINERNAME sh
cd etc/certs/

#RSA-2048
openssl genrsa -out key.pem 2048
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=edge"

#RSA-4096
openssl genrsa -out key.pem 4096
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=edge"

#ECDSA P-256
openssl ecparam -genkey -name prime256v1 -out key.pem #Generate key
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=edge" 

#ECDSA P-384
openssl ecparam -genkey -name secp384r1 -out key.pem
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=edge"

#Ed25519
openssl genpkey -algorithm Ed25519 -out key.pem
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=edge"

#Verification:
openssl x509 -in cert.pem -text -noout

#Pin in core 1 and restart
sudo docker update --cpuset-cpus="1" CONTAINERNAME
sudo docker restart CONTAINERNAME
```

# Measurement
After everything is setup, assuming fresh reboot:

1. sensor@sensor:~ $ ./rtoptimizaion.sh
2. edge@edge:~ $ sudo rfkill block wifi
3. edge@edge:~ $ ./rtoptimization.sh
4. edge@edge:~ $ sudo docker update --cpuset-cpus="1" CONTAINERNAME
5. edge@edge:~ $ sudo docker restart CONTAINERNAME

TCP initial security cost evaluation:
1. edge@edge:~ $ sudo docker cp CONTAINERNAME:/opt/emqx/etc/certs/cert.pem .
2. sensor@sensor:~ $ scp edge@192.168.0.34:/home/edge/cert.pem .
3. cloud@cloud:~ $ scp edge@IP:/home/edge/cert.pem .
4. edge@edge:~ $ ./tcpautomation.sh
5. scp sensor.log, edge.log, cloud.log to home with file name and path, e.g., \results\TCP\rpi5sensor\RSA2048sensor.log, etc.
6. Stop container on edge, start another one, repeat from 1.

QUIC initial security cost evaluation:
1. Setup quicautomation.sh according to who is pub, sub.
2. Modify pub.c to log topic,timestamp
```
...
    //printf("Total messages sent: %lu\n", sent_count);
    //printf("Average send rate: %.2f msgs/s\n", rate);
int main(int argc, char **argv)
{
    mlockall(MCL_CURRENT | MCL_FUTURE);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t start_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

    FILE *f = fopen("sensor.log", "a");
        if (!f) {
            perror("fopen");
            return 1;
    }
    fprintf(f, "%lu\n", start_ns);
    fclose(f);
    //etc.
```
3. Modify sub.c logger_thread() to log topic,timestamp
```
void *logger_thread(void *arg)
{
    FILE *f = fopen("cloud.log", "a");
    int last = 0;
    while (1) {
        int w = ring_write;
        while (last != w) {
            measurement_t *m = &ring[last];
            fprintf(f, "%llu\n", m->recv_ts);
            last = (last + 1) % RING_SIZE;
        }
        fflush(f);
        nng_msleep(100);
    }
}
```
4. edge@edge:~ $ ./quicautomation.sh
5. scp sensor.log, edge.log, cloud.log to home with file name and path, e.g., \results\QUIC\rpi5sensor\RSA2048sensor.log, etc.
6. Stop container on edge, start another one, repeat from 1.

Calculate and visualize results on TCP and QUIC initial security cost:
1. python initresults.py results\ result.csv #Assumes file structure as \results\TRANSPORT\PLATFORM\ALGORITHMnode.log
2. python plotminmax.py result.csv sensor-edge #Reads .csv and plots the cost for a particular path

QUIC power storm evaluation:
1. Modify
2. Modify quicautomation.sh to use multiple publishers, e.g.:
```
# Example: Start 3 sending threads, here using: core 1-n, Broker address, QoS 0, Topic, Size (B), msgs/s, duration, silent-mode, periodic traffic pattern.
SENSOR_CMD="
sudo taskset -c 1 ./pmulti pub mqtt-quic://192.168.0.34:14567 0 sensor/1 100 1 1 1 -p
sudo taskset -c 2 ./pmulti pub mqtt-quic://192.168.0.34:14567 0 sensor/2 100 1 1 1 -p
sudo taskset -c n ./pmulti pub mqtt-quic://192.168.0.34:14567 0 sensor/n 100 1 1 1 -p
...etc.
"
```
Spreading out the publishers on multiple cores per sensor/n topic.
