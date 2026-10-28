#include "status.h"
#include "output.h"

namespace renderer {

void Status::fail(int value, const char* category, const char* text, int native,
                  const char* native_domain) {
    exit_code = value;
    result = "error";
    code = category;
    message = text;
    native_code = native;
    domain = native_domain ? native_domain : "";
}

void Status::gfail(int value, const char* category, const char* text, const GError* error) {
    fail(value, category, text, error ? error->code : 0,
         error ? g_quark_to_string(error->domain) : nullptr);
}

namespace {
std::string quote(const std::string& value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + '"';
}
}

int emit(const Status& s, FILE* stream) {
    const auto error = s.exit_code == 0 ? "null" :
        "{\"code\":" + quote(s.code) + ",\"message\":" + quote(s.message) +
        ",\"domain\":" + (s.domain.empty() ? "null" : quote(s.domain)) +
        ",\"native_code\":" + std::to_string(s.native_code) + "}";
    const auto json = "{\"schema\":\"4d-pdf-printer.worker-status\",\"version\":1,"
        "\"worker_version\":\"0.1.0\",\"result\":" + quote(s.result) +
        ",\"exit_code\":" + std::to_string(s.exit_code) +
        ",\"stage\":" + quote(s.stage) + ",\"error\":" + error +
        ",\"document\":" + (s.document ? std::to_string(s.document) : "null") +
        ",\"page\":" + (s.page ? std::to_string(s.page) : "null") +
        ",\"documents\":" + std::to_string(s.documents) +
        ",\"pages_completed\":" + std::to_string(s.pages_completed) +
        ",\"output_bytes\":" + std::to_string(s.output_bytes) +
        ",\"output_published\":" + (s.output_published ? "true" : "false") +
        ",\"cleanup_complete\":" + (s.cleanup_complete ? "true" : "false") + "}\n";
    if (fwrite(json.data(), 1, json.size(), stream) != json.size() || fflush(stream) != 0) {
        return 71;
    }
    return s.exit_code;
}

void complete_job(Output& output, Status& s) {
    s.output_bytes = output.bytes();
    s.output_published = output.published();
    s.cleanup_complete = output.cleanup();
    if (!s.cleanup_complete) {
        s.stage = "cleanup";
        s.fail(24, "cleanup", "Job-local temporary output could not be fully removed.",
               output.native_error(), "filesystem");
    }
}

}
