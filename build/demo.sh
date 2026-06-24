#!/bin/bash

SHM_NAME="/demo_shm"
BUILD_DIR="."

# 检查可执行文件
MISSING=""
for exe in pointcloud_demo_subscriber camera_demo_publisher lidar_demo_publisher; do
    [ ! -x "${BUILD_DIR}/${exe}" ] && MISSING="${MISSING} ${exe}"
done
if [ -n "$MISSING" ]; then
    echo "❌ 缺少可执行文件:${MISSING}"
    echo "   请先编译： cd build && cmake .. && make -j4"
    exit 1
fi

# 清理残留
echo "🧹 清理残留环境..."
killall -9 camera_demo_publisher lidar_demo_publisher pointcloud_demo_subscriber image_demo_subscriber shm_cli 2>/dev/null
rm -f /dev/shm${SHM_NAME}

# 启动消费者（带守护：退出后自动重启）
start_subscriber() {
    local name="$1" pid_var="$2" exe="$3"
    while true; do
        ${BUILD_DIR}/${exe} &
        eval ${pid_var}=$!
        wait $!
        echo "⚠️  ${name} 退出，3 秒后重启..."
        sleep 3
    done
}

start_subscriber "点云消费者" PID_SUB pointcloud_demo_subscriber &
PID_SUB_GUARD=$!
sleep 1

# 图像消费者（如果有）
if [ -x "${BUILD_DIR}/image_demo_subscriber" ]; then
    start_subscriber "图像消费者" PID_IMG image_demo_subscriber &
    PID_IMG_GUARD=$!
    sleep 1
else
    echo "⚠️  未找到 image_demo_subscriber（需要 OpenCV），跳过。"
    PID_IMG_GUARD=""
fi

# 启动摄像头生产者
echo "📷 启动摄像头生产者..."
${BUILD_DIR}/camera_demo_publisher &
PID_CAM=$!
sleep 0.5

# 启动点云生产者
echo "📡 启动点云生产者..."
${BUILD_DIR}/lidar_demo_publisher &
PID_LID=$!

echo ""
echo "✅ 演示已启动（消费者带自动重启守护）！"
echo "   - 摄像头生产者 PID: ${PID_CAM}"
echo "   - 点云生产者 PID: ${PID_LID}"
echo ""
echo "   按 Ctrl+C 停止演示..."

# 捕获 Ctrl+C，杀死所有进程
trap "echo '🛑 停止所有进程...'; kill ${PID_SUB_GUARD} ${PID_IMG_GUARD} ${PID_CAM} ${PID_LID} 2>/dev/null; rm -f /dev/shm${SHM_NAME}; echo '✅ 已清理。'; exit 0" INT

# 等待守护进程（或任意子进程）
wait
