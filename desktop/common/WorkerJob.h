#pragma once
#include "Trace.h"
#include <vector>

namespace proof
{
    struct RuntimeFile { std::filesystem::path path; std::wstring sha256; };
    struct Runtime
    {
        std::filesystem::path root;
        std::wstring inventoryHash;
        std::vector<RuntimeFile> files;
    };
    Runtime LoadRuntime(std::filesystem::path const& root);
    bool ValidSuccess(JsonObject const& status, DWORD exit);
    JsonObject ParseSuccess(std::string const& bytes, DWORD exit);
    struct WorkerOptions
    {
        std::chrono::milliseconds deadline{60000};
        enum Fault { None, Cancel, Terminate } fault = None;
    };
    JsonObject RunWorker(Runtime const& runtime, std::filesystem::path const& input,
        std::filesystem::path const& jobRoot, Trace& trace, WorkerOptions options = {});
}
