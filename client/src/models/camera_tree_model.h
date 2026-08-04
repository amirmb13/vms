#pragma once
// =============================================================================
// CameraTreeModel — QAbstractItemModel over the Django REST camera directory.
//
//   GET /api/camera-groups/?tree=1  -> recursive group tree (roots only)
//   GET /api/cameras/               -> flat camera list, merged in by group id
//
// Feeds the RTL Farsi tree view (CameraTree.qml). Group nodes carry the
// Farsi group name + camera count; camera leaves carry the camera UUID used
// by the Grid Engine / Stream Controller to request relay streams.
// UTF-8 on the wire; QString (UTF-16) internally.
// =============================================================================
#include <QAbstractItemModel>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QString>

#include <memory>
#include <vector>

class QNetworkReply;

namespace vms {

struct CameraTreeNode {
    enum class Kind { Group, Camera };

    Kind kind = Kind::Group;
    QString nameFa;

    // Group payload
    int groupId = -1;
    int cameraCount = 0;

    // Camera payload
    QString cameraUuid;
    bool recordingEnabled = true;

    CameraTreeNode* parent = nullptr;
    std::vector<std::unique_ptr<CameraTreeNode>> children;

    // Cached index inside parent->children, assigned at build time.
    // Computing it by linear search made parent() O(n) per call — with a
    // 10,000-camera directory the TreeView degraded to O(n²).
    int rowInParent = 0;

    int row() const;
};

class CameraTreeModel : public QAbstractItemModel {
    Q_OBJECT
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl WRITE setApiBaseUrl
                   NOTIFY apiBaseUrlChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorFa READ errorFa NOTIFY errorFaChanged)

public:
    enum Roles {
        NameFaRole = Qt::UserRole + 1,
        IsCameraRole,
        CameraUuidRole,
        CameraCountRole,
        RecordingEnabledRole,
        GroupIdRole,
    };
    Q_ENUM(Roles)

    explicit CameraTreeModel(QObject* parent = nullptr);
    ~CameraTreeModel() override;

    // --- QAbstractItemModel -------------------------------------------------
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- QML API ------------------------------------------------------------
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setAuthToken(const QString& jwtAccessToken);

    QString apiBaseUrl() const { return api_base_url_; }
    void setApiBaseUrl(const QString& url);
    bool loading() const { return pending_replies_ > 0; }
    QString errorFa() const { return error_fa_; }

signals:
    void apiBaseUrlChanged();
    void loadingChanged();
    void errorFaChanged();

private:
    QNetworkReply* authedGet(const QString& path);
    QNetworkReply* authedGetUrl(const QUrl& url);
    void onGroupsReply(QNetworkReply* reply);
    void onCamerasReply(QNetworkReply* reply);
    void finishFetch();
    void requestNextCamerasPage(const QUrl& url);
    void scheduleRebuild();
    void setErrorFa(const QString& message);

    CameraTreeNode* nodeFor(const QModelIndex& index) const;

    // Pure function: builds a detached tree from JSON snapshots. Runs on a
    // QtConcurrent worker so a 10,000-camera parse never blocks the GUI
    // thread (QJson types are implicitly shared, copies are cheap and
    // thread-safe).
    static std::shared_ptr<CameraTreeNode> buildTree(QJsonArray groups,
                                                     QJsonArray cameras);

    QNetworkAccessManager nam_;
    QString api_base_url_ = QStringLiteral("http://127.0.0.1:8000");
    QString auth_token_;

    std::shared_ptr<CameraTreeNode> root_;
    QJsonArray groups_data_;
    QJsonArray cameras_data_;
    // DRF-paginated /api/cameras/ pages accumulate here until `next` is null.
    QJsonArray cameras_accumulating_;
    int pending_replies_ = 0;
    // Per-fetch outcome flags. A failed request must NOT clobber the data we
    // already have (mock or previous fetch) — wiping cameras on error made
    // group nodes lose their children, so their expand arrows vanished a few
    // seconds after startup whenever the server was unreachable.
    bool fetch_had_error_ = false;
    bool fetch_got_data_ = false;
    // Builds never overlap: if fresh data lands while a build is running,
    // one follow-up build is queued and started when the current finishes.
    bool rebuild_queued_ = false;
    QFutureWatcher<std::shared_ptr<CameraTreeNode>> build_watcher_;
    QString error_fa_;
};

}  // namespace vms
