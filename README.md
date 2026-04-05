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
Source for modified EMQX, NanoSDK IoT MQTT over QUIC project


# Hardware
Sensor: RPi 4 Model B (4GB) ... Edge: RPI 5 Model B (4GB) ... Cloud: RPI 5 Model B (4GB)

All three nodes are currently connected to one one router over Wi-Fi.

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
./quic_client conn 'mqtt-quic://192.168.0.29:14567' #Default QUIC port, simple connection test to MQTT broker.
```
Verify connection in Edge, see below.

Copy pub.c, compile it, and test it out!
```
gcc -O2 pub.c -I/usr/local/include -L/usr/local/lib -lnng -lmsquic -lssl -lcrypto -lpthread -ldl -lm -o pub
#For example:
#Pin in core 3, FIFO, PRI 80, Publish MQTT topic sensor/1 with QoS 0, 10 B, 10 msgs, 1 s duration, silent-mode.
#And
#Pin in core 2, FIFO, PRI 78, Publish MQTT topic sensor/2 with QoS 0, 10 B, 10 msgs, 1 s duration, silent-mode.
sudo taskset -c 3 chrt -f 80 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/1 10 10 1 1 & sudo taskset -c 2 chrt -f 78 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/2 10 10 1 1
```

# Edge
Install docker [https://docs.docker.com/engine/install/debian/](https://docs.docker.com/engine/install/debian/)

Install EMQX MQTT broker [https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)

And setup MQTT over QUIC. Note that container does not autostart on boot, default config accepts 0.0.0.0:1883, 0.0.0.0:8883, etc.

Useful commands, files:
```
sudo tcpdump -i wlan0 -w quic_capture.pcap udp port 14567 #Verification of QUIC via packet capture for WireShark

sudo docker update --cpuset-cpus="1" emqxQUIC; sudo docker restart emqxQUIC #Pin to core 1, restart. emqxQUIC is the container name
sudo docker exec -it emqxQUIC sh #Go into the container
#Following are inside the container
emqx ctl clients list #Should show the client IP
emqx ctl listeners #Should show QUIC enable
/opt/emqx/etc/base.hocon #Needs QUIC listener
```

# Cloud
Setup a Oracle Cloud VM ("Always Free-eligible"): Canonical Ubuntu 22.04 Minimal, VM.Standard.E2.1.Micro

Setup the VM network (Paravirtualized) with a Ephemeral Public IP. Downloaded the VM's SSH keys.

Then configured the VM, installing neccessary packages to run sub.c:
```
ssh -i ssh-key-2026-04-05.key ubuntu@79.76.50.54
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
#If OK can now test demo program
cd ~/NanoSDK/demo/quic_mqtt
./quic_client conn mqtt-quic://IP:PORT
#And sub.c
./sub sub mqtt-quic://IP:14567 0 sensor/# 0 #Subscribe to wildcard "sensor/#" with QoS 0 silent-mode
CTRL+C #Interrupt and save statistics after sensors finished sending.
cat messages.log #Contains: recv_ts,seq,send_ts,rnd_len
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
