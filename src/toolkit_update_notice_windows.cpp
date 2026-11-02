#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include "toolkit_update_notice.hpp"
#include "version.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace drifter::toolkit_update_notice {
namespace {
[[noreturn]] void failed() {
    throw std::runtime_error("The public toolkit release check could not be completed.");
}
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET handle) : value(handle) { if (!value) failed(); }
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet(const Internet&) = delete;
    HINTERNET get() const { return value; }
};
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    DWORD event = 0, count = 0;
    bool failed = false;
    std::array<char, 16384> buffer{};
    std::string payload;
};
struct Registry {
    std::mutex mutex;
    std::map<DWORD_PTR, std::shared_ptr<State>> requests;
    std::atomic<DWORD_PTR> next{1};
};
// WinHTTP can deliver HANDLE_CLOSING after the request object is destroyed.
// Keep its context and read buffer alive until that final callback arrives.
Registry& registry() {
    static auto* result = new Registry;
    return *result;
}
void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) noexcept {
    try {
        std::shared_ptr<State> state;
        {
            auto& all = registry();
            std::lock_guard lock(all.mutex);
            auto found = all.requests.find(context);
            if (found == all.requests.end()) return;
            state = found->second;
            if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) all.requests.erase(found);
        }
        std::lock_guard lock(state->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR || status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE ||
            status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            state->failed = true;
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state->event = status;
            state->count = length;
        }
        state->wake.notify_all();
    } catch (...) {}
}
struct Request {
    Internet handle;
    std::shared_ptr<State> state = std::make_shared<State>();
    const DWORD_PTR context = registry().next.fetch_add(1);
    const Deadline deadline;
    Request(HINTERNET value, Deadline until) : handle(value), deadline(until) {
        if (!context) failed();
        auto& all = registry();
        {
            std::lock_guard lock(all.mutex);
            all.requests.emplace(context, state);
        }
        auto nativeContext = context;
        if (!WinHttpSetOption(handle.get(), WINHTTP_OPTION_CONTEXT_VALUE, &nativeContext, sizeof(nativeContext)) ||
            WinHttpSetStatusCallback(handle.get(), callback,
                                     WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES |
                                         WINHTTP_CALLBACK_FLAG_SECURE_FAILURE,
                                     0) == WINHTTP_INVALID_STATUS_CALLBACK) {
            std::lock_guard lock(all.mutex);
            all.requests.erase(context);
            failed();
        }
    }
    void begin() {
        if (std::chrono::steady_clock::now() >= deadline) failed();
        std::lock_guard lock(state->mutex);
        if (state->failed) failed();
        state->event = 0;
        state->count = 0;
    }
    void started(BOOL result) { if (!result && GetLastError() != ERROR_IO_PENDING) failed(); }
    DWORD wait(DWORD event) {
        std::unique_lock lock(state->mutex);
        if (!state->wake.wait_until(lock, deadline, [&] { return state->failed || state->event == event; }) ||
            state->failed || std::chrono::steady_clock::now() >= deadline)
            failed();
        return state->count;
    }
};
std::string header(HINTERNET request, DWORD name, bool required = false) {
    wchar_t value[256]{};
    DWORD bytes = sizeof(value);
    if (!WinHttpQueryHeaders(request, name, nullptr, value, &bytes, nullptr)) {
        if (!required && GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
        failed();
    }
    std::string result;
    for (auto c : value) {
        if (!c) break;
        if (c < 32 || c > 126) failed();
        result.push_back(static_cast<char>(c));
    }
    return result;
}
std::string normalized(std::string value) {
    auto begin = value.find_first_not_of(' '), end = value.find_last_not_of(' ');
    if (begin == std::string::npos) return {};
    value = value.substr(begin, end - begin + 1);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}
}

std::string fetch(Deadline deadline) {
    // Also enforce the build gate here so no direct transport caller can make
    // a Nexus API request before registration is enabled for the release.
    if (!defaultEnabled()) throw std::runtime_error("Toolkit release checks await Nexus registration.");
    deadline = std::min(deadline, std::chrono::steady_clock::now() + RequestTimeout);
    if (std::chrono::steady_clock::now() >= deadline) failed();
    auto payload = query();
    if (payload.empty() || payload.size() > 8192) failed();
    Internet session(WinHttpOpen(L"DriftersToolkit-ReleaseCheck/" DRIFTER_VERSION,
                                 WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                 WINHTTP_FLAG_ASYNC));
    if (!WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000)) failed();
    // Host/path are native literals, independent of renderer or server input.
    Internet connection(WinHttpConnect(session.get(), L"api.nexusmods.com", 443, 0));
    Request request(WinHttpOpenRequest(connection.get(), L"POST", L"/v2/graphql", nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), deadline);
    // WinHTTP may retain the POST buffer after cancellation. The registry owns
    // it through HANDLE_CLOSING, just like the asynchronous read buffer.
    request.state->payload = std::move(payload);
    auto handle = request.handle.get();
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable))) failed();
    request.begin();
    request.started(WinHttpSendRequest(
        handle,
        L"Accept: application/json\r\nContent-Type: application/json\r\nAccept-Encoding: identity\r\n"
        L"Cache-Control: no-cache\r\nApplication-Name: DriftersToolkit\r\nApplication-Version: " DRIFTER_VERSION "\r\n",
        (DWORD)-1, request.state->payload.data(), static_cast<DWORD>(request.state->payload.size()),
        static_cast<DWORD>(request.state->payload.size()),
        request.context));
    request.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    request.begin();
    request.started(WinHttpReceiveResponse(handle, nullptr));
    request.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &bytes,
                             nullptr) || status != 200)
        failed();
    auto type = header(handle, WINHTTP_QUERY_CONTENT_TYPE, true);
    type = normalized(type.substr(0, type.find(';')));
    if (type != "application/json" && type != "application/graphql-response+json") failed();
    const auto encoding = normalized(header(handle, WINHTTP_QUERY_CONTENT_ENCODING));
    if (!encoding.empty() && encoding != "identity") failed();
    const auto length = header(handle, WINHTTP_QUERY_CONTENT_LENGTH);
    std::int64_t expected = -1;
    if (!length.empty()) {
        auto parsed = std::from_chars(length.data(), length.data() + length.size(), expected);
        if (parsed.ec != std::errc{} || parsed.ptr != length.data() + length.size() || expected < 0 ||
            static_cast<std::uint64_t>(expected) > MaxResponse)
            failed();
    }
    std::string body;
    for (;;) {
        request.begin();
        request.started(WinHttpReadData(handle, request.state->buffer.data(),
                                        static_cast<DWORD>(request.state->buffer.size()), nullptr));
        const auto count = request.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        if (!count) break;
        if (count > request.state->buffer.size() || count > MaxResponse - body.size()) failed();
        body.append(request.state->buffer.data(), count);
    }
    if (expected >= 0 && static_cast<std::uint64_t>(expected) != body.size()) failed();
    return body;
}
}
