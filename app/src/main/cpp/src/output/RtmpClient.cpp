#include "pusher/output/RtmpClient.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <errno.h>
#include <random>

#include "pusher/common/Log.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace pusher {

namespace {

constexpr int kRtmpDefaultPort = 1935;

void appendU8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void appendU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void appendDoubleBE(std::vector<uint8_t>& out, double v) {
    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
}

void amfString(std::vector<uint8_t>& out, const std::string& value) {
    out.push_back(0x02);
    appendU16BE(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void amfPropertyName(std::vector<uint8_t>& out, const std::string& value) {
    appendU16BE(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void amfNumber(std::vector<uint8_t>& out, double value) {
    out.push_back(0x00);
    appendDoubleBE(out, value);
}

void amfNull(std::vector<uint8_t>& out) {
    out.push_back(0x05);
}

void amfObjectEnd(std::vector<uint8_t>& out) {
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x09);
}

std::string joinHostPort(const std::string& host, uint16_t port) {
    return host + ":" + std::to_string(port);
}

}  // namespace

// 析构函数：关闭并释放 RTMP 连接。
RtmpClient::~RtmpClient() {
    close();
}

// 解析 rtmp://host[:port]/app/stream 形式的地址。
bool RtmpClient::parseUrl(const std::string& url) {
    const std::string prefix = "rtmp://";
    if (url.rfind(prefix, 0) != 0) {
        LOGE("RTMP: invalid url %s", url.c_str());
        return false;
    }

    std::string rest = url.substr(prefix.size());
    size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "" : rest.substr(slash + 1);

    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host_ = authority.substr(0, colon);
        try {
            port_ = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
        } catch (...) {
            port_ = kRtmpDefaultPort;
        }
    } else {
        host_ = authority;
        port_ = kRtmpDefaultPort;
    }

    if (path.empty()) {
        LOGE("RTMP: missing app/stream");
        return false;
    }

    size_t firstSlash = path.find('/');
    if (firstSlash == std::string::npos) {
        app_ = path;
        stream_ = "stream";
    } else {
        app_ = path.substr(0, firstSlash);
        stream_ = path.substr(firstSlash + 1);
    }
    tcUrl_ = "rtmp://" + joinHostPort(host_, port_) + "/" + app_;
    return !host_.empty() && !stream_.empty();
}

// 创建非阻塞 TCP socket，并连接 RTMP 服务器。
bool RtmpClient::openSocket() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        LOGE("RTMP: socket failed");
        return false;
    }

    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    hostent* he = gethostbyname(host_.c_str());
    if (he == nullptr || he->h_addr_list[0] == nullptr) {
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    } else {
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        LOGE("RTMP: connect failed errno=%d", errno);
        close();
        return false;
    }

    if (rc < 0) {
        pollfd pfd{fd_, POLLOUT, 0};
        int prc = poll(&pfd, 1, 10000);
        if (prc <= 0) {
            LOGE("RTMP: connect timeout");
            close();
            return false;
        }
        int soError = 0;
        socklen_t len = sizeof(soError);
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &len);
        if (soError != 0) {
            LOGE("RTMP: connect so_error=%d", soError);
            close();
            return false;
        }
    }

    return true;
}

// 把 data 全部发送出去；遇到 EAGAIN 时用 poll 等待后再发。
bool RtmpClient::sendAll(const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{fd_, POLLOUT, 0};
            if (poll(&pfd, 1, 1000) <= 0) {
                LOGE("RTMP send timeout errno=%d", errno);
                return false;
            }
            continue;
        }
        LOGE("RTMP send failed errno=%d", errno);
        return false;
    }
    return true;
}

