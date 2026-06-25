#!/bin/bash

SHM_NAME="/bigmsg_shm"
BUILD_DIR="."

# 检查可执行文件
if [ ! -x "${BUILD_DIR}/bigmsg_publisher" ] || [ ! -x "${BUILD_DIR}/bigmsg_subscriber" ]; then
    echo "❌ 请先编译项目"
    exit 1
fi

# 清理残留
echo "🧹 清理残留环境..."
killall -9 bigmsg_publisher bigmsg_subscriber 2>/dev/null
rm -f /dev/shm${SHM_NAME}

# 后台启动消费者，输出到日志
echo "📡 启动消费者（后台）..."
nohup ${BUILD_DIR}/bigmsg_subscriber > bigmsg_subscriber.log 2>&1 &
PID_SUB=$!

sleep 1

# 后台启动生产者，输出到日志
echo "📷 启动生产者（后台）..."
nohup ${BUILD_DIR}/bigmsg_publisher > bigmsg_publisher.log 2>&1 &
PID_PUB=$!

echo ""
echo "✅ 大消息测试已启动（后台运行）"
echo "   - 消费者 PID: ${PID_SUB}  (日志: bigmsg_subscriber.log)"
echo "   - 生产者 PID: ${PID_PUB}  (日志: bigmsg_publisher.log)"
echo ""
echo "查看实时统计: tail -f bigmsg_subscriber.log"
echo "停止测试: kill ${PID_SUB} ${PID_PUB} && rm -f /dev/shm${SHM_NAME}"
