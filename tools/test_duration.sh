#!/bin/bash
# 测试30秒看程序是否正常退出
./modules/Core/devzero_stress_test pub 30 3 4096 1000 &
PUB_PID=$!

sleep 2

for i in 0 1 2; do
    ./modules/Core/devzero_stress_test sub 30 3 4096 $i 1000 &
done

./modules/Core/devzero_stress_test monitor 30 3 4096 1000 &
MON_PID=$!

echo "等待30秒..."
sleep 35

if ps -p $PUB_PID > /dev/null 2>&1; then
    echo "ERROR: Publisher仍在运行!"
    pkill -9 devzero_stress_test
else
    echo "SUCCESS: Publisher已正常退出"
fi
