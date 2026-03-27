# quic_experimental
Source for modified EMQX, NanoSDK IoT MQTT over QUIC project


cloud@cloud:~/NanoSDK/demo/quic_mqtt $ ./sub sub mqtt-quic://192.168.0.29:14567 0 sensor/# 0

edge@edge:~/NanoSDK/demo/quic_mqtt $ sudo taskset -c 3 chrt -f 80 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/1 10 10 1 1 & sudo taskset -c 2 chrt -f 78 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/2 10 10 1 1

# Hardware
Sensor: RPi 4 Model B (4GB) with RPi OS Lite (64-bit)

Edge & Cloud: RPI 5 Model B (4GB) with RPi OS Lite (64-bit)

# Operating system & real-time optimization
After installing OS, followed instructions at to build new kernel (with PREEMPT_RT): https://www.raspberrypi.com/documentation/computers/linux_kernel.html

Before "Build" step, changed .config to enable PREEMPT_RT=y via GUI (sudo apt install libncurses-dev -y && make menuconfig).

After complete and rebooted, verified that PREEMPT_RT is enabled (uname -a #Shows this).

Isolated the cores at boot by adding to config "/boot/firmware/cmdline.txt" the following:
isolcpus=1,2,3 nohz_full=1,2,3 rcu_nocbs=1,2,3
Requires reboot

Disable CPU frequency scaling (lost after rebooting):
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

Pin interrupts away from RT cores:
```
for i in /proc/irq/*/smp_affinity; do
    echo 1 > $i
done
#OR this
for pid in $(ps -e -o pid=); do
    taskset -pc 0 $pid 2>/dev/null
done
```

Add memory lock in program (.c code): 
mlockall(MCL_CURRENT | MCL_FUTURE);

Run program in core, using schedule, and priority: sudo taskset -c [0-4] chrt -[e.g, f, r, etc.] [0-99] ./my_program arg

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
./quic_client conn 'mqtt-quic://IP_ADDRESS:14567'
```

Verify connection:
```
sudo docker exec -it CONTAINERNAME sh #Go into the container
emqx ctl clients list #Should show the client IP
```
# Edge
Install EMQX broker via Docker https://docs.emqx.com/en/emqx/latest/deploy/install-docker.html

And setup MQTT over QUIC

Pin container to core 1: sudo docker update --cpuset-cpus="1" CONTAINERNAME & sudo docker restart CONTAINERNAME

# Cloud
Installed NanoSDK client github.com/emqx/NanoSDK
