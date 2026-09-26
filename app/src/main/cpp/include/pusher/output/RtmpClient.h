#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pusher {

/**
 * 最小 RTMP 发布客户端：
 * 支持简单握手、AMF0 connect/createStream/publish，以及 FLV audio/video tag 推流。
 * 面向 nginx-rtmp / SRS 等常见服务器；不实现 RTMPS、relay 与复杂握手。
 */
class RtmpClient {
public:
    RtmpClient() = default;
    ~RtmpClient();

    bool connect(const std::string& url);
    void close();
    bool connected() const { return fd_ >= 0; }

    bool sendVideoTag(uint32_t timestampMs, const uint8_t* data, size_t size);
    bool sendAudioTag(uint32_t timestampMs, const uint8_t* data, size_t size);

private:
    bool parseUrl(const std::string& url);
    bool openSocket();
    bool handshake();
    bool setChunkSize();
    bool sendConnect();
    bool sendCreateStream();
    bool sendPublish();

    bool sendAll(const uint8_t* data, size_t size);
    bool recvAll(uint8_t* data, size_t size);
    void sendMessage(uint8_t type, uint32_t streamId, uint32_t timestampMs,
                     const uint8_t* payload, size_t size);

    int fd_ = -1;
    std::string host_;
    std::string app_;
    std::string stream_;
    std::string tcUrl_;
    uint16_t port_ = 1935;
    uint32_t streamId_ = 1;
};

}  // namespace pusher
