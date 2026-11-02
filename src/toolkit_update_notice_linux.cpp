#include "toolkit_update_notice.hpp"
#include "version.hpp"
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <charconv>
#include <memory>
#include <stdexcept>

namespace drifter::toolkit_update_notice {
std::string fetch(Deadline deadline) {
    if (!defaultEnabled()) throw std::runtime_error("Toolkit release checks await Nexus registration.");
    const auto fail = [] { throw std::runtime_error("The public toolkit release check could not be completed."); };
    deadline = std::min(deadline, std::chrono::steady_clock::now() + RequestTimeout);
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) fail();
    const auto payload = query();
    if (payload.empty() || payload.size() > 8192) fail();
    QNetworkAccessManager manager;
    // Endpoint is a compiled constant. Redirects and authentication never
    // replace it or attach a user's Nexus credentials to this public request.
    QNetworkRequest request{QUrl(QString::fromLatin1(Endpoint), QUrl::StrictMode)};
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
    request.setRawHeader("User-Agent", "DriftersToolkit-ReleaseCheck/" DRIFTER_VERSION);
    request.setRawHeader("Application-Name", "DriftersToolkit");
    request.setRawHeader("Application-Version", DRIFTER_VERSION);
    std::unique_ptr<QNetworkReply> reply(manager.post(request, QByteArray(payload.data(), static_cast<int>(payload.size()))));
    reply->setReadBufferSize(16384);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray body;
    bool failed = false;
    std::int64_t expected = -1;
    auto abort = [&] {
        failed = true;
        reply->abort();
        loop.quit();
    };
    auto headers = [&] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!status.isValid()) return;
        const auto type = reply->rawHeader("Content-Type").split(';').front().trimmed().toLower();
        const auto encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
        if (status.toInt() != 200 || (type != "application/json" && type != "application/graphql-response+json") ||
            (!encoding.isEmpty() && encoding != "identity")) {
            abort();
            return;
        }
        if (reply->hasRawHeader("Content-Length")) {
            const auto length = reply->rawHeader("Content-Length");
            auto parsed = std::from_chars(length.data(), length.data() + length.size(), expected);
            if (length.isEmpty() || parsed.ec != std::errc{} || parsed.ptr != length.data() + length.size() ||
                expected < 0 || static_cast<std::uint64_t>(expected) > MaxResponse)
                abort();
        }
    };
    auto drain = [&] {
        while (!failed && reply->bytesAvailable() > 0) {
            if (std::chrono::steady_clock::now() >= deadline) { abort(); return; }
            const auto chunk = reply->read(std::min<qint64>(16384, reply->bytesAvailable()));
            if (static_cast<std::size_t>(chunk.size()) > MaxResponse - static_cast<std::size_t>(body.size())) {
                abort();
                return;
            }
            body.append(chunk);
        }
    };
    QObject::connect(reply.get(), &QNetworkReply::metaDataChanged, &loop, headers);
    QObject::connect(reply.get(), &QIODevice::readyRead, &loop, drain);
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply.get(), &QNetworkReply::sslErrors, &loop, [&](const QList<QSslError>&) { abort(); });
    QObject::connect(&manager, &QNetworkAccessManager::authenticationRequired, &loop,
                     [&](QNetworkReply*, QAuthenticator*) { abort(); });
    QObject::connect(&manager, &QNetworkAccessManager::proxyAuthenticationRequired, &loop,
                     [&](const QNetworkProxy&, QAuthenticator*) { abort(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, abort);
    timer.start(static_cast<int>(remaining));
    if (!reply->isFinished()) loop.exec();
    timer.stop();
    headers();
    drain();
    if (failed || std::chrono::steady_clock::now() >= deadline || reply->error() != QNetworkReply::NoError ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
        (expected >= 0 && expected != body.size()))
        fail();
    return std::string(body.constData(), static_cast<std::size_t>(body.size()));
}
}
