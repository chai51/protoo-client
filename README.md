#### 介绍
用于多方实时应用程序的极简且可扩展的 c++ 信令框架。

#### 安装教程
cmake .. && make

#### 用法
```cpp
using namespace protoo;
std::string url = "wss://ip:port/?peerId=1000&roomId=1200";
auto transport = std::make_unique<WebSocketTransport>(url, nullptr);
auto peer = std::make_unique<Peer>(std::move(transport));
peer_->set_open_handler([]() {
  std::cout << "wss open" << std::endl;
});
peer_->set_disconnected_handler([]() {
  std::cout << "wss disconnected" << std::endl;
});
peer_->set_close_handler([](){
  std::cout << "wss close" << std::endl;
});
peer_->set_failed_handler([](int currentAttempt) {
  std::cout << "wss failed" << std::endl;
});
peer_->set_notification_handler([](const json& notification){
  std::cout << "wss notification" << std::endl;
});
peer_->set_request_handler([](const json& request, accept_handler accept, reject_handler reject) {
  true ? accept({}) : reject(-1, "error");
});
```
