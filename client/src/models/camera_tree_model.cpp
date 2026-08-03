// =============================================================================
// CameraTreeModel implementation with offline Mock Data support.
//
// Scale contract (10,000+ cameras):
//   * /api/cameras/ is DRF-paginated — every `next` page is followed and
//     accumulated before the tree is rebuilt (a single unpaginated response
//     for 10k cameras is both slow and memory-hostile on the server).
//   * Tree construction (JSON -> node graph) runs on a QtConcurrent worker;
//     only the final beginResetModel/endResetModel swap touches the GUI
//     thread, so the wall never stutters during a directory refresh.
// =============================================================================
#include "models/camera_tree_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtConcurrent>

#include <functional>
#include <unordered_map>

namespace vms {

namespace {

QJsonArray extractArray(const QByteArray& payload) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isArray()) return doc.array();
    if (doc.isObject()) return doc.object().value("results").toArray();
    return {};
}

}  // namespace

int CameraTreeNode::row() const { return rowInParent; }

CameraTreeModel::CameraTreeModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_shared<CameraTreeNode>()) {

    // بارگذاری داده‌های اولیه تست
    groups_data_ = extractArray(QByteArray(
        "[\n"
        "  {\"id\": 1, \"name_fa\": \"ورودی و لابی اصلی\", \"camera_count\": 2, \"children\": []},\n"
        "  {\"id\": 2, \"name_fa\": \"محیط و پارکینگ\", \"camera_count\": 2, \"children\": [\n"
        "    {\"id\": 3, \"name_fa\": \"طبقه منفی یک (انباری)\", \"camera_count\": 1, \"children\": []}\n"
        "  ]}\n"
        "]"
        ));

    cameras_data_ = extractArray(QByteArray(
        "[\n"
        "  {\"id\": 101, \"uuid\": \"E:/kashfsho/moarefi/end_scene_cutted.mp4\", \"name_fa\": \"دوربین ورودی (ویدیو محلی)\", \"group\": 1, \"recording_enabled\": true},\n"
        "  {\"id\": 102, \"uuid\": \"E:/kashfsho/moarefi/MK_scene2.mp4\", \"name_fa\": \"دوربین سالن (ویدیو محلی)\", \"group\": 1, \"recording_enabled\": true},\n"
        "  {\"id\": 103, \"uuid\": \"E:/kashfsho/moarefi/MK_scene3.mp4\", \"name_fa\": \"دوربین پارکینگ (ویدیو محلی)\", \"group\": 2, \"recording_enabled\": true}\n"
        "]"
        ));

    // Apply a finished background build on the GUI thread. Builds never
    // overlap; a queued follow-up is started here when needed.
    connect(&build_watcher_,
            &QFutureWatcher<std::shared_ptr<CameraTreeNode>>::finished, this,
            [this] {
                std::shared_ptr<CameraTreeNode> built = build_watcher_.result();
                if (built) {
                    beginResetModel();
                    root_ = std::move(built);
                    endResetModel();
                }
                if (rebuild_queued_) {
                    rebuild_queued_ = false;
                    scheduleRebuild();
                }
            });

    // Initial mock data is tiny — build synchronously so the tree is ready
    // before the first frame.
    root_ = buildTree(groups_data_, cameras_data_);
}

CameraTreeModel::~CameraTreeModel() {
    build_watcher_.waitForFinished();
}

// --- QAbstractItemModel ------------------------------------------------------

CameraTreeNode* CameraTreeModel::nodeFor(const QModelIndex& index) const {
    if (!index.isValid()) return root_.get();
    return static_cast<CameraTreeNode*>(index.internalPointer());
}

QModelIndex CameraTreeModel::index(int row, int column,
                                   const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return {};
    CameraTreeNode* parent_node = nodeFor(parent);
    if (row < 0 || static_cast<size_t>(row) >= parent_node->children.size())
        return {};
    return createIndex(row, column, parent_node->children[row].get());
}

QModelIndex CameraTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    CameraTreeNode* node = nodeFor(child);
    CameraTreeNode* parent_node = node ? node->parent : nullptr;
    if (!parent_node || parent_node == root_.get()) return {};
    return createIndex(parent_node->row(), 0, parent_node);
}

int CameraTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) return 0;
    return static_cast<int>(nodeFor(parent)->children.size());
}

int CameraTreeModel::columnCount(const QModelIndex&) const { return 1; }

QVariant CameraTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const CameraTreeNode* node = nodeFor(index);

    switch (role) {
    case Qt::DisplayRole:
    case NameFaRole:
        return node->nameFa;
    case IsCameraRole:
        return node->kind == CameraTreeNode::Kind::Camera;
    case CameraUuidRole:
        return node->cameraUuid;
    case CameraCountRole:
        return node->cameraCount;
    case RecordingEnabledRole:
        return node->recordingEnabled;
    case GroupIdRole:
        return node->groupId;
    default:
        return {};
    }
}

QHash<int, QByteArray> CameraTreeModel::roleNames() const {
    return {
             {NameFaRole, "nameFa"},
             {IsCameraRole, "isCamera"},
             {CameraUuidRole, "cameraUuid"},
             {CameraCountRole, "cameraCount"},
             {RecordingEnabledRole, "recordingEnabled"},
             {GroupIdRole, "groupId"},
             };
}

// --- Networking --------------------------------------------------------------

void CameraTreeModel::setApiBaseUrl(const QString& url) {
    if (api_base_url_ == url) return;
    api_base_url_ = url;
    emit apiBaseUrlChanged();
}

