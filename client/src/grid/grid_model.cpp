#include "grid/grid_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace vms {
namespace {

QJsonObject makeCell(int x, int y, int w = 1, int h = 1) {
    return QJsonObject{{QStringLiteral("x"), x},
                       {QStringLiteral("y"), y},
                       {QStringLiteral("w"), w},
                       {QStringLiteral("h"), h},
                       {QStringLiteral("camera_uuid"), QJsonValue::Null}};
}

}  // namespace

GridModel::GridModel(QObject* parent) : QAbstractListModel(parent) {
    // Boot with a sensible 2x2 grid until Django layouts arrive.
    QJsonArray cells;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) cells.append(makeCell(x, y));
    active_ = QJsonObject{
        {QStringLiteral("grid"),
         QJsonObject{{QStringLiteral("cols"), 2}, {QStringLiteral("rows"), 2}}},
        {QStringLiteral("cells"), cells}};
}

// --- Catalogue model ---------------------------------------------------------
int GridModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(layouts_.size());
}

QVariant GridModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(layouts_.size()))
        return {};
    const SavedLayout& l = layouts_[static_cast<size_t>(index.row())];
    switch (role) {
        case LayoutIdRole: return l.uuid;
        case NameFaRole: return l.nameFa;
        case IsSharedRole: return l.isShared;
        // Cached at parse time; re-parsing the cells array on every data()
        // call made list painting O(cells) per delegate per frame.
        case CellCountRole: return l.cellCount;
        default: return {};
    }
}

QHash<int, QByteArray> GridModel::roleNames() const {
    return {{LayoutIdRole, "layoutId"},
            {NameFaRole, "nameFa"},
            {IsSharedRole, "isShared"},
            {CellCountRole, "cellCount"}};
}

// --- Networking ----------------------------------------------------------------
QNetworkReply* GridModel::authedRequest(const QUrl& url,
                                        const QByteArray& verb,
                                        const QByteArray& body) {
    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    if (!auth_token_.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + auth_token_.toUtf8());

    ++pending_;
    emit loadingChanged();
    return nam_.sendCustomRequest(req, verb, body);
}

void GridModel::reload() {
    incoming_.clear();
    requestLayoutsPage(QUrl(api_base_url_ + QStringLiteral("/api/layouts/")));
}

void GridModel::requestLayoutsPage(const QUrl& url) {
    QNetworkReply* reply = authedRequest(url, "GET");
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onLayoutsReply(reply); });
}

void GridModel::onLayoutsReply(QNetworkReply* reply) {
    reply->deleteLater();
    --pending_;
    emit loadingChanged();

    if (reply->error() != QNetworkReply::NoError) {
        incoming_.clear();
        setErrorFa(QStringLiteral("خطا در دریافت چیدمان‌ها از سرور مدیریت"));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject page = doc.object();
    const QJsonArray results =
        doc.isArray() ? doc.array()
                      : page.value(QStringLiteral("results")).toArray();

    // Field names mirror GridLayoutSerializer exactly: uuid / name_fa /
    // is_shared / layout_json (the pre-fix "id"/"name" pair silently produced
    // empty catalogue rows and rejected saves).
    for (const QJsonValue& v : results) {
        const QJsonObject o = v.toObject();
        SavedLayout l;
        l.uuid = o.value(QStringLiteral("uuid")).toString();
        l.nameFa = o.value(QStringLiteral("name_fa")).toString();
        l.isShared = o.value(QStringLiteral("is_shared")).toBool();
        l.layoutJson = o.value(QStringLiteral("layout_json")).toObject();
        l.cellCount =
            l.layoutJson.value(QStringLiteral("cells")).toArray().size();
        incoming_.push_back(std::move(l));
    }

    // DRF pagination: follow `next` until exhausted, then swap the model in
    // ONE reset (page-by-page resets would repaint the catalogue N times).
    const QJsonValue next = page.value(QStringLiteral("next"));
    if (next.isString() && !next.toString().isEmpty()) {
        requestLayoutsPage(QUrl(next.toString()));
        return;
    }

    beginResetModel();
    layouts_ = std::move(incoming_);
    incoming_.clear();
    endResetModel();
    setErrorFa({});
}

// --- Active layout mutations ----------------------------------------------------
QVariantMap GridModel::activeLayout() const { return active_.toVariantMap(); }

void GridModel::applyLayoutAt(int row) {
    if (row < 0 || row >= static_cast<int>(layouts_.size())) return;
    active_ = layouts_[static_cast<size_t>(row)].layoutJson;
    emit activeLayoutChanged();

    // Re-announce every occupied cell so relay sessions renegotiate.
    const QJsonArray cells = active_.value(QStringLiteral("cells")).toArray();
    for (int i = 0; i < cells.size(); ++i) {
        const QString uuid =
            cells[i].toObject().value(QStringLiteral("camera_uuid")).toString();
        if (!uuid.isEmpty()) emit cellCameraChanged(i, uuid);
    }
}

void GridModel::applyPresetGrid(int cols, int rows) {
    // Match the server-side schema bound (16x16) — enterprise video walls
    // legitimately run much denser than the old 8x8 client cap.
    cols = qBound(1, cols, kMaxGridDim);
    rows = qBound(1, rows, kMaxGridDim);
    QJsonArray cells;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) cells.append(makeCell(x, y));
    active_ = QJsonObject{
        {QStringLiteral("grid"), QJsonObject{{QStringLiteral("cols"), cols},
                                             {QStringLiteral("rows"), rows}}},
        {QStringLiteral("cells"), cells}};
    emit activeLayoutChanged();
}

