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

    // Cached position within parent->children, assigned at build time.
    // A linear scan here is O(n) per call; with a 10k-camera group the
    // TreeView's parent() lookups would degrade to O(n^2).
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
    void onGroupsReply(QNetworkReply* reply);
    void onCamerasReply(QNetworkReply* reply);
    void rebuildTree();
    void setErrorFa(const QString& message);

    CameraTreeNode* nodeFor(const QModelIndex& index) const;

    QNetworkAccessManager nam_;
    QString api_base_url_ = QStringLiteral("http://127.0.0.1:8000");
    QString auth_token_;

    std::unique_ptr<CameraTreeNode> root_;
    QByteArray groups_payload_;
    QByteArray cameras_payload_;
    int pending_replies_ = 0;
    QString error_fa_;
};

}  // namespace vms
