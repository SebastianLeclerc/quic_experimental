# quic_experimental
Source for modified EMQX, NanoSDK IoT MQTT over QUIC project


cloud@cloud:~/NanoSDK/demo/quic_mqtt $ ./sub sub mqtt-quic://192.168.0.29:14567 0 sensor/# 0

edge@edge:~/NanoSDK/demo/quic_mqtt $ sudo taskset -c 3 chrt -f 80 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/1 10 10 1 1 & sudo taskset -c 2 chrt -f 78 ./pub pub mqtt-quic://192.168.0.29:14567 0 sensor/2 10 10 1 1

# Hardware
Sensor: RPi 4 Model B (4GB) with RPi OS Lite (64-bit)

Edge & Cloud: RPI 5 Model B (4GB) with RPi OS Lite (64-bit)

# Operating system
After installing OS, followed instructions at to build new kernel (with PREEMPT_RT): https://www.raspberrypi.com/documentation/computers/linux_kernel.html

Before "Build" step, changed .config to enable PREEMPT_RT=y via GUI (sudo apt install libncurses-dev -y && make menuconfig).

After complete, verified that PREEMPT_RT is enabled (uname -a).

# Sensor

# Edge


# Cloud
