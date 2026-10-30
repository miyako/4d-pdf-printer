#pragma once
#include "Job.h"
#include <windows.h>
#include <filesystem>
#include <utility>
#include <vector>

namespace pdf {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { Close(); value = std::exchange(other.value, INVALID_HANDLE_VALUE); }
        return *this;
    }
    ~Handle() { Close(); }
    void Close() noexcept {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
inline void Check(BOOL value) { if (!value) throw_last_error(); }
Handle OpenRead(fs::path const& path);
std::wstring Hash(HANDLE file);
std::wstring Hash(fs::path const& path);
std::vector<Handle> SafeParents(fs::path const& target, bool existingAllowed = false,
    bool packageStorage = false);
void Machine(HANDLE file);
struct Runtime {
    fs::path root;
    std::wstring inventory, receipt, provenance;
    std::vector<Handle> locks;
};
Runtime InspectRuntime(fs::path const& root);
Runtime ConfiguredRuntime(StorageFolder const& root);
std::uint64_t Render(Runtime const& runtime, fs::path const& source, fs::path const& output,
    fs::path const& work, std::function<bool()> stop, JsonObject const& observation);
void PublishNew(fs::path const& staged, fs::path const& destination,
    StorageFolder const& job, std::wstring_view id, Mode mode, std::function<bool()> stop);
JsonObject Reconcile(StorageFolder const& job, std::wstring_view id, Mode mode);
}
