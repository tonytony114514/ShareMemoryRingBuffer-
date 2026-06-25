//进入build

cd /home/tony/ShareMemoryRingBuffer1/build


//打开演示代码

./demo.sh

./demo_big.sh


//核心代码在/home/tony/ShareMemoryRingBuffer1/src的SimpleShm.cpp,SimpleShm.h里

生产者

ShmPublisher(const std::string& name, size_t size, bool auto_create);

bool publish(const void* data, size_t len, const char* filename, int timeout_ms);

消费者

ShmSubscriber(const std::string& name);

bool receive(std::vector<uint8_t>& data, std::string* filename, int timeout_ms);

文件中有示例图

程序能长时间运行，现在已持续运行6小时

听项目要求的人说要加一个端口，所以加了一个网络端口给远端调用
