#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "version.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <objbase.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <map>
#include <array>
#include <cctype>
#include <stdexcept>
#include <algorithm>
#include <charconv>
#include "preview_images.hpp"
#include "nexus.hpp"

namespace drifter::preview_detail {
namespace {
// GUID_ContainerFormatWebp and WINCODEC_ERR_COMPONENTNOTFOUND are defined here
// by value so the build does not depend on a particular SDK header revision or
// on libuuid exporting the newer WebP constant.
const GUID WebpContainerFormat = {0xe094b0e2, 0x67f2, 0x45b3, {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3}};
constexpr HRESULT WincodecComponentNotFound = static_cast<HRESULT>(0x88982F50L);
void checked(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Preview codec failed.");
}
template<class T> struct Com {
    T* value = nullptr;
    ~Com() {
        if (value) value->Release();
    }
    T** put() { return &value; }
    T* operator->() const { return value; }
};
struct Internet {
    HINTERNET value;
    explicit Internet(HINTERNET h) : value(h) {
        if (!h) throw std::runtime_error("Preview connection failed.");
    }
    ~Internet() {
        if (value) WinHttpCloseHandle(value);
    }
    HINTERNET get() const { return value; }
};
struct AsyncState {
    std::mutex mutex;
    std::condition_variable condition;
    DWORD event = 0, count = 0;
    bool failed = false;
    std::array<char, 65536> buffer{};
};
struct Registry {
    std::mutex mutex;
    std::map<DWORD_PTR, std::shared_ptr<AsyncState>> states;
    std::atomic<DWORD_PTR> next{1};
};
// The registry outlives WinHTTP callback teardown. Each request entry is removed
// on HANDLE_CLOSING; callbacks take their own shared reference before lookup
// locks are released, so cancellation never leaves a dangling context/buffer.
Registry& registry() {
    static auto* value = new Registry;
    return *value;
}
void CALLBACK callback(HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) noexcept {
    try {
        std::shared_ptr<AsyncState> state;
        {
            auto& all = registry();
            std::lock_guard<std::mutex> lock(all.mutex);
            auto found = all.states.find(context);
            if (found == all.states.end()) return;
            state = found->second;
            if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) all.states.erase(found);
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR || status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE ||
            status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
            state->failed = true;
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state->event = status;
            state->count = length;
        }
        state->condition.notify_all();
    } catch (...) {}
}
struct Async {
    Internet request;
    std::shared_ptr<AsyncState> state = std::make_shared<AsyncState>();
    DWORD_PTR context = registry().next.fetch_add(1);
    Deadline deadline;
    Async(HINTERNET handle, Deadline limit) : request(handle), deadline(limit) {
        if (!context) throw std::runtime_error("Preview request identity failed.");
        auto& all = registry();
        {
            std::lock_guard<std::mutex> lock(all.mutex);
            all.states.emplace(context, state);
        }
        if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)) ||
            WinHttpSetStatusCallback(request.get(), callback,
                                     WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES |
                                         WINHTTP_CALLBACK_FLAG_SECURE_FAILURE,
                                     0) == WINHTTP_INVALID_STATUS_CALLBACK) {
            std::lock_guard<std::mutex> lock(all.mutex);
            all.states.erase(context);
            throw std::runtime_error("Preview asynchronous setup failed.");
        }
    }
    void begin() {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Preview timeout.");
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->failed) throw std::runtime_error("Preview request stopped.");
        state->event = 0;
        state->count = 0;
    }
    void started(BOOL result) {
        if (!result && GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("Preview request failed.");
    }
    DWORD wait(DWORD event) {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->condition.wait_until(lock, deadline, [&] { return state->failed || state->event == event; }) ||
            state->failed)
            throw std::runtime_error("Preview request timed out or failed.");
        return state->count;
    }
};
std::wstring wide(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}
std::string ascii(const wchar_t* value) {
    std::string out;
    for (; *value; ++value) {
        if (*value > 127 || *value < 32) throw std::runtime_error("Preview response header failed.");
        out.push_back(static_cast<char>(*value));
    }
    return out;
}
}
namespace {
std::string imageFetch(const std::string& address, std::int64_t modId, Deadline deadline, bool avatar) {
    if constexpr (LocalEdition) throw std::runtime_error(LocalEditionMessage);
    deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(8));
    auto current = avatar ? approvedAvatarUrl(address, modId) : approvedUrl(address, modId);
    Internet session(WinHttpOpen(L"DriftersToolkit-Previews/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000))
        throw std::runtime_error("Preview timeouts failed.");
    Internet connection(
        WinHttpConnect(session.get(), avatar ? L"avatars.nexusmods.com" : L"staticdelivery.nexusmods.com", 443, 0));
    for (unsigned redirects = 0; redirects <= 2; ++redirects) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Preview timeout.");
        auto path = current.substr(current.find('/', 8));
        auto target = wide(path);
        Async request(WinHttpOpenRequest(connection.get(), L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
                      deadline);
        auto handle = request.request.get();
        DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        if (!WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)))
            throw std::runtime_error("Preview credential isolation failed.");
        request.begin();
        request.started(
            WinHttpSendRequest(handle,
                               avatar ? L"Accept: image/png, image/jpeg, image/webp\r\nAccept-Encoding: "
                                        L"identity\r\nCache-Control: no-cache\r\n"
                                      : L"Accept: image/png, image/jpeg, image/webp\r\nAccept-Encoding: identity\r\n",
                               (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, request.context));
        request.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
        request.begin();
        request.started(WinHttpReceiveResponse(handle, nullptr));
        request.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size,
                                 nullptr))
            throw std::runtime_error("Preview response failed.");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            wchar_t location[2049]{};
            size = sizeof(location);
            if (redirects == 2 ||
                !WinHttpQueryHeaders(handle, WINHTTP_QUERY_LOCATION, nullptr, location, &size, nullptr))
                throw std::runtime_error("Preview redirect failed.");
            current = avatar ? redirectAvatarUrl(current, ascii(location), modId)
                             : redirectUrl(current, ascii(location), modId);
            continue;
        }
        if (status != 200) throw std::runtime_error("Preview response failed.");
        wchar_t type[128]{};
        size = sizeof(type);
        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_TYPE, nullptr, type, &size, nullptr))
            throw std::runtime_error("Preview type missing.");
        auto mime = ascii(type);
        mime = mime.substr(0, mime.find(';'));
        std::transform(mime.begin(), mime.end(), mime.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        // The avatar CDN labels its WebP files application/octet-stream. The
        // declared type is only a coarse filter; inspectRaster() decides the format.
        if (mime != "image/png" && mime != "image/jpeg" && mime != "image/webp" && mime != "application/octet-stream")
            throw std::runtime_error("Preview type refused.");
        std::int64_t expected = -1;
        wchar_t length[32]{};
        size = sizeof(length);
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, length, &size, nullptr)) {
            auto text = ascii(length);
            auto result = std::from_chars(text.data(), text.data() + text.size(), expected);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || expected < 0 ||
                (std::uint64_t)expected > MaxTransfer)
                throw std::runtime_error("Preview length refused.");
        }
        std::string body;
        for (;;) {
            request.begin();
            request.started(
                WinHttpReadData(handle, request.state->buffer.data(), (DWORD)request.state->buffer.size(), nullptr));
            auto count = request.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
            if (!count) break;
            if (count > request.state->buffer.size() || body.size() + count > MaxTransfer ||
                std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("Preview limit.");
            body.append(request.state->buffer.data(), count);
        }
        if (expected >= 0 && (std::uint64_t)expected != body.size()) throw std::runtime_error("Preview incomplete.");
        return body;
    }
    throw std::runtime_error("Preview redirect limit.");
}
}
std::string fetch(const std::string& address, std::int64_t modId, Deadline deadline) {
    return imageFetch(address, modId, deadline, false);
}
std::string fetchAvatar(const std::string& address, std::int64_t authorId, Deadline deadline) {
    return imageFetch(address, authorId, deadline, true);
}
std::string fetchAuthorProfile(Deadline deadline) {
    if constexpr (LocalEdition) throw std::runtime_error(LocalEditionMessage);
    deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(8));
    Internet session(WinHttpOpen(L"DriftersToolkit-AuthorImage/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!WinHttpSetTimeouts(session.get(), 2000, 2000, 2000, 2000))
        throw std::runtime_error("Author profile timeouts failed.");
    Internet connection(WinHttpConnect(session.get(), L"api.nexusmods.com", 443, 0));
    Async request(WinHttpOpenRequest(connection.get(), L"POST", L"/v2/graphql", nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE),
                  deadline);
    auto handle = request.request.get();
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)))
        throw std::runtime_error("Author profile credential isolation failed.");
    auto payload = authorQuery();
    request.begin();
    request.started(WinHttpSendRequest(
        handle,
        L"Accept: application/json\r\nContent-Type: application/json\r\nAccept-Encoding: identity\r\nCache-Control: "
        L"no-cache\r\nApplication-Name: DriftersToolkit\r\nApplication-Version: " DRIFTER_VERSION "\r\n",
        (DWORD)-1, payload.data(), static_cast<DWORD>(payload.size()), static_cast<DWORD>(payload.size()),
        request.context));
    request.wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE);
    request.begin();
    request.started(WinHttpReceiveResponse(handle, nullptr));
    request.wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE);
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size,
                             nullptr) ||
        status != 200)
        throw std::runtime_error("Public author profile response failed.");
    wchar_t type[128]{};
    size = sizeof(type);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_TYPE, nullptr, type, &size, nullptr))
        throw std::runtime_error("Author profile response type missing.");
    auto mime = ascii(type);
    mime = mime.substr(0, mime.find(';'));
    std::transform(mime.begin(), mime.end(), mime.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (mime != "application/json" && mime != "application/graphql-response+json")
        throw std::runtime_error("Author profile response type refused.");
    std::int64_t expected = -1;
    wchar_t length[32]{};
    size = sizeof(length);
    if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, length, &size, nullptr)) {
        auto text = ascii(length);
        auto result = std::from_chars(text.data(), text.data() + text.size(), expected);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || expected < 0 ||
            (std::uint64_t)expected > MaxAuthorProfile)
            throw std::runtime_error("Author profile response length refused.");
    }
    std::string body;
    for (;;) {
        request.begin();
        request.started(
            WinHttpReadData(handle, request.state->buffer.data(), (DWORD)request.state->buffer.size(), nullptr));
        auto count = request.wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE);
        if (!count) break;
        if (count > request.state->buffer.size() || body.size() + count > MaxAuthorProfile ||
            std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Author profile response limit.");
        body.append(request.state->buffer.data(), count);
    }
    if (expected >= 0 && (std::uint64_t)expected != body.size())
        throw std::runtime_error("Author profile response incomplete.");
    return body;
}
static Image decodeSized(const std::string& bytes, const Raster& raster, unsigned edge) {
    auto inspected = inspectRaster(bytes);
    if (inspected.kind != raster.kind || inspected.width != raster.width || inspected.height != raster.height)
        throw std::runtime_error("Preview header changed.");
    auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) checked(initialized);
    struct Uninitialize {
        bool active;
        ~Uninitialize() {
            if (active) CoUninitialize();
        }
    } uninitialize{SUCCEEDED(initialized)};
    auto safe = pixelBytes(bytes);
    Com<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
    Com<IWICStream> source;
    checked(factory->CreateStream(source.put()));
    checked(source->InitializeFromMemory(reinterpret_cast<BYTE*>(safe.data()), (DWORD)safe.size()));
    Com<IWICBitmapDecoder> decoder;
    const auto created =
        factory->CreateDecoderFromStream(source.value, nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
    // Windows ships WebP decoding through the optional WebP Image Extension. Its
    // absence is a codec gap, not a corrupt image, so report it distinctly.
    if (created == WincodecComponentNotFound)
        throw CodecUnavailable("Windows has no codec installed for this image container.");
    checked(created);
    GUID format{};
    checked(decoder->GetContainerFormat(&format));
    const GUID& expectedContainer = raster.kind == Raster::Png    ? GUID_ContainerFormatPng
                                    : raster.kind == Raster::Jpeg ? GUID_ContainerFormatJpeg
                                                                  : WebpContainerFormat;
    if (!IsEqualGUID(format, expectedContainer)) throw std::runtime_error("Preview codec type differs.");
    Com<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0, frame.put()));
    UINT width = 0, height = 0;
    checked(frame->GetSize(&width, &height));
    if (width != raster.width || height != raster.height) throw std::runtime_error("Preview codec dimensions differ.");
    UINT outputWidth = width, outputHeight = height;
    if (width > edge || height > edge) {
        if (width >= height) {
            outputWidth = edge;
            outputHeight = std::max<UINT>(1, (std::uint64_t)height * edge / width);
        } else {
            outputHeight = edge;
            outputWidth = std::max<UINT>(1, (std::uint64_t)width * edge / height);
        }
    }
    Com<IWICBitmapScaler> scaler;
    checked(factory->CreateBitmapScaler(scaler.put()));
    checked(scaler->Initialize(frame.value, outputWidth, outputHeight, WICBitmapInterpolationModeFant));
    Com<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(converter.put()));
    checked(converter->Initialize(scaler.value, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom));
    Com<IStream> output;
    checked(CreateStreamOnHGlobal(nullptr, TRUE, output.put()));
    Com<IWICBitmapEncoder> encoder;
    checked(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    checked(encoder->Initialize(output.value, WICBitmapEncoderNoCache));
    Com<IWICBitmapFrameEncode> encoded;
    Com<IPropertyBag2> properties;
    checked(encoder->CreateNewFrame(encoded.put(), properties.put()));
    checked(encoded->Initialize(properties.value));
    checked(encoded->SetSize(outputWidth, outputHeight));
    WICPixelFormatGUID pixel = GUID_WICPixelFormat32bppBGRA;
    checked(encoded->SetPixelFormat(&pixel));
    if (!IsEqualGUID(pixel, GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("Preview pixel format failed.");
    checked(encoded->WriteSource(converter.value, nullptr));
    checked(encoded->Commit());
    checked(encoder->Commit());
    STATSTG stats{};
    checked(output->Stat(&stats, STATFLAG_NONAME));
    if (stats.cbSize.QuadPart > MaxPng) throw std::runtime_error("Preview encoding limit.");
    HGLOBAL memory = nullptr;
    checked(GetHGlobalFromStream(output.value, &memory));
    auto data = GlobalLock(memory);
    if (!data) throw std::runtime_error("Preview output unavailable.");
    std::string png(static_cast<const char*>(data), (std::size_t)stats.cbSize.QuadPart);
    GlobalUnlock(memory);
    return {std::move(png), outputWidth, outputHeight};
}
Image decode(const std::string& bytes, const Raster& raster) {
    return decodeSized(bytes, raster, 1024);
}
Image decodeThumbnail(const std::string& bytes, const Raster& raster) {
    if (raster.kind != Raster::Png && raster.kind != Raster::Jpeg)
        throw std::runtime_error("Choose a PNG or JPEG picture for this backup.");
    return decodeSized(bytes, raster, 512);
}
}
