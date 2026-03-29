```
Todo:
Investigate SCHED_DEADLINE, requires root and "sched_setattr" in the code
https://docs.emqx.com/en/emqx/latest/mqtt-over-quic/introduction.html
https://www.suse.com/c/cpu-isolation-practical-example-part-5/
https://ieeexplore-ieee-org.ep.bib.mdh.se/stamp/stamp.jsp?tp=&arnumber=10279305
```

# quic_experimental
Source for modified EMQX, NanoSDK IoT MQTT over QUIC project


# Hardware
Sensor: RPi 4 Model B (4GB)

Edge & Cloud: RPI 5 Model B (4GB)

# Operating system & real-time optimization
Operating System (OS): RPi OS Lite (64-bit). Basic setup with user/pass, SSH, automatic Wi-Fi connection (check and reserve IP in router).

After installing OS, followed instructions at to build new kernel (with PREEMPT_RT): [https://www.raspberrypi.com/documentation/computers/linux_kernel.html](https://www.suse.com/c/cpu-isolation-practical-example-part-5/)

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
Installed NanoSDK client github.com/emqx/NanoSDK

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
./quic_client conn 'mqtt-quic://192.168.0.29:14567' #Default QUIC port, simple connection test.
```
Verify connection:
```
sudo docker exec -it CONTAINERNAME sh #Go into the container
emqx ctl clients list #Should show the client IP
```
Copy pub.c, compile it, and test it out!
```
#For example:
#Pin in core 3, FIFO, PRI 80, Publish MQTT topic sensor/1 with QoS 0, 10 B, 10 msgs, 1 s duration, silent-mode.
#And
#Pin in core 2, FIFO, PRI 78, Publish MQTT topic sensor/2 with QoS 0, 10 B, 10 msgs, 1 s duration, silent-mode.
sudo taskset -c 3 chrt -f 80 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/1 10 10 1 1 & sudo taskset -c 2 chrt -f 78 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/2 10 10 1 1
```

# Edge
Install EMQX broker via Docker [https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html](https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html)

And setup MQTT over QUIC

Pin container to core 1 and restart it: ```sudo docker update --cpuset-cpus="1" emqxQUIC && sudo docker restart emqxQUIC #Where emqxQUIC is the container name```

# Cloud
Installed NanoSDK client github.com/emqx/NanoSDK

See steps above in # Sensor

Copy sub.c, compile it, and test it out!
```
#For example:
cd ~/NanoSDK/demo/quic_mqtt
#Subscribe to wildcard "sensor/#" with QoS 0 silent-mode
./sub sub mqtt-quic://192.168.0.29:14567 0 sensor/# 0
CTRL+C #Interrupt and save statistics after sensors finished sending.
cat messages.log #Contains: recv_timestamp,topic,seq,send_timestamp,random_data
```
