```
Todo:

fk me: https://github.com/emqx/emqtt-bench

Investigate SCHED_DEADLINE, requires root and "sched_setattr" in the code
https://docs.emqx.com/en/emqx/latest/mqtt-over-quic/introduction.html
https://www.suse.com/c/cpu-isolation-practical-example-part-5/
https://ieeexplore-ieee-org.ep.bib.mdh.se/stamp/stamp.jsp?tp=&arnumber=10279305
Netflix recommends bandwidth req. of 3 mbps, or 5 mbps for 720p HD or 1080p FHD
Max send probably: 1 048 563 B ~ 1.04 MB
timestamp is "relatively" close to network stack
Change from CLOCK_REALTIME to CLOCK_TAI?
MQTT5? Baseline Mosquitto MQTT (+/-sec)?
Mobile nodes?
Head-of-line blocking simulation?
Diff QoS? (MQTT QoS 0 = fastest (no ack), 1 = ACK, might duplicate, 2 = most reliable, only once)
```

# quic_experimental
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
Installed NanoSDK client [https://github.com/emqx/NanoSDK
](https://github.com/emqx/NanoSDK). Avoid "NanoMQT" because it adds another translation layer (TCP->QUIC).

```
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

Start 3 threads sending threads, here using: core 1-3, FIFO, Pri 60, Broker address, QoS 0, Topic, Size (B), msgs/s, duration, silent-mode, periodic traffic pattern
```
#PERIODIC 100 B
sudo taskset -c 1 chrt -f 60 ./pub pub mqtt-quic://192.168.0.34:14567 0 sensor/1 100 10 60 1 -p & 
sudo taskset -c 2 chrt -f 60 ./pub pub mqtt-quic://192.168.0.34:14567 0 sensor/2 100 10 60 1 -p &
sudo taskset -c 3 chrt -f 60 ./pub pub mqtt-quic://192.168.0.34:14567 0 sensor/3 100 10 60 1 -p & 

wait
echo "PERIODIC RUN FINISHED"
```

# Edge
Install docker [https://docs.docker.com/engine/install/debian/](https://docs.docker.com/engine/install/debian/)

Install EMQX MQTT broker [https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)

And setup MQTT over QUIC. Note that container does not autostart on boot, default config accepts 0.0.0.0:1883, 0.0.0.0:8883, etc.

Useful commands, files:
```
sudo rfkill block wifi #Kill wifi

sudo tcpdump -i wlan0 -w quic_capture.pcap udp port 14567 #Verification of QUIC via packet capture for WireShark

sudo docker update --cpuset-cpus="1" emqxQUIC; sudo docker restart emqxQUIC #Pin to core 1, restart. emqxQUIC is the container name
sudo docker exec -it emqxQUIC sh #Go into the container
#Following are inside the container
emqx ctl clients list #Should show the client IP
emqx ctl listeners #Should show QUIC enable
/opt/emqx/etc/base.hocon #Needs QUIC listener
```

To enable measurement in edge:

Go to Web GUI and login, e.g.: ```http://192.168.0.34:18083/``` with admin + public (default, has to change). Go to Integration> Rules> Create> Either an ingress or an egress rule with “Action” to republish matches to, e.g., ```__edge/egress```. Note that message time inside the broker is typically negligable, hence both rules probably not needed.
```
//Egress rule
SELECT
  timestamp AS t_edge_egress,
  clientid,
  topic,
  payload
FROM
  "$events/message_delivered"
WHERE
   peerhost = '192.168.0.30'
```
Can then run local logger on edge, e.g. ```mosquitto_sub -h 127.0.0.1 -t "__edge/#" -v >> edge_timestamps.log``` to capture the edge traffic in a log.
Then run ```elog.py``` to parse the bin data into egress.log human readable format (topic,egresstime,seq).


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
#Added Port Forwarding Rule in local router for: UDP, Internal/External IP:Port
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
EMQX by default use X.509 certificate, RSA 2048-bit public key, signed with sha256WithRSAEncryption and a corresponding private RSA 2048-bit private key. Generally secure until QC.

EMQX by defauly only uses server-only authentication. Not secure.

QUIC requires TLS 1.3, meaning only approved ciphers are allowed: 
- Symmetric: AES‑128‑GCM, AES‑256‑GCM, CHACHA20‑POLY1305
- Asymmetric: ECDHE only (P‑256, P‑384, P‑521)
- Certificate RSA (typically 2048–4096 bit), ECDSA (P‑256, P‑384)

Check certificate/key details in the Edge
```
sudo docker exec -it emqxQUIC sh
cd etc/certs/
openssl x509 -in cert.pem -text -noout
openssl pkey -in key.pem -text -noout
```

Change to ECDSA (faster than RSA) encryption
```
openssl ecparam -genkey -name prime256v1 -out ECDSAkey.pem
openssl req -new -x509 -key ECDSAkey.pem -out ECDSAcert.pem -days 365 -subj "/CN=edge"
#Then backup/copy new *key.pem and *cert.pem files into etc/certs/. Default key/cert is key.pem, cert.pem.
#And restart container
sudo docker restart emqxQUIC
```

```
Maybe useful? Not sure if cached atm
authz cache-clean all         # Clears authorization cache on all nodes
authz cache-clean node <Node> # Clears authorization cache on given node
authz cache-clean <ClientId>  # Clears authorization cache for given client
pem_cache clean all         # Clears x509 certificate cache on all nodes
pem_cache clean node <Node> # Clears x509 certificate cache on given node
```
