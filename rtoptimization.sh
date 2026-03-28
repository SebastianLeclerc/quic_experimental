#!/bin/bash
# Preparing for running real-time tasks (SCHED_FIFO / SCHED_DEADLINE)
# Linux sensor 6.12.78-v8+ #1 SMP PREEMPT_RT Fri Mar 27 02:09:06 CET 2026 aarch64 GNU/Linux

u=$(whoami)
error=0

# PREEMPT_RT
if uname -a | grep -q "PREEMPT_RT"; then
    echo "PREEMPT_RT kernel detected, nice!"
else
    echo "PREEMPT_RT not found :("
    exit 1
fi

# Hyperthreading off/not available
if lscpu | grep -q "^Thread(s) per core:[[:space:]]*1$"; then
    echo "Hyperthreading is OFF or not supported, nice!"
else
    echo "Hyperthreading is ON :("
    exit 1
fi

# Check kernel params
if grep -q "isolcpus=1,2,3 nohz_full=1,2,3 rcu_nocbs=1,2,3" /boot/firmware/cmdline.txt; then
    echo "CPUs 1,2,3 already isolated at boot, nice!"
else
    echo "Fixing CPU isolation :("
    echo ' isolcpus=1,2,3 nohz_full=1,2,3 rcu_nocbs=1,2,3' | sudo tee -a /boot/firmware/cmdline.txt
    error=1
fi

# Check force_turbo
if grep -q "force_turbo=1" /boot/firmware/config.txt; then
    echo "CPU frequency scaling already off, nice!"
else
    echo "Fixing CPU frequency scaling :("
    echo 'force_turbo=1' | sudo tee -a /boot/firmware/config.txt
    error=1
fi

# Check CPU thermal throttling warnings
if grep -q "avoid_warnings=1" /boot/firmware/config.txt; then
    echo "CPU thermal throttling warnings off, nice!"
else
    echo "Fixing CPU thermal throttling :("
    echo 'avoid_warnings=1' | sudo tee -a /boot/firmware/config.txt
    error=1
fi

# Check global memory lock limit
if grep -q "^#DefaultLimitMEMLOCK=8M" /etc/systemd/system.conf; then
    echo "Fixing global memory lock :("
    sudo sed -i 's/^#DefaultLimitMEMLOCK=8M/DefaultLimitMEMLOCK=infinity/' /etc/systemd/system.conf
    sudo systemctl daemon-reexec
    error=1
else
    echo "Global memory already configured, nice!"
fi

# Check user memory access limit
if grep -q "^$u hard memlock unlimited" /etc/security/limits.conf; then
    echo "User memory already configured, nice!"
else
    echo "Fixing user memory lock :("
    {
        echo ""
        echo "# Added by setup script"
        echo "$u hard memlock unlimited"
        echo "$u soft memlock unlimited"
        echo "root hard memlock unlimited"
        echo "root soft memlock unlimited"
    } | sudo tee -a /etc/security/limits.conf > /dev/null

    sudo systemctl daemon-reexec
    error=1
fi


# Reboot if needed
if [ "$error" -eq 1 ]; then
    echo "Gotta reboot :("
    sudo reboot
    exit 1
else
    echo "Passed all the tests, nice!"
fi

###################################### NO REBOOT, NICE! ######################################
echo "Set performance mode"
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null

echo "Check min/max frequency in RT cores 1,2,3"
maxfreq=$(cat /sys/devices/system/cpu/cpu1/cpufreq/scaling_max_freq)
echo "$maxfreq" | sudo tee /sys/devices/system/cpu/cpu1/cpufreq/scaling_min_freq > /dev/null
echo "$maxfreq" | sudo tee /sys/devices/system/cpu/cpu2/cpufreq/scaling_min_freq > /dev/null
echo "$maxfreq" | sudo tee /sys/devices/system/cpu/cpu3/cpufreq/scaling_min_freq > /dev/null

echo "Disable RT Throttling (RT tasks can run max 1 s period)"
p=$(cat /proc/sys/kernel/sched_rt_period_us)
echo "Current period is: $p" 
echo -1 | sudo tee /proc/sys/kernel/sched_rt_runtime_us > /dev/null

echo "Migrate (all possible) IRQs to CPU 0 (exclude CPU 1-3)"
for I in /proc/irq/[0-9]*; do
    [ -d "$I" ] || continue
    irq=$(basename "$I")
    echo 0 | sudo tee "$I/smp_affinity_list" > /dev/null
done

echo "Disable services that we dont need"
sudo systemctl disable --now avahi-daemon.socket
sudo systemctl disable --now avahi-daemon.service
sudo systemctl disable --now bluetooth.service
sudo systemctl disable --now ModemManager.service

############## IGNORED CONFIG ##############
# Deep sleep state "Deep C-states" not relevant on RPi, equivalent probably not needed (low impact)

# Can probably skip "cset shield" with this setup, e.g.:
# sudo apt install cgroup-tools
# sudo cset shield -c 3 -k on
# sudo cset shield --exec ./my_program
# Removes "all"´processes from core3 and shields
############################################