void CameraTreeModel::setAuthToken(const QString& jwtAccessToken) {
    auth_token_ = jwtAccessToken;
}

QNetworkReply* CameraTreeModel::authedGet(const QString& path) {
    return authedGetUrl(QUrl(api_base_url_ + path));
}

QNetworkReply* CameraTreeModel::authedGetUrl(const QUrl& url) {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    if (!auth_token_.isEmpty()) {
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + auth_token_.toUtf8());
    }
    return nam_.get(request);
}

void CameraTreeModel::setErrorFa(const QString& message) {
    if (error_fa_ == message) return;
    error_fa_ = message;
    emit errorFaChanged();
}

void CameraTreeModel::reload() {
    // اگر آدرس سرور تنظیم نشده است، روی داده‌های Mock باقی بمان و درخواست شبکه نفرست
    if (api_base_url_.isEmpty()) {
        return;
    }

    if (pending_replies_ > 0) return;  // fetch already in flight
    setErrorFa({});
    cameras_accumulating_ = QJsonArray();
    pending_replies_ = 2;
    emit loadingChanged();

    QNetworkReply* groups = authedGet(QStringLiteral("/api/camera-groups/?tree=1"));
    connect(groups, &QNetworkReply::finished, this,
            [this, groups] { onGroupsReply(groups); });

    QNetworkReply* cameras = authedGet(QStringLiteral("/api/cameras/"));
    connect(cameras, &QNetworkReply::finished, this,
            [this, cameras] { onCamerasReply(cameras); });
}

void CameraTreeModel::onGroupsReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        groups_data_ = extractArray(reply->readAll());
    }
    if (--pending_replies_ == 0) {
        emit loadingChanged();
        scheduleRebuild();
    }
}

void CameraTreeModel::requestNextCamerasPage(const QUrl& url) {
    QNetworkReply* next = authedGetUrl(url);
    connect(next, &QNetworkReply::finished, this,
            [this, next] { onCamerasReply(next); });
}

void CameraTreeModel::onCamerasReply(QNetworkReply* reply) {
    reply->deleteLater();

    bool more_pages = false;
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isArray()) {
            // Unpaginated server (legacy) — single shot.
            cameras_accumulating_ = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QJsonArray page = obj.value("results").toArray();
            for (const QJsonValue& v : page) cameras_accumulating_.append(v);

            const QJsonValue next = obj.value("next");
            if (next.isString() && !next.toString().isEmpty()) {
                // Follow DRF pagination until exhausted. pending_replies_
                // stays >0 so `loading` remains true and reload() is
                // re-entrancy-safe for the whole multi-page fetch.
                more_pages = true;
                requestNextCamerasPage(QUrl(next.toString()));
            }
        }
    }

    if (more_pages) return;

    cameras_data_ = cameras_accumulating_;
    cameras_accumulating_ = QJsonArray();
    if (--pending_replies_ == 0) {
        emit loadingChanged();
        scheduleRebuild();
    }
}

// --- Tree assembly -----------------------------------------------------------

std::shared_ptr<CameraTreeNode> CameraTreeModel::buildTree(QJsonArray groups,
                                                           QJsonArray cameras) {
    auto new_root = std::make_shared<CameraTreeNode>();

    std::unordered_map<int, CameraTreeNode*> group_index;

    std::function<void(const QJsonObject&, CameraTreeNode*)> add_group =
        [&](const QJsonObject& obj, CameraTreeNode* parent) {
            auto node = std::make_unique<CameraTreeNode>();
            node->kind = CameraTreeNode::Kind::Group;
            node->nameFa = obj.value("name_fa").toString();
            node->groupId = obj.value("id").toInt(-1);
            node->cameraCount = obj.value("camera_count").toInt(0);
            node->parent = parent;
            group_index[node->groupId] = node.get();

            const QJsonArray children = obj.value("children").toArray();
            for (const QJsonValue& child : children) {
                add_group(child.toObject(), node.get());
            }
            node->rowInParent = static_cast<int>(parent->children.size());
            parent->children.push_back(std::move(node));
        };

    for (const QJsonValue& value : groups) {
        add_group(value.toObject(), new_root.get());
    }

    for (const QJsonValue& value : cameras) {
        const QJsonObject obj = value.toObject();
        const int group_id = obj.value("group").toInt(-1);
        const auto it = group_index.find(group_id);
        CameraTreeNode* parent =
            (it != group_index.end()) ? it->second : new_root.get();

        auto leaf = std::make_unique<CameraTreeNode>();
        leaf->kind = CameraTreeNode::Kind::Camera;
        leaf->nameFa = obj.value("name_fa").toString();
        leaf->cameraUuid = obj.value("uuid").toString();
        leaf->recordingEnabled = obj.value("recording_enabled").toBool(true);
        leaf->groupId = group_id;
        leaf->parent = parent;
        leaf->rowInParent = static_cast<int>(parent->children.size());
        parent->children.push_back(std::move(leaf));
    }

    return new_root;
}

void CameraTreeModel::scheduleRebuild() {
    if (build_watcher_.isRunning()) {
        rebuild_queued_ = true;  // coalesce: at most one queued follow-up
        return;
    }
    // QJsonArray copies are implicitly shared and detach-on-write, so the
    // worker owns immutable snapshots — no locking required.
    build_watcher_.setFuture(
        QtConcurrent::run(&CameraTreeModel::buildTree, groups_data_,
                          cameras_data_));
}

}  // namespace vms
