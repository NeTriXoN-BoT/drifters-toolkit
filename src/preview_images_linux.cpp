#include "version.hpp"
#include "preview_images.hpp"
#include "nexus.hpp"
#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <stdexcept>

namespace drifter::preview_detail {
namespace {
std::string imageFetch(const std::string& address, std::int64_t modId, Deadline deadline, bool avatar) {
    if constexpr (LocalEdition) throw std::runtime_error(LocalEditionMessage);
    deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(8));
    auto current = avatar ? approvedAvatarUrl(address, modId) : approvedUrl(address, modId);
    QNetworkAccessManager manager;
    for (unsigned redirects = 0; redirects <= 2; ++redirects) {
        auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) throw std::runtime_error("Preview timeout.");
        QNetworkRequest request{QUrl(QString::fromStdString(current), QUrl::StrictMode)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setRawHeader("Accept", "image/png, image/jpeg, image/webp");
        request.setRawHeader("Accept-Encoding", "identity");
        request.setRawHeader("User-Agent", "DriftersToolkit-Previews/0.1");
        if (avatar) request.setRawHeader("Cache-Control", "no-cache");
        auto reply = manager.get(request);
        reply->setReadBufferSize(65536);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QByteArray body;
        bool failed = false;
        auto drain = [&] {
            while (reply->bytesAvailable() > 0) {
                auto chunk = reply->read(std::min<qint64>(65536, reply->bytesAvailable()));
                if (body.size() + chunk.size() > (qint64)MaxTransfer) {
                    failed = true;
                    reply->abort();
                    loop.quit();
                    return;
                }
                body.append(chunk);
            }
        };
        QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::sslErrors, &loop, [&](const QList<QSslError>&) {
            failed = true;
            reply->abort();
            loop.quit();
        });
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
            failed = true;
            reply->abort();
            loop.quit();
        });
        timer.start(static_cast<int>(std::min<std::int64_t>(left, 8000)));
        if (!reply->isFinished()) loop.exec();
        timer.stop();
        drain();
        auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto location = reply->rawHeader("Location");
        auto error = reply->error();
        auto contentType = reply->rawHeader("Content-Type").split(';').front().trimmed().toLower();
        auto length = reply->header(QNetworkRequest::ContentLengthHeader);
        bool lengthOk = true;
        auto expected = length.isValid() ? length.toLongLong(&lengthOk) : -1;
        delete reply;
        if (failed || std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Preview transfer failed.");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (redirects == 2) throw std::runtime_error("Preview redirect limit.");
            auto next = std::string(location.constData(), (std::size_t)location.size());
            current = avatar ? redirectAvatarUrl(current, next, modId) : redirectUrl(current, next, modId);
            continue;
        }
        // The avatar CDN labels its WebP files application/octet-stream. The
        // declared type is only a coarse filter; inspectRaster() decides the format.
        const bool imageType = contentType == "image/png" || contentType == "image/jpeg" ||
                               contentType == "image/webp" || contentType == "application/octet-stream";
        if (status != 200 || error != QNetworkReply::NoError || !imageType || !lengthOk ||
            (length.isValid() && expected < 0) || (expected >= 0 && expected != body.size()))
            throw std::runtime_error("Preview response failed.");
        return std::string(body.constData(), (std::size_t)body.size());
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
    auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) throw std::runtime_error("Author profile timeout.");
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QStringLiteral("https://api.nexusmods.com/v2/graphql"))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("User-Agent", "DriftersToolkit-AuthorImage/0.1");
    request.setRawHeader("Application-Name", "DriftersToolkit");
    request.setRawHeader("Application-Version", DRIFTER_VERSION);
    auto payload = authorQuery();
    auto reply = manager.post(request, QByteArray(payload.data(), static_cast<int>(payload.size())));
    reply->setReadBufferSize(65536);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray body;
    bool failed = false;
    auto drain = [&] {
        while (reply->bytesAvailable() > 0) {
            auto chunk = reply->read(std::min<qint64>(65536, reply->bytesAvailable()));
            if (body.size() + chunk.size() > (qint64)MaxAuthorProfile) {
                failed = true;
                reply->abort();
                loop.quit();
                return;
            }
            body.append(chunk);
        }
    };
    QObject::connect(reply, &QIODevice::readyRead, &loop, drain);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::sslErrors, &loop, [&](const QList<QSslError>&) {
        failed = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        failed = true;
        reply->abort();
        loop.quit();
    });
    timer.start(static_cast<int>(std::min<std::int64_t>(left, 8000)));
    if (!reply->isFinished()) loop.exec();
    timer.stop();
    drain();
    auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto error = reply->error();
    auto type = reply->rawHeader("Content-Type").split(';').front().trimmed().toLower();
    auto length = reply->header(QNetworkRequest::ContentLengthHeader);
    bool lengthOk = true;
    auto expected = length.isValid() ? length.toLongLong(&lengthOk) : -1;
    delete reply;
    if (failed || std::chrono::steady_clock::now() > deadline || status != 200 || error != QNetworkReply::NoError ||
        (type != "application/json" && type != "application/graphql-response+json") || !lengthOk ||
        (length.isValid() && expected < 0) || (expected >= 0 && expected != body.size()))
        throw std::runtime_error("Public author profile response failed.");
    return std::string(body.constData(), (std::size_t)body.size());
}

