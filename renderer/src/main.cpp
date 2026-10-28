#include "output.h"
#include "status.h"

#include <archive.h>
#include <archive_entry.h>
#include <cairo-pdf.h>
#include <libgxps/gxps.h>
#include <glib/gstdio.h>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {

std::atomic<bool> backend_warning{false};
constexpr std::uint64_t max_expanded_bytes = 1024ULL * 1024 * 1024;
constexpr unsigned int max_entries = 16384;
constexpr unsigned int max_pages = 10000;

using renderer::Status;
using renderer::emit;

struct Unref {
    template<class T> void operator()(T* object) const { if (object) g_object_unref(object); }
};
template<class T> using Object = std::unique_ptr<T, Unref>;
struct Error {
    GError* value = nullptr;
    ~Error() { g_clear_error(&value); }
};
using Surface = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;
using Context = std::unique_ptr<cairo_t, decltype(&cairo_destroy)>;

bool check_warning(Status& s) {
    if (!backend_warning.load()) {
        return true;
    }
    s.fail(33, "backend_warning", "Backend reported a warning; partial output was rejected.");
    return false;
}

bool preflight(const fs::path& input, Status& s) {
    s.stage = "input";
    std::error_code ec;
    if (!fs::is_regular_file(input, ec)) {
        s.fail(10, "input_io", "Input must be an accessible regular file.", ec.value(), "filesystem");
        return false;
    }
    // Require a seekable ZIP with a central directory, not recovery of a truncated stream.
    std::unique_ptr<archive, decltype(&archive_read_free)> ar(archive_read_new(), archive_read_free);
    if (!ar) {
        s.fail(70, "internal", "Cannot allocate archive reader.");
        return false;
    }
    archive_read_support_format_zip_seekable(ar.get());
#ifdef _WIN32
    const auto opened = archive_read_open_filename_w(ar.get(), input.c_str(), 65536);
#else
    const auto opened = archive_read_open_filename(ar.get(), input.c_str(), 65536);
#endif
    if (opened != ARCHIVE_OK) {
        const auto native = archive_errno(ar.get());
        if (native == EACCES || native == ENOENT || native == EIO || native == EMFILE || native == ENFILE) {
            s.fail(10, "input_io", "Cannot read input package.", native, "libarchive");
        } else {
            s.fail(11, "invalid_package", "Input is not a readable, complete ZIP package.",
                   native, "libarchive");
        }
        return false;
    }
    unsigned int count = 0;
    std::uint64_t total = 0;
    archive_entry* entry = nullptr;
    int next = ARCHIVE_OK;
    char buffer[65536];
    while ((next = archive_read_next_header(ar.get(), &entry)) == ARCHIVE_OK) {
        if (archive_entry_is_encrypted(entry) > 0) {
            s.fail(12, "unsupported_input", "Encrypted packages are not supported.");
            return false;
        }
        if (++count > max_entries) {
            s.fail(13, "resource_limit", "Package exceeds the entry limit.");
            return false;
        }
        la_ssize_t n = 0;
        while ((n = archive_read_data(ar.get(), buffer, sizeof(buffer))) > 0) {
            total += static_cast<std::uint64_t>(n);
            if (total > max_expanded_bytes) {
                s.fail(13, "resource_limit", "Package exceeds the expanded-byte limit.");
                return false;
            }
        }
        if (n < 0) {
            s.fail(11, "invalid_package", "Package data is corrupt or uses unsupported compression.",
                   archive_errno(ar.get()), "libarchive");
            return false;
        }
    }
    if (next != ARCHIVE_EOF || count == 0 || archive_read_close(ar.get()) != ARCHIVE_OK) {
        s.fail(11, "invalid_package", "Package structure or data is incomplete.",
               archive_errno(ar.get()), "libarchive");
        return false;
    }
    return true;
}

cairo_status_t write_pdf(void* closure, const unsigned char* data, unsigned int length) {
    return static_cast<renderer::Output*>(closure)->write(data, length) ?
        CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}

