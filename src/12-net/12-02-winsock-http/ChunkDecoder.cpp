#include "ChunkDecoder.h"
#include <stdexcept>

size_t ChunkDecoder::parse_chunk_size(const std::string& line) {
    size_t semicolon = line.find(';');
    std::string size_str = line.substr(0, semicolon);
    size_t start = size_str.find_first_not_of(" \t");
    if (start == std::string::npos) {
        throw std::runtime_error("ChunkDecoder: empty chunk-size");
    }
    size_str = size_str.substr(start);
    try {
        return std::stoull(size_str, nullptr, 16);
    } catch (const std::invalid_argument&) {
        throw std::runtime_error("ChunkDecoder: invalid chunk-size (not hex)");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("ChunkDecoder: chunk-size out of range");
    }
}

void ChunkDecoder::feed(const char* data, size_t len, std::string& out) {
    buffer_.append(data, len);
    
    while (true) {
        bool progressed = false;
        switch (state_) {
            case State::Size:     progressed = consume_size_line(); break;
            case State::Data:     progressed = consume_data(out);   break;
            case State::Trailer:  progressed = consume_trailer_line(); break;
            case State::Done:     return;
        }
        if (!progressed) break; // 数据不够，或发生错误，退出等下次
    }
}

bool ChunkDecoder::consume_size_line() {
    size_t crlf = buffer_.find("\r\n");
    if (crlf == std::string::npos) {
        return false; // 没凑齐一行，等
    }

    std::string line = buffer_.substr(0, crlf);
    buffer_.erase(0, crlf + 2); // 吃掉这一行以及 \r\n

    size_t chunk_size = parse_chunk_size(line);
    if (chunk_size > 0) {
        remainingPayloadBytes_ = chunk_size;
        state_ = State::Data;
    } else {
        state_ = State::Trailer;
    }
    return true; // 成功处理了一个长度行
}

bool ChunkDecoder::consume_data(std::string& out) {
    // 检查：缓冲区里有没有足够的数据？
    if (buffer_.size() < remainingPayloadBytes_) {
        return false; // 数据不够，等
    }

    // 1. 把有效载荷切出来，追加给 out
    out.append(buffer_.data(), remainingPayloadBytes_);
    buffer_.erase(0, remainingPayloadBytes_);
    remainingPayloadBytes_ = 0; // 归零

    // 2. 现在必须吃掉后面的 \r\n
    if (buffer_.size() >= 2 && buffer_[0] == '\r' && buffer_[1] == '\n') {
        buffer_.erase(0, 2);
        state_ = State::Size; // 切回 Size，准备读下一块的长度
        return true;
    } else {
        // 数据读完了，但 \r\n 还没到（比如只收到了 \r）
        // 此时状态停在 Data，但 remainingPayloadBytes_ 已经为 0。
        // 下一次 feed 进来时，会再次进入这个函数，执行上面的检查，
        // 由于 remainingPayloadBytes_ == 0，buffer_.size() >= 0 为真，
        // 它会直接跳到步骤 2 去等 \r\n。
        // 注意：此时我们绝不能消耗任何字节，所以直接返回 false 等数据。
        return false;
    }
}

bool ChunkDecoder::consume_trailer_line() {
    size_t crlf = buffer_.find("\r\n");
    if (crlf == std::string::npos) {
        return false; // 没凑齐一行
    }

    std::string line = buffer_.substr(0, crlf);
    buffer_.erase(0, crlf + 2);

    if (line.empty()) {
        // 空行：Trailer 结束
        state_ = State::Done;
        return true;
    }
    // 解析 Key: Value
    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("ChunkDecoder: invalid trailer line");
    }
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    size_t v_start = value.find_first_not_of(" \t");
    if (v_start != std::string::npos) {
        value = value.substr(v_start);
    } else {
        value.clear();
    }
    trailers_[key] = value; // 如果重复 key 则覆盖
    return true; // 处理完一行，继续读下一行
}