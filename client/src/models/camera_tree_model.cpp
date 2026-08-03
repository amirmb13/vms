// =============================================================================
// CameraTreeModel implementation with offline Mock Data support.
// =============================================================================
#include "models/camera_tree_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <functional>
#include <unordered_map>

namespace vms {

int CameraTreeNode::row() const {
    if (!parent) return 0;
    for (size_t i = 0; i < parent->children.size(); ++i) {
        if (parent->children[i].get() == this) return static_cast<int>(i);
    }
    return 0;
}

CameraTreeModel::CameraTreeModel(QObject* parent)
    : QAbstractItemModel(parent), root_(std::make_unique<CameraTreeNode>()) {

    // بارگذاری داده‌های اولیه تست
    groups_payload_ = QByteArray(
        "[\n"
        "  {\"id\": 1, \"name_fa\": \"ورودی و لابی اصلی\", \"camera_count\": 2, \"children\": []},\n"
        "  {\"id\": 2, \"name_fa\": \"محیط و پارکینگ\", \"camera_count\": 2, \"children\": [\n"
        "    {\"id\": 3, \"name_fa\": \"طبقه منفی یک (انباری)\", \"camera_count\": 1, \"children\": []}\n"
        "  ]}\n"
        "]"
        );

    cameras_payload_ = QByteArray(
        "[\n"
        "  {\"id\": 101, \"uuid\": \"E:/kashfsho/moarefi/end_scene_cutted.mp4\", \"name_fa\": \"دوربین ورودی (ویدیو محلی)\", \"group\": 1, \"recording_enabled\": true},\n"
        "  {\"id\": 102, \"uuid\": \"E:/kashfsho/moarefi/MK_scene2.mp4\", \"name_fa\": \"دوربین سالن (ویدیو محلی)\", \"group\": 1, \"recording_enabled\": true},\n"
        "  {\"id\": 103, \"uuid\": \"E:/kashfsho/moarefi/MK_scene3.mp4\", \"name_fa\": \"دوربین پارکینگ (ویدیو محلی)\", \"group\": 2, \"recording_enabled\": true}\n"
        "]"
        );

    rebuildTree();
}

CameraTreeModel::~CameraTreeModel() = default;

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
    QNetworkRequest request(QUrl(api_base_url_ + path));
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
        groups_payload_ = reply->readAll();
    }
    if (--pending_replies_ == 0) {
        emit loadingChanged();
        rebuildTree();
    }
}

void CameraTreeModel::onCamerasReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        cameras_payload_ = reply->readAll();
    }
    if (--pending_replies_ == 0) {
        emit loadingChanged();
        rebuildTree();
    }
}

// --- Tree assembly -----------------------------------------------------------

namespace {

QJsonArray extractArray(const QByteArray& payload) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isArray()) return doc.array();
    if (doc.isObject()) return doc.object().value("results").toArray();
    return {};
}

}  // namespace

void CameraTreeModel::rebuildTree() {
    auto new_root = std::make_unique<CameraTreeNode>();

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
            parent->children.push_back(std::move(node));
        };

    const QJsonArray groups = extractArray(groups_payload_);
    for (const QJsonValue& value : groups) {
        add_group(value.toObject(), new_root.get());
    }

    const QJsonArray cameras = extractArray(cameras_payload_);
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
        parent->children.push_back(std::move(leaf));
    }

    beginResetModel();
    root_ = std::move(new_root);
    endResetModel();
}

}  // namespace vms