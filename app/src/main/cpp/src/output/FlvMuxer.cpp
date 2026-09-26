#include "pusher/output/FlvMuxer.h"

#include <cstring>

namespace pusher {

namespace {

void appendBe16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBe24(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool hasStartCode(const uint8_t* p, size_t remaining, size_t& startCodeLen) {
    if (remaining >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
        startCodeLen = 4;
        return true;
    }
    if (remaining >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) {
        startCodeLen = 3;
        return true;
    }
    return false;
}

int firstNaluType(const std::vector<uint8_t>& annexB) {
    size_t offset = 0;
    while (offset + 3 < annexB.size()) {
        size_t scLen = 0;
        if (hasStartCode(annexB.data() + offset, annexB.size() - offset, scLen)) {
            size_t nalu = offset + scLen;
            if (nalu < annexB.size()) {
                return annexB[nalu] & 0x1F;
            }
        }
        ++offset;
    }
    return -1;
}

std::vector<uint8_t> annexBToAvcc(const std::vector<uint8_t>& annexB) {
    std::vector<uint8_t> out;
    size_t offset = 0;
    while (offset < annexB.size()) {
        size_t scLen = 0;
        if (!hasStartCode(annexB.data() + offset, annexB.size() - offset, scLen)) {
            ++offset;
            continue;
        }
        size_t naluStart = offset + scLen;
        size_t next = naluStart;
        while (next < annexB.size()) {
            size_t nextScLen = 0;
            if (hasStartCode(annexB.data() + next, annexB.size() - next, nextScLen)) {
                break;
            }
            ++next;
        }
        size_t naluSize = next - naluStart;
        if (naluSize > 0) {
            appendBe32(out, static_cast<uint32_t>(naluSize));
            out.insert(out.end(), annexB.begin() + naluStart, annexB.begin() + next);
        }
        offset = next;
    }
    return out;
}

std::vector<uint8_t> prependSpsPps(
    const std::vector<uint8_t>& annexB,
    const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps) {
    std::vector<uint8_t> combined;
    const uint8_t startCode[4] = {0, 0, 0, 1};
    if (!sps.empty()) {
        combined.insert(combined.end(), startCode, startCode + 4);
        combined.insert(combined.end(), sps.begin(), sps.end());
    }
    if (!pps.empty()) {
        combined.insert(combined.end(), startCode, startCode + 4);
        combined.insert(combined.end(), pps.begin(), pps.end());
    }
    combined.insert(combined.end(), annexB.begin(), annexB.end());
    return combined;
}

uint8_t audioSoundFormatByte(int sampleRate, int channels) {
    uint8_t byte = 0xA0;  // AAC, 16-bit
    if (sampleRate >= 44000) {
        byte |= (3u << 2);  // 44.1 kHz
    } else if (sampleRate >= 22000) {
        byte |= (2u << 2);
    } else if (sampleRate >= 11000) {
        byte |= (1u << 2);
    }
    if (channels > 1) {
        byte |= 0x01;
    }
    return byte;
}

}  // namespace

std::vector<uint8_t> FlvMuxer::makeAvcSequenceHeader(
    const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps) const {
    std::vector<uint8_t> out;
    out.push_back(0x17);  // keyframe + AVC
    out.push_back(0x00);  // AVCPacketType: sequence header
    appendBe24(out, 0);   // composition time

    // AVCDecoderConfigurationRecord
    out.push_back(0x01);  // configurationVersion
    if (!sps.empty()) {
        out.push_back(sps[1]);  // profile
        out.push_back(sps[2]);  // compatibility
        out.push_back(sps[3]);  // level
    } else {
        out.push_back(0x42);
        out.push_back(0x00);
        out.push_back(0x1F);
    }
    out.push_back(0xFF);  // lengthSizeMinusOne: 4-byte NALU length
    out.push_back(0xE1);  // numOfSequenceParameterSets
    appendBe16(out, static_cast<uint16_t>(sps.size()));
    out.insert(out.end(), sps.begin(), sps.end());
    out.push_back(0x01);  // numOfPictureParameterSets
    appendBe16(out, static_cast<uint16_t>(pps.size()));
    out.insert(out.end(), pps.begin(), pps.end());
    return out;
}

std::vector<uint8_t> FlvMuxer::makeVideoBody(
    const VideoPacket& packet,
    const std::vector<uint8_t>& sps,
    const std::vector<uint8_t>& pps) const {
    std::vector<uint8_t> annexB = packet.data;
    if (packet.keyframe && firstNaluType(annexB) != 7 && (!sps.empty() || !pps.empty())) {
        annexB = prependSpsPps(annexB, sps, pps);
    }

    std::vector<uint8_t> out;
    out.push_back(packet.keyframe ? 0x17 : 0x27);
    out.push_back(0x01);  // AVCPacketType: NALU
    appendBe24(out, 0);   // composition time; 本实现未使用 B 帧

    std::vector<uint8_t> avcc = annexBToAvcc(annexB);
    out.insert(out.end(), avcc.begin(), avcc.end());
    return out;
}

std::vector<uint8_t> FlvMuxer::makeAudioSequenceHeader(
    const std::vector<uint8_t>& asc) const {
    std::vector<uint8_t> out;
    out.push_back(audioSoundFormatByte(audio_.sampleRate, audio_.channels));
    out.push_back(0x00);  // AACPacketType: sequence header
    out.insert(out.end(), asc.begin(), asc.end());
    return out;
}

std::vector<uint8_t> FlvMuxer::makeAudioBody(const AudioPacket& packet) const {
    std::vector<uint8_t> out;
    out.push_back(audioSoundFormatByte(audio_.sampleRate, audio_.channels));
    out.push_back(0x01);  // AACPacketType: raw AAC
    out.insert(out.end(), packet.data.begin(), packet.data.end());
    return out;
}

}  // namespace pusher
