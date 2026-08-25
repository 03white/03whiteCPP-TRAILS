#pragma once
#include <cstddef>
#include <map>
#include <string>

class ChunkDecoder {
public:
    void feed(const char* data, size_t len, std::string& out);
    bool done() const { return state_ == State::Done; }
    const std::map<std::string, std::string>& trailers() const { return trailers_; }
private:
    enum class State {
        Size,
        Data,
        Trailer,
        Done,
    };

    bool consume_size_line();
    bool consume_data(std::string& out);
    bool consume_trailer_line();
    static size_t parse_chunk_size(const std::string& line);

    State state_{State::Size};
    std::string buffer_;  // 缓冲区
    size_t remainingPayloadBytes_{0};
    std::map<std::string, std::string> trailers_;
};