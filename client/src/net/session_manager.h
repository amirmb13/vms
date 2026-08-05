#pragma once
// =============================================================================
// SessionManager — backend connection + JWT session for the desktop client.
//
//   POST {base}/api/auth/token/         username+password -> {access, refresh}
//   POST {base}/api/auth/token/refresh/ {refresh}         -> rotating pair
//
// Responsibilities:
//   - Holds the server URL (persisted in QSettings, editable from the UI).
//   - Logs in with username/password and exposes the access token to the
//     camera tree / grid models.
//   - Auto-refreshes the access token every kRefreshIntervalMs (Django's
//     access lifetime is 30 minutes) using the rotating refresh token.
//   - Restores the session at startup via the persisted refresh token, so a
//     user who ticked "keep me signed in" is not forced to log in again.
// =============================================================================
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkReply;

namespace vms {

class SessionManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl NOTIFY apiBaseUrlChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorFa READ errorFa NOTIFY errorFaChanged)
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)

public:
    explicit SessionManager(QObject* parent = nullptr);

    // --- QML API --------------------------------------------------------------
    Q_INVOKABLE void setServerUrl(const QString& url);
    Q_INVOKABLE void login(const QString& username, const QString& password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void restore();  // refresh-token relogin at startup

    QString apiBaseUrl() const { return api_base_url_; }
    bool connected() const { return connected_; }
    bool busy() const { return busy_; }
    QString errorFa() const { return error_fa_; }
    QString username() const { return username_; }
    QString authToken() const { return access_token_; }

signals:
    void apiBaseUrlChanged();
    void connectedChanged();
    void busyChanged();
    void errorFaChanged();
    void usernameChanged();
    // Emitted whenever a fresh access token is available (login or refresh);
    // main.cpp re-arms the camera tree / grid models with it.
    void sessionRestored();

private slots:
    void onLoginReply(QNetworkReply* reply);
    void onRefreshReply(QNetworkReply* reply);

private:
    void startRefresh();
    void applySession(const QString& access, const QString& refresh,
                      const QString& user);
    void setErrorFa(const QString& message);
    void persist();
    void load();

    QNetworkAccessManager nam_;
    QTimer refresh_timer_;
    QString api_base_url_ = QStringLiteral("http://127.0.0.1:8000");
    QString username_;
    QString access_token_;
    QString refresh_token_;
    bool connected_ = false;
    bool busy_ = false;
    QString error_fa_;
};

}  // namespace vms
