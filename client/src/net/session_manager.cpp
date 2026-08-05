// =============================================================================
// SessionManager — see session_manager.h.
// =============================================================================
#include "net/session_manager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

namespace vms {

namespace {
// Django access tokens live 30 minutes; refresh 25 minutes early so the
// access token is always valid on the wire.
constexpr int kRefreshIntervalMs = 25 * 60 * 1000;
const QString kServerUrlKey = QStringLiteral("server/apiBaseUrl");
const QString kRefreshTokenKey = QStringLiteral("session/refreshToken");
const QString kUsernameKey = QStringLiteral("session/username");
}  // namespace

SessionManager::SessionManager(QObject* parent) : QObject(parent) {
    load();
    connect(&refresh_timer_, &QTimer::timeout, this,
            &SessionManager::startRefresh);
}

void SessionManager::setServerUrl(const QString& url) {
    QString trimmed = url.trimmed();
    while (trimmed.endsWith(QChar('/'))) trimmed.chop(1);
    if (trimmed.isEmpty()) {
        setErrorFa(QStringLiteral("آدرس سرور را وارد کنید."));
        return;
    }
    if (trimmed == api_base_url_) return;
    api_base_url_ = trimmed;
    persist();
    emit apiBaseUrlChanged();
}

void SessionManager::login(const QString& username, const QString& password) {
    if (busy_) return;
    if (username.trimmed().isEmpty()) {
        setErrorFa(QStringLiteral("نام کاربری را وارد کنید."));
        return;
    }
    if (password.isEmpty()) {
        setErrorFa(QStringLiteral("رمز عبور را وارد کنید."));
        return;
    }
    busy_ = true;
    error_fa_.clear();
    emit busyChanged();
    emit errorFaChanged();
    username_ = username;

    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    QNetworkRequest request{QUrl(api_base_url_ +
                                 QStringLiteral("/api/auth/token/"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = nam_.post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onLoginReply(reply); });
}

void SessionManager::logout() {
    access_token_.clear();
    refresh_token_.clear();
    connected_ = false;
    error_fa_.clear();
    refresh_timer_.stop();
    persist();
    emit connectedChanged();
    emit errorFaChanged();
}

void SessionManager::restore() {
    // A persisted refresh token means the user was signed in last time; try
    // to resume without asking for credentials again.
    if (refresh_token_.isEmpty()) return;
    startRefresh();
}

void SessionManager::onLoginReply(QNetworkReply* reply) {
    reply->deleteLater();
    const QJsonObject obj =
        QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() == QNetworkReply::NoError) {
        const QString access = obj.value(QStringLiteral("access")).toString();
        const QString refresh =
            obj.value(QStringLiteral("refresh")).toString();
        if (access.isEmpty() || refresh.isEmpty()) {
            setErrorFa(QStringLiteral("پاسخ سرور نامعتبر بود."));
            return;
        }
        applySession(access, refresh, username_);
        return;
    }

    // 400/401 → wrong credentials; network failure → server unreachable.
    const QString detail =
        obj.value(QStringLiteral("detail")).toString();
    if (reply->error() == QNetworkReply::ConnectionRefusedError ||
        reply->error() == QNetworkReply::HostNotFoundError ||
        reply->error() == QNetworkReply::TimeoutError ||
        reply->error() == QNetworkReply::RemoteHostClosedError) {
        setErrorFa(QStringLiteral("سرور در دسترس نیست — آدرس را بررسی کنید."));
    } else if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
        setErrorFa(QStringLiteral("نام کاربری یا رمز عبور اشتباه است."));
    } else if (!detail.isEmpty()) {
        setErrorFa(detail);
    } else {
        setErrorFa(QStringLiteral("ورود ناموفق بود (خطای %1).")
                       .arg(reply->error()));
    }
}

void SessionManager::onRefreshReply(QNetworkReply* reply) {
    reply->deleteLater();
    const QJsonObject obj =
        QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() == QNetworkReply::NoError) {
        const QString access = obj.value(QStringLiteral("access")).toString();
        const QString refresh =
            obj.value(QStringLiteral("refresh")).toString();
        // ROTATE_REFRESH_TOKENS is on, so a new refresh token comes back too.
        if (!access.isEmpty()) {
            applySession(access, refresh.isEmpty() ? refresh_token_ : refresh,
                         username_);
        }
        return;
    }

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401) {
        // Refresh token revoked/expired — force a manual login.
        logout();
    }
    // Transient network failure: keep the current session, retry next tick.
}

void SessionManager::applySession(const QString& access, const QString& refresh,
                                  const QString& user) {
    access_token_ = access;
    if (!refresh.isEmpty()) refresh_token_ = refresh;
    username_ = user;
    connected_ = true;
    busy_ = false;
    error_fa_.clear();
    persist();
    refresh_timer_.start(kRefreshIntervalMs);
    emit connectedChanged();
    emit busyChanged();
    emit errorFaChanged();
    emit usernameChanged();
    emit sessionRestored();
}

void SessionManager::startRefresh() {
    if (refresh_token_.isEmpty()) return;
    QJsonObject body;
    body.insert(QStringLiteral("refresh"), refresh_token_);
    QNetworkRequest request{QUrl(api_base_url_ +
                                 QStringLiteral("/api/auth/token/refresh/"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = nam_.post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onRefreshReply(reply); });
}

void SessionManager::setErrorFa(const QString& message) {
    if (error_fa_ == message) return;
    error_fa_ = message;
    busy_ = false;
    emit errorFaChanged();
    emit busyChanged();
}

void SessionManager::persist() {
    QSettings settings;
    settings.setValue(kServerUrlKey, api_base_url_);
    settings.setValue(kUsernameKey, username_);
    settings.setValue(kRefreshTokenKey, refresh_token_);
}

void SessionManager::load() {
    QSettings settings;
    const QString savedUrl = settings.value(kServerUrlKey).toString();
    if (!savedUrl.isEmpty()) api_base_url_ = savedUrl;
    username_ = settings.value(kUsernameKey).toString();
    refresh_token_ = settings.value(kRefreshTokenKey).toString();
}

}  // namespace vms