// 从 socket 完整接收 size 字节；遇到 EAGAIN 时用 poll 等待。
bool RtmpClient::recvAll(uint8_t* data, size_t size) {
    size_t got = 0;
    while (got < size) {
        ssize_t n = ::recv(fd_, data + got, size - got, 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{fd_, POLLIN, 0};
            if (poll(&pfd, 1, 5000) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

// 执行 RTMP 简单握手：C0/C1 -> S0/S1/S2 -> C2。
bool RtmpClient::handshake() {
    std::vector<uint8_t> c1(1536, 0);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 8; i < c1.size(); ++i) {
        c1[i] = static_cast<uint8_t>(dist(gen));
    }

    uint8_t c0 = 0x03;
    if (!sendAll(&c0, 1) || !sendAll(c1.data(), c1.size())) {
        return false;
    }

    uint8_t s0 = 0;
    if (!recvAll(&s0, 1) || s0 != 0x03) {
        LOGE("RTMP: handshake S0 invalid");
        return false;
    }

    std::vector<uint8_t> s1(1536);
    std::vector<uint8_t> s2(1536);
    if (!recvAll(s1.data(), s1.size()) || !recvAll(s2.data(), s2.size())) {
        LOGE("RTMP: handshake recv failed");
        return false;
    }

    // C2 = S1
    if (!sendAll(s1.data(), s1.size())) {
        return false;
    }
    return true;
}

// 发送一条 RTMP message；这里把每条消息作为一个 chunk 发送。
void RtmpClient::sendMessage(uint8_t type, uint32_t streamId, uint32_t timestampMs,
                             const uint8_t* payload, size_t size) {
    uint8_t csid = 3;
    if (type == 9) csid = 5;
    else if (type == 8) csid = 4;
    else if (type == 18) csid = 6;
    else if (type == 1) csid = 2;

    std::vector<uint8_t> header;
    appendU8(header, (0 << 6) | csid);

    uint32_t headerTimestamp = timestampMs < 0xFFFFFF ? timestampMs : 0xFFFFFF;
    appendU8(header, static_cast<uint8_t>((headerTimestamp >> 16) & 0xFF));
    appendU8(header, static_cast<uint8_t>((headerTimestamp >> 8) & 0xFF));
    appendU8(header, static_cast<uint8_t>(headerTimestamp & 0xFF));
    appendU8(header, static_cast<uint8_t>((size >> 16) & 0xFF));
    appendU8(header, static_cast<uint8_t>((size >> 8) & 0xFF));
    appendU8(header, static_cast<uint8_t>(size & 0xFF));
    appendU8(header, type);
    appendU32LE(header, streamId);

    if (timestampMs >= 0xFFFFFF) {
        appendU32BE(header, timestampMs);
    }

    if (!sendAll(header.data(), header.size())) {
        LOGE("RTMP sendMessage header failed type=%u", type);
        return;
    }
    if (payload != nullptr && size > 0) {
        if (!sendAll(payload, size)) {
            LOGE("RTMP sendMessage payload failed type=%u size=%zu", type, size);
        }
    }
}

// 发送 Set Chunk Size 控制消息，把 chunk size 设得足够大。
bool RtmpClient::setChunkSize() {
    std::vector<uint8_t> payload;
    // 使用足够大的 chunk size，简化发送逻辑：单条 audio/video message 作为单个 chunk 发送。
    appendU32BE(payload, 0xFFFFFF);
    sendMessage(1, 0, 0, payload.data(), payload.size());
    return true;
}

// 发送 AMF0 connect 命令。
bool RtmpClient::sendConnect() {
    std::vector<uint8_t> cmd;
    amfString(cmd, "connect");
    amfNumber(cmd, 1.0);
    cmd.push_back(0x03);
    amfPropertyName(cmd, "app");
    amfString(cmd, app_);
    amfPropertyName(cmd, "type");
    amfString(cmd, "nonprivate");
    amfPropertyName(cmd, "tcUrl");
    amfString(cmd, tcUrl_);
    amfObjectEnd(cmd);
    sendMessage(20, 0, 0, cmd.data(), cmd.size());
    return true;
}

// 发送 AMF0 createStream 命令。
bool RtmpClient::sendCreateStream() {
    std::vector<uint8_t> cmd;
    amfString(cmd, "createStream");
    amfNumber(cmd, 2.0);
    amfNull(cmd);
    sendMessage(20, 0, 0, cmd.data(), cmd.size());
    return true;
}

// 发送 AMF0 publish 命令，开始发布指定流名。
bool RtmpClient::sendPublish() {
    std::vector<uint8_t> cmd;
    amfString(cmd, "publish");
    amfNumber(cmd, 3.0);
    amfNull(cmd);
    amfString(cmd, stream_);
    amfString(cmd, "live");
    sendMessage(20, streamId_, 0, cmd.data(), cmd.size());
    return true;
}

// 解析地址、建立 TCP 连接，完成握手并发送 connect/createStream/publish。
bool RtmpClient::connect(const std::string& url) {
    close();
    if (!parseUrl(url)) return false;
    if (!openSocket()) return false;
    if (!handshake()) {
        close();
        return false;
    }
    setChunkSize();
    sendConnect();
    sendCreateStream();
    sendPublish();
    LOGI("RTMP: connected to %s/%s", app_.c_str(), stream_.c_str());
    return true;
}

// 发送一个 FLV 视频 tag 对应的 RTMP 视频消息。
bool RtmpClient::sendVideoTag(uint32_t timestampMs, const uint8_t* data, size_t size) {
    if (!connected()) return false;
    sendMessage(9, streamId_, timestampMs, data, size);
    return true;
}

// 发送一个 FLV 音频 tag 对应的 RTMP 音频消息。
bool RtmpClient::sendAudioTag(uint32_t timestampMs, const uint8_t* data, size_t size) {
    if (!connected()) return false;
    sendMessage(8, streamId_, timestampMs, data, size);
    return true;
}

// 关闭 TCP 连接。
void RtmpClient::close() {
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace pusher
