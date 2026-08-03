#pragma once
// =============================================================================
// GridModel — C++ QAbstractListModel backing the hyper-customizable grid.
//
// Two responsibilities:
//   1. Saved-layout catalogue: GET/POST /api/layouts/ against the Django
//      Control Plane (layout_json documents are validated server-side).
//      DRF pagination is followed page-by-page; the model is swapped once
//      at the end so the GUI thread sees a single reset.
//   2. Active layout state: the currently applied layout_json, exposed to
//      GridEngine.qml as a parsed object + per-cell model roles, with
//      instantaneous apply (no re-instantiation of unaffected delegates).
//
// Layout document schema (LTR canonical; RTL mirroring happens in QML):
//   { "grid": { "cols": 4, "rows": 3 },
//     "cells": [ { "x":0,"y":0,"w":2,"h":2,"camera_uuid":"..." }, ... ] }
// =============================================================================
#include <QAbstractListModel>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

#include <vector>

class QNetworkReply;

namespace vms {

struct SavedLayout {
    QString uuid;            // server-side identity (lookup_field = "uuid")
    QString nameFa;
    QJsonObject layoutJson;
    int cellCount = 0;       // cached at parse time — data() must stay O(1)
    bool isShared = false;
};

class GridModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl WRITE setApiBaseUrl
                   NOTIFY apiBaseUrlChanged)
    Q_PROPERTY(QVariantMap activeLayout READ activeLayout
                   NOTIFY activeLayoutChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorFa READ errorFa NOTIFY errorFaChanged)

public:
    enum Roles {
        LayoutIdRole = Qt::UserRole + 1,   // server uuid (string)
        NameFaRole,
        IsSharedRole,
        CellCountRole,
    };
    Q_ENUM(Roles)

    // Server-side schema allows up to 16x16 (enterprise video walls).
    static constexpr int kMaxGridDim = 16;

    explicit GridModel(QObject* parent = nullptr);

    // --- QAbstractListModel (saved layout catalogue) -------------------------
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- QML API --------------------------------------------------------------
    Q_INVOKABLE void reload();                     // fetch saved layouts
    Q_INVOKABLE void applyLayoutAt(int row);       // activate a saved layout
    Q_INVOKABLE void applyPresetGrid(int cols, int rows);  // ad-hoc N×M grid
    Q_INVOKABLE void assignCamera(int cellIndex, const QString& cameraUuid);
    Q_INVOKABLE void mergeCells(int cellIndex, int spanW, int spanH);
    Q_INVOKABLE void saveActiveLayout(const QString& nameFa, bool shared);
    Q_INVOKABLE void setAuthToken(const QString& jwtAccessToken);

    QString apiBaseUrl() const { return api_base_url_; }
    void setApiBaseUrl(const QString& url);
    QVariantMap activeLayout() const;
    bool loading() const { return pending_ > 0; }
    QString errorFa() const { return error_fa_; }

signals:
    void apiBaseUrlChanged();
    void activeLayoutChanged();
    void loadingChanged();
    void errorFaChanged();
    // Emitted so the StreamController can (re)negotiate relay sessions.
    void cellCameraChanged(int cellIndex, const QString& cameraUuid);

private:
    void requestLayoutsPage(const QUrl& url);      // follows DRF `next` links
    void onLayoutsReply(QNetworkReply* reply);
    void setErrorFa(const QString& message);
    QNetworkReply* authedRequest(const QUrl& url, const QByteArray& verb,
                                 const QByteArray& body = {});

    QNetworkAccessManager nam_;
    QString api_base_url_ = QStringLiteral("http://127.0.0.1:8000");
    QString auth_token_;

    std::vector<SavedLayout> layouts_;
    std::vector<SavedLayout> incoming_;  // pages accumulate off-model here
    QJsonObject active_;                 // currently applied layout_json
    int pending_ = 0;
    QString error_fa_;
};

}  // namespace vms
