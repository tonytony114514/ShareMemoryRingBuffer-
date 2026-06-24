cmake_minimum_required(VERSION 3.10)
project(SharedMemoryRingBuffer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# 核心库
add_library(SharedMemoryRingBuffer src/SharedMemoryRingBuffer.cpp)
target_link_libraries(SharedMemoryRingBuffer pthread rt)

# 交互式命令行工具（需适配新接口）
add_executable(shm_cli examples/shm_cli.cpp)
target_link_libraries(shm_cli SharedMemoryRingBuffer)

# 点云模拟生产者（无需修改）
add_executable(lidar_simulator examples/lidar_simulator.cpp)
target_link_libraries(lidar_simulator SharedMemoryRingBuffer)

# 高速消费者（已适配新接口）
add_executable(fast_consumer examples/fast_consumer.cpp)
target_link_libraries(fast_consumer SharedMemoryRingBuffer)

# 注释或删除以下旧示例
# add_executable(producer examples/producer.cpp)
# add_executable(consumer examples/consumer.cpp)
# add_executable(string_test examples/string_test.cpp)
# add_executable(struct_test examples/struct_test.cpp)
# add_executable(test_main src/main.cpp)

add_executable(produce_example examples/produce_example.cpp)
target_link_libraries(produce_example SharedMemoryRingBuffer)

add_library(SimpleShm src/SimpleShm.cpp)
target_link_libraries(SimpleShm SharedMemoryRingBuffer)

add_executable(my_publisher examples/my_publisher.cpp)
target_link_libraries(my_publisher SimpleShm)

add_executable(my_subscriber examples/my_subscriber.cpp)
target_link_libraries(my_subscriber SimpleShm)

# 摄像头示例（需要 OpenCV）
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(camera_publisher examples/camera_publisher.cpp)
target_link_libraries(camera_publisher SimpleShm ${OpenCV_LIBS})

add_executable(camera_subscriber examples/camera_subscriber.cpp)
target_link_libraries(camera_subscriber SimpleShm ${OpenCV_LIBS})


# Demo targets
add_executable(camera_demo_publisher examples/camera_demo_publisher.cpp)
target_link_libraries(camera_demo_publisher SimpleShm ${OpenCV_LIBS})

add_executable(lidar_demo_publisher examples/lidar_demo_publisher.cpp)
target_link_libraries(lidar_demo_publisher SimpleShm)

add_executable(image_demo_subscriber examples/image_demo_subscriber.cpp)
target_link_libraries(image_demo_subscriber SimpleShm ${OpenCV_LIBS})

add_executable(pointcloud_demo_subscriber examples/pointcloud_demo_subscriber.cpp)
target_link_libraries(pointcloud_demo_subscriber SimpleShm)
