#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace renderer {

class Output {
public:
    explicit Output(std::filesystem::path destination);
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    bool create();
    bool write(const unsigned char* data, std::size_t length);
    bool finish();
    bool publish();
    bool cleanup();
    bool published() const { return published_; }
    std::uint64_t bytes() const { return bytes_; }
    int native_error() const { return native_error_; }
    bool collision() const { return collision_; }

private:
    std::filesystem::path destination_;
    std::filesystem::path directory_;
    std::filesystem::path temporary_;
    int fd_ = -1;
    int native_error_ = 0;
    bool collision_ = false;
    bool finished_ = false;
    bool published_ = false;
    std::uint64_t bytes_ = 0;
};

}