void convert(const fs::path& input, renderer::Output& output, Status& s) {
    if (!preflight(input, s)) {
        return;
    }
    s.stage = "document";
    Object<GFile> file(g_file_new_for_path(input.u8string().c_str()));
    Error error;
    Object<GXPSFile> xps(gxps_file_new(file.get(), &error.value));
    if (!xps || error.value) {
        s.gfail(30, "document", "Cannot read XPS/OXPS fixed-document sequence.", error.value);
        return;
    }
    s.documents = gxps_file_get_n_documents(xps.get());
    if (s.documents == 0 || s.documents > max_pages) {
        s.fail(30, "document", "Document sequence is empty or exceeds the document limit.");
        return;
    }
    if (!check_warning(s)) {
        return;
    }
    s.stage = "output";
    if (!output.create()) {
        s.fail(output.collision() ? 20 : 21, output.collision() ? "output_exists" : "output_io",
               "Cannot create a new destination transaction.", output.native_error(), "filesystem");
        return;
    }
    Surface surface(cairo_pdf_surface_create_for_stream(write_pdf, &output, 1, 1), cairo_surface_destroy);
    // Stable timestamps avoid wall-clock metadata changes; byte identity across backends is not promised.
    cairo_pdf_surface_set_metadata(surface.get(), CAIRO_PDF_METADATA_CREATE_DATE, "2000-01-01T00:00:00Z");
    cairo_pdf_surface_set_metadata(surface.get(), CAIRO_PDF_METADATA_MOD_DATE, "2000-01-01T00:00:00Z");
    cairo_surface_set_fallback_resolution(surface.get(), 300, 300);
    unsigned int total_pages = 0;
    for (unsigned int d = 0; d < s.documents; ++d) {
        s.document = d + 1;
        s.page = 0;
        s.stage = "document";
        Object<GXPSDocument> doc(gxps_file_get_document(xps.get(), d, &error.value));
        if (!doc || error.value) {
            s.gfail(30, "document", "Cannot load fixed document.", error.value);
            return;
        }
        const auto pages = gxps_document_get_n_pages(doc.get());
        if (pages == 0 || pages > max_pages - total_pages) {
            s.fail(13, "resource_limit", "Document is empty or exceeds the total page limit.");
            return;
        }
        total_pages += pages;
        for (unsigned int p = 0; p < pages; ++p) {
            s.page = p + 1;
            s.stage = "page";
            Object<GXPSPage> page(gxps_document_get_page(doc.get(), p, &error.value));
            if (!page || error.value) {
                s.gfail(31, "page", "Cannot load fixed page.", error.value);
                return;
            }
            double width = 0, height = 0;
            gxps_page_get_size(page.get(), &width, &height);
            if (!std::isfinite(width) || !std::isfinite(height) ||
                width <= 0 || height <= 0 || width > 200000 || height > 200000) {
                s.fail(31, "page", "Page dimensions are invalid or exceed the prototype limit.");
                return;
            }
            cairo_pdf_surface_set_size(surface.get(), width * 0.75, height * 0.75);
            Context cr(cairo_create(surface.get()), cairo_destroy);
            cairo_scale(cr.get(), 0.75, 0.75);
            s.stage = "render";
            if (!gxps_page_render(page.get(), cr.get(), &error.value) || error.value) {
                s.gfail(32, "render", "Page rendering failed.", error.value);
                return;
            }
            cairo_show_page(cr.get());
            const auto context_status = cairo_status(cr.get());
            const auto surface_status = cairo_surface_status(surface.get());
            if (context_status != CAIRO_STATUS_SUCCESS || surface_status != CAIRO_STATUS_SUCCESS) {
                const auto failure = context_status != CAIRO_STATUS_SUCCESS ? context_status : surface_status;
                s.fail(failure == CAIRO_STATUS_WRITE_ERROR ? 21 : 32,
                       failure == CAIRO_STATUS_WRITE_ERROR ? "output_io" : "render",
                       "Cairo could not complete the page.", static_cast<int>(failure), "cairo");
                return;
            }
            if (!check_warning(s)) {
                return;
            }
            ++s.pages_completed;
        }
    }
    s.stage = "finalize";
    cairo_surface_finish(surface.get());
    const auto final_status = cairo_surface_status(surface.get());
    if (final_status != CAIRO_STATUS_SUCCESS) {
        s.fail(22, "finalize", "Cairo PDF finalization failed.", static_cast<int>(final_status), "cairo");
        return;
    }
    surface.reset();
    if (!check_warning(s)) {
        return;
    }
    if (!output.finish()) {
        s.fail(22, "finalize", "PDF flush or close failed.", output.native_error(), "filesystem");
        return;
    }
    s.stage = "publish";
    if (!output.publish()) {
        s.fail(output.collision() ? 20 : 23, output.collision() ? "output_exists" : "publish",
               "Completed PDF could not be published without replacement.",
               output.native_error(), "filesystem");
        return;
    }
    s.document = 0;
    s.page = 0;
    s.stage = "complete";
}