void GridModel::assignCamera(int cellIndex, const QString& cameraUuid) {
    QJsonArray cells = active_.value(QStringLiteral("cells")).toArray();
    if (cellIndex < 0 || cellIndex >= cells.size()) return;
    QJsonObject cell = cells[cellIndex].toObject();
    cell.insert(QStringLiteral("camera_uuid"), cameraUuid);
    cells[cellIndex] = cell;
    active_.insert(QStringLiteral("cells"), cells);
    emit activeLayoutChanged();
    emit cellCameraChanged(cellIndex, cameraUuid);
}

void GridModel::mergeCells(int cellIndex, int spanW, int spanH) {
    QJsonArray cells = active_.value(QStringLiteral("cells")).toArray();
    if (cellIndex < 0 || cellIndex >= cells.size()) return;
    const QJsonObject grid = active_.value(QStringLiteral("grid")).toObject();
    const int cols = grid.value(QStringLiteral("cols")).toInt(1);
    const int rows = grid.value(QStringLiteral("rows")).toInt(1);

    QJsonObject target = cells[cellIndex].toObject();
    const int tx = target.value(QStringLiteral("x")).toInt();
    const int ty = target.value(QStringLiteral("y")).toInt();
    spanW = qBound(1, spanW, cols - tx);
    spanH = qBound(1, spanH, rows - ty);
    target.insert(QStringLiteral("w"), spanW);
    target.insert(QStringLiteral("h"), spanH);

    // Drop every cell fully covered by the merged region (except the target).
    QJsonArray next;
    for (int i = 0; i < cells.size(); ++i) {
        if (i == cellIndex) { next.append(target); continue; }
        const QJsonObject c = cells[i].toObject();
        const int cx = c.value(QStringLiteral("x")).toInt();
        const int cy = c.value(QStringLiteral("y")).toInt();
        const bool covered = cx >= tx && cx < tx + spanW &&
                             cy >= ty && cy < ty + spanH;
        if (!covered) next.append(c);
    }
    active_.insert(QStringLiteral("cells"), next);
    emit activeLayoutChanged();
}

void GridModel::saveActiveLayout(const QString& nameFa, bool shared) {
    // Payload matches GridLayoutSerializer writable fields.
    const QJsonObject payload{{QStringLiteral("name_fa"), nameFa},
                              {QStringLiteral("is_shared"), shared},
                              {QStringLiteral("layout_json"), active_}};
    QNetworkReply* reply = authedRequest(
        QUrl(api_base_url_ + QStringLiteral("/api/layouts/")), "POST",
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        --pending_;
        emit loadingChanged();
        if (reply->error() != QNetworkReply::NoError) {
            setErrorFa(QStringLiteral("ذخیره‌سازی چیدمان با خطا مواجه شد"));
            return;
        }
        reload();  // refresh catalogue with the server-validated document
    });
}

void GridModel::setAuthToken(const QString& jwtAccessToken) {
    auth_token_ = jwtAccessToken;
}

void GridModel::setApiBaseUrl(const QString& url) {
    if (api_base_url_ == url) return;
    api_base_url_ = url;
    emit apiBaseUrlChanged();
}

void GridModel::setErrorFa(const QString& message) {
    if (error_fa_ == message) return;
    error_fa_ = message;
    emit errorFaChanged();
}

}  // namespace vms