static Image decodeSized(const std::string& bytes, const Raster& raster, unsigned edge) {
    // The format is forced after our own bounded container/dimension validation;
    // this never dispatches to SVG or another image plugin based on user input.
    auto inspected = inspectRaster(bytes);
    if (inspected.kind != raster.kind || inspected.width != raster.width || inspected.height != raster.height)
        throw std::runtime_error("Preview header changed.");
    auto safe = pixelBytes(bytes);
    QByteArray input(safe.data(), static_cast<int>(safe.size()));
    QBuffer source(&input);
    if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Preview buffer failed.");
    // Qt reads WebP through its optional imageformats plugin. Its absence is a
    // codec gap, not a corrupt image, so report it distinctly.
    const QByteArray format = raster.kind == Raster::Png    ? QByteArray("png")
                              : raster.kind == Raster::Jpeg ? QByteArray("jpeg")
                                                            : QByteArray("webp");
    if (raster.kind == Raster::WebP && !QImageReader::supportedImageFormats().contains(format))
        throw CodecUnavailable("Qt has no WebP image plugin available.");
    QImageReader reader(&source, format);
    reader.setAutoDetectImageFormat(false);
    reader.setDecideFormatFromContent(false);
    reader.setAutoTransform(false);
    if (reader.size() != QSize(static_cast<int>(raster.width), static_cast<int>(raster.height)))
        throw std::runtime_error("Preview decoder dimensions differ.");
    auto target = reader.size();
    if (raster.width > edge || raster.height > edge) {
        if (raster.width >= raster.height)
            target = QSize(edge, std::max(1, (int)((std::uint64_t)raster.height * edge / raster.width)));
        else
            target = QSize(std::max(1, (int)((std::uint64_t)raster.width * edge / raster.height)), edge);
    }
    reader.setScaledSize(target);
    auto decoded = reader.read();
    if (decoded.isNull() || decoded.width() > 4096 || decoded.height() > 4096 ||
        (std::uint64_t)decoded.width() * decoded.height() > 8 * 1024 * 1024)
        throw std::runtime_error("Preview decode failed.");
    if (decoded.size() != target) decoded = decoded.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // A fresh pixel-only image strips text, EXIF, profiles and other source
    // metadata instead of saving the QImageReader result with attached fields.
    QImage clean(decoded.width(), decoded.height(), QImage::Format_RGBA8888);
    if (clean.isNull()) throw std::runtime_error("Preview allocation failed.");
    auto pixels = decoded.convertToFormat(QImage::Format_RGBA8888);
    if (pixels.isNull()) throw std::runtime_error("Preview conversion failed.");
    for (int y = 0; y < clean.height(); ++y) std::copy_n(pixels.constScanLine(y), clean.width() * 4, clean.scanLine(y));
    QByteArray encoded;
    QBuffer destination(&encoded);
    if (!destination.open(QIODevice::WriteOnly)) throw std::runtime_error("Preview output failed.");
    QImageWriter writer(&destination, "png");
    if (!writer.write(clean) || encoded.size() > (qint64)MaxPng) throw std::runtime_error("Preview encoding failed.");
    return {std::string(encoded.constData(), (std::size_t)encoded.size()), (std::uint32_t)clean.width(),
            (std::uint32_t)clean.height()};
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