bool parse(const std::vector<std::string>& args, fs::path& input, fs::path& output, Status& s) {
    if (args.size() == 2 && (args[1] == "--version" || args[1] == "--help")) {
        s.result = "info";
        s.stage = args[1] == "--version" ? "version" : "help";
        if (args[1] == "--help") {
            fputs("pdf-render-worker --protocol-version 1 --input <XPS/OXPS> --output <new PDF>\n", stderr);
        }
        return false;
    }
    bool protocol = false, have_input = false, have_output = false;
    for (std::size_t i = 1; i < args.size(); i += 2) {
        if (i + 1 >= args.size()) {
            break;
        }
        if (args[i] == "--protocol-version" && !protocol && args[i + 1] == "1") {
            protocol = true;
        } else if (args[i] == "--input" && !have_input && !args[i + 1].empty()) {
            input = fs::u8path(args[i + 1]);
            have_input = true;
        } else if (args[i] == "--output" && !have_output && !args[i + 1].empty()) {
            output = fs::u8path(args[i + 1]);
            have_output = true;
        } else {
            s.fail(2, "invalid_arguments", "Expected protocol version 1 and one input/output path.");
            return false;
        }
    }
    if (args.size() != 7 || !protocol || !have_input || !have_output) {
        s.fail(2, "invalid_arguments", "Expected protocol version 1 and one input/output path.");
        return false;
    }
    return true;
}

int run(const std::vector<std::string>& args) {
    Status s;
    fs::path input, destination;
    if (!parse(args, input, destination, s)) {
        return emit(s);
    }
    // Keep a dedicated status handle, then suppress both third-party output streams.
#ifdef _WIN32
    const int status_fd = _dup(_fileno(stdout));
    FILE* status_stream = status_fd < 0 ? nullptr : _fdopen(status_fd, "wb");
#else
    const int status_fd = dup(fileno(stdout));
    FILE* status_stream = status_fd < 0 ? nullptr : fdopen(status_fd, "w");
#endif
    if (!status_stream) {
        return 71;
    }
    std::unique_ptr<FILE, decltype(&fclose)> status_owner(status_stream, fclose);
    constexpr const char* null_device =
#ifdef _WIN32
        "NUL";
#else
        "/dev/null";
#endif
    if (!freopen(null_device, "w", stderr) || !freopen(null_device, "w", stdout)) {
        s.fail(70, "internal", "Cannot isolate backend diagnostics.", errno, "errno");
        return emit(s, status_stream);
    }
    std::error_code ec;
    destination = fs::absolute(destination, ec);
    if (ec || destination.filename().empty()) {
        s.fail(21, "output_io", "Cannot resolve destination path.", ec.value(), "filesystem");
        return emit(s, status_stream);
    }
    renderer::Output output(destination);
    try {
        convert(input, output, s);
    } catch (const std::bad_alloc&) {
        s.fail(70, "internal", "Worker allocation failed.");
    } catch (const fs::filesystem_error& e) {
        s.fail(70, "internal", "Worker filesystem operation failed.", e.code().value(), "filesystem");
    }
    renderer::complete_job(output, s);
    return emit(s, status_stream);
}

void log_handler(const gchar*, GLogLevelFlags level, const gchar*, gpointer) {
    if (level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)) {
        backend_warning.store(true);
    }
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (length == 0) {
            Status s;
            s.fail(2, "invalid_arguments", "Arguments must contain valid Unicode.");
            return emit(s);
        }
        std::string value(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), length, nullptr, nullptr);
        value.pop_back();
        args.push_back(std::move(value));
    }
#else
int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        if (!g_utf8_validate(argv[i], -1, nullptr)) {
            Status s;
            s.fail(2, "invalid_arguments", "Arguments must contain valid UTF-8.");
            return emit(s);
        }
        args.emplace_back(argv[i]);
    }
#endif
    g_log_set_default_handler(log_handler, nullptr);
    return run(args);
}
