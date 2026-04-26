# Info
Setup for MQTT over QUIC/TCP project using EMQX, NanoSDK, mosquitto, Oracle Cloud, and local RPi's patched with PREEMPT_RT as Sensor and Edge

# Hardware and connection scheme
Sensor: RPi 4 Model B (4GB) ←→ Wi-Fi 802.11ac ←→ Local Sagemcom Broadband SAS Router (version 3.0_CU) ←→ Edge: RPI 5 Model B (4GB)

↑

↓

Ethernet to ISP

↑

↓

Cloud: Oracle Cloud VM (_or_ local RPI 5 Model B (4GB))

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
Set environment variables, compile demo script with correct links and test it, e.g.:
```
#Controls minimum publisher behaviour, e.g.:
export PATTERN=periodic
export MSGPS=1
export SIZE=100
export DURATION=1

gcc -O2 mqtt_pub.c -I/usr/local/include -L/usr/local/lib -lnng -lm -o mqtt_pub
gcc -O2 mqtt_sub.c -I/usr/local/include -L/usr/local/lib -lnng -lpthread -lm -o mqtt_sub
./mqtt_pub mqtt-tcp://192.168.0.34:1883 0 test
./mqtt_sub mqtt-tcp://192.168.0.34:1883 0 test

gcc -O2 quic_pub.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -lpthread -ldl -lm -o quic_pub
gcc -O2 quic_sub.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -lpthread -ldl -lm -o quic_sub
./quic_pub mqtt-quic://192.168.0.34:14567 0 test
./quic_sub mqtt-quic://192.168.0.34:14567 0 test
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
#Following commands are inside the container
emqx ctl clients list #Should show the client IP
emqx ctl listeners #Should show QUIC enable
/opt/emqx/etc/base.hocon #Conf that needs QUIC listener
etc/certs/ #Where certs live 
```

Note: EMQX uses different timestamps than C "CLOCK_REALTIME" used in the code. Therefore better to use MQTT subscriber timestamping, rather than setting up ingress/egress rules with timestamping.

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

If network is OK can now test demo program for connection
```
cd ~/NanoSDK/demo/quic_mqtt
./quic_client conn mqtt-quic://IP:PORT
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
curl https://api.ipify.org #Check public IP of device

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
chronyc sources -v #Shows chosen time source marked with ^*
sudo chronyc clients -v #Shows connected NTP clients on edge
sudo systemctl restart chrony #After changes to the conf
```

# Security
EMQX docker container setup by default uses X.509 certificate with RSA-2048, server-only authentication. QUIC requires TLS 1.3.

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
openssl s_client -connect IP:Port -tls1_3 # To see what the server accepts

#Restart
sudo docker restart CONTAINERNAME
```

# Measurement
After everything is setup, assuming fresh reboot:

Kill unused services:
```
sudo rfkill block wifi #E.g, on the edge
sudo systemctl disable --now avahi-daemon.socket
sudo systemctl disable --now avahi-daemon.service
sudo systemctl disable --now bluetooth.service
sudo systemctl disable --now ModemManager.service
```

Check that NTP is syncronized.
Start docker container ```sudo docker restart CONTAINERNAME```.
Automation scritps are designed to execute from the edge. 
Copy SSH keys from edge to sensor and cloud ```ssh-copy-id user@ip```
Note that, for using mosquitto, certificates are copied from inside the container and distributed via the automation scripts.

**Connection Establishment Latency**

Run ```init.sensor.sh``` and/or ```init.cloud.sh``` depending on direction, adjust IP, SSH keys, and directories as needed.
This creates 5 new directories with .log files.
Copy .log files to a suitable machine if needed.
Run ```create_csv.py``` to analyze data.
Run ```plot_from_csv.py``` to plot.

**Concurrent Connection Scaling**

A

**Traffic Pattern and Streaming Behaviour**

Run ```autocomparison.sh``` and adjust IP, SSH keys, and directories depending on test direction and location of the pub/sub programs.
This creates a new directories with several .log files.
Run ```analysis.py``` to print analysis.

<!--

**QUIC power storm evaluation**:
1. Modify pub.c to log topic,timestamp
```
int main(int argc, char **argv)
{
    ...
    fprintf(f, "%s,%lu\n", argv[4], start_ns);
    ...
```
2. Modify sub.c to log topic,timestamp
```
void *logger_thread(void *arg)
{
    ...
    fprintf(f, "%s,%llu\n", m->topic, (unsigned long long)m->recv_ts);
    ...
```
3. Modify quicautomation.sh to use multiple publishers, e.g.:
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

4. scp sensor*.log, edge*.log, cloud*.log to appropriate folder, e.g., \results\powerstorm\20sensors\
5. python powerstorm.py #On the .log files and save data in .csv in the format: N,min,p50,p90,max
6. python powerplot.py #And modify the input .csv file accordingly 
-->
