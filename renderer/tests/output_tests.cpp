#include "output.h"
#include "status.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--cleanup-failure") {
        const auto destination = fs::u8path(argv[2]);
        renderer::Output output(destination);
        const unsigned char bytes[] = "synthetic published data";
        require(output.create() && output.write(bytes, sizeof(bytes)) && output.finish() && output.publish(),
                "cleanup failure setup");
        fs::path extra;
        for (const auto& entry : fs::directory_iterator(destination.parent_path())) {
            if (entry.is_directory() && entry.path().filename().string().find(".gxps-job-") == 0) {
                extra = entry.path() / "simulate-cleanup-denial";
                std::ofstream(extra) << "test-only blocker";
            }
        }
        require(!extra.empty(), "test found transaction directory");
        renderer::Status status;
        renderer::complete_job(output, status);
        require(status.exit_code == 24 && status.output_published && !status.cleanup_complete,
                "cleanup failure after publication must remain an error with output_published true");
        fs::remove(extra);
        require(output.cleanup(), "test fault cleanup");
        return renderer::emit(status);
    }
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = fs::temp_directory_path() / ("output-tests-" + suffix);
    fs::create_directory(directory);
    try {
        const auto destination = directory / "output.pdf";
        const unsigned char bytes[] = "%PDF-synthetic-output-test";
        {
            renderer::Output output(destination);
            require(!output.finish(), "finish before create must fail");
            require(!output.publish(), "publish before finish must fail");
            require(!output.write(bytes, sizeof(bytes)), "write before create must fail");
            require(output.create(), "create must succeed");
            require(!fs::exists(destination), "destination must not exist while writing");
            require(output.write(bytes, sizeof(bytes)), "write must succeed");
            require(!fs::exists(destination), "uncommitted output must stay private");
            require(output.finish(), "flush/close must succeed");
            require(!output.write(bytes, sizeof(bytes)), "write after close must fail");
            require(!output.finish(), "double finish must fail");
            require(output.publish(), "publish must succeed");
            require(!output.publish(), "double publication must fail");
            require(output.cleanup(), "cleanup must succeed");
        }
        require(fs::file_size(destination) == sizeof(bytes), "published byte size must match");
        {
            renderer::Output collision(destination);
            require(!collision.create() && collision.collision(), "existing path must not be overwritten");
            require(collision.cleanup(), "collision cleanup must succeed");
        }
        fs::remove(destination);
        {
            renderer::Output race(destination);
            require(race.create(), "race transaction creation");
            require(race.write(bytes, sizeof(bytes)) && race.finish(), "race transaction completion");
            std::ofstream(destination) << "competitor";
            require(!race.publish() && race.collision(), "late collision must not overwrite");
            require(race.cleanup(), "race cleanup");
            require(fs::file_size(destination) == 10, "competitor preserved");
        }
        fs::remove(destination);
        {
            renderer::Output abort(destination);
            require(abort.create() && abort.write(bytes, sizeof(bytes)), "abort setup");
            require(abort.cleanup(), "unfinished output cleanup");
            require(!fs::exists(destination), "abort must not publish");
        }
        require(fs::is_empty(directory), "all transaction debris removed");
        fs::remove(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        for (const auto& entry : fs::directory_iterator(directory)) {
            std::cerr << "retained test artifact: " << entry.path() << '\n';
        }
        return 1;
    }
}
