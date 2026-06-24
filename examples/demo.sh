#!/bin/bash

SHM_NAME="/demo_shm"
BUILD_DIR="."

# 检查可执行文件
if [ ! -x "${BUILD_DIR}/pointcloud_demo_subscriber" ] || \
   [ ! -x "${BUILD_DIR}/camera_demo_publisher" ] || \
   [ ! -x "${BUILD_DIR}/lidar_demo_publisher" ]; then
    echo "❌ 请先编译项目： cd build && cmake .. && make -j4"
    exit 1
fi

# 1. 彻底清理环境
echo "🧹 清理残留..."
killall -9 camera_demo_publisher lidar_demo_publisher pointcloud_demo_subscriber image_demo_subscriber shm_cli 2>/dev/null
rm -f /dev/shm${SHM_NAME}
sleep 0.5

# 2. 启动消费者
echo "📡 启动消费者..."
${BUILD_DIR}/pointcloud_demo_subscriber &
PID_SUB=$!
sleep 1

# 3. 启动两个生产者
echo "📷 启动摄像头生产者..."
${BUILD_DIR}/camera_demo_publisher &
PID_CAM=$!
sleep 0.5

echo "📡 启动点云生产者..."
${BUILD_DIR}/lidar_demo_publisher &
PID_LID=$!

echo ""
echo "✅ 演示已启动（PID: ${PID_SUB} ${PID_CAM} ${PID_LID}）"
echo "   按 Ctrl+C 停止演示..."

# 4. 后台自动 reset 守护：每 3 秒重置共享内存，防止死锁
(
    while true; do
        sleep 3
        if [ -f /dev/shm${SHM_NAME} ]; then
            # 使用 shm_cli 做一次静默 reset
            ${BUILD_DIR}/shm_cli ${SHM_NAME} <<< "reset" > /dev/null 2>&1
        fi
    done
) &
PID_WATCHDOG=$!

# 5. 捕获 Ctrl+C 停止所有进程
trap "echo '🛑 停止...'; kill ${PID_SUB} ${PID_CAM} ${PID_LID} ${PID_WATCHDOG} 2>/dev/null; rm -f /dev/shm${SHM_NAME}; echo '✅ 已清理。'; exit 0" INT

# 等待主进程（消费者或生产者）结束
wait
