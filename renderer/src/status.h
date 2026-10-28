#pragma once

#include <cstdint>
#include <cstdio>
#include <glib.h>
#include <string>

namespace renderer {

class Output;

struct Status {
    int exit_code = 0;
    std::string result = "ok";
    std::string stage = "arguments";
    std::string code;
    std::string message;
    std::string domain;
    int native_code = 0;
    unsigned int document = 0;
    unsigned int page = 0;
    unsigned int documents = 0;
    unsigned int pages_completed = 0;
    std::uint64_t output_bytes = 0;
    bool output_published = false;
    bool cleanup_complete = true;

    void fail(int value, const char* category, const char* text, int native = 0,
              const char* native_domain = nullptr);
    void gfail(int value, const char* category, const char* text, const GError* error);
};

int emit(const Status& status, FILE* stream = stdout);
void complete_job(Output& output, Status& status);

}
