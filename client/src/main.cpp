// =============================================================================
// VMS Desktop Client entry point.
// - Forces Qt Quick Scene Graph onto RHI (Direct3D 12 on Windows / Vulkan).
// - Registers Farsi fonts, enforces RTL, exposes C++ models to QML.
// =============================================================================
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QFontDatabase>
#include <QLocale>

#include "grid/grid_model.h"
#include "models/camera_tree_model.h"
#include "net/session_manager.h"
#include "stream/stream_controller.h"
#include "playback/sync_playback.h"
#include "playback/pip_worker.h"
#include "i18n/shamsi_formatter.h"

int main(int argc, char* argv[]) {
    // RHI mandate: never fall back to OpenGL legacy paths.
#if defined(VMS_RHI_D3D12)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#endif

    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Vms"));
    app.setApplicationName(QStringLiteral("سامانه مدیریت تصاویر نظارتی"));

    // Force the "Basic" (non-native) Controls style. Platform-native styles
    // (FluentWinUI3 etc.) paint their own light backgrounds under our dark
    // theme — that is exactly where white-on-white text came from — and they
    // forbid customizing control internals (Slider, ComboBox).
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Farsi locale drives RTL layout direction application-wide.
    QLocale::setDefault(QLocale(QLocale::Persian, QLocale::Iran));
    QGuiApplication::setLayoutDirection(Qt::RightToLeft);

    // Persian typography (UTF-8 everywhere; QString is UTF-16 internally).
    QFontDatabase::addApplicationFont(":/Vms/Client/resources/fonts/Vazirmatn-Regular.ttf");
    QFontDatabase::addApplicationFont(":/Vms/Client/resources/fonts/Vazirmatn-Bold.ttf");
    QFont vazir(QStringLiteral("Vazirmatn"));
    vazir.setPixelSize(14);
    QGuiApplication::setFont(vazir);

    QQmlApplicationEngine engine;

    // C++ backends exposed to QML (grid engine, adaptive streams, sync playback)
    vms::GridModel gridModel;               // QAbstractListModel — layout JSONs
    vms::CameraTreeModel cameraTreeModel;   // RTL directory tree (REST-backed)
    vms::SessionManager session;            // backend connection + JWT session
    vms::StreamController streamController; // profile switching vs. Media Relay
    vms::SyncPlayback syncPlayback;         // master NTP timeline broadcaster
    vms::ShamsiFormatter shamsi;            // ۱۴۰۵/۰۴/۳۰ - ۱۸:۵۱:۱۵ rendering

    // Master timeline fans out NTP seek/pause/rate to every archive decoder.
    syncPlayback.attachController(&streamController);

    // When a session becomes available (fresh login or token refresh), arm the
    // REST models with the server address + access token and reload them.
    QObject::connect(&session, &vms::SessionManager::sessionRestored,
                     &cameraTreeModel, [&] {
        const QString base = session.apiBaseUrl();
        const QString token = session.authToken();
        cameraTreeModel.setApiBaseUrl(base);
        cameraTreeModel.setAuthToken(token);
        gridModel.setApiBaseUrl(base);
        gridModel.setAuthToken(token);
        cameraTreeModel.reload();
        gridModel.reload();
    });

    // PiP workers are instantiated per-cell from QML (VideoCell.qml).
    qmlRegisterType<vms::PipWorker>("Vms.Client", 1, 0, "PipWorker");

    engine.rootContext()->setContextProperty("gridModel", &gridModel);
    engine.rootContext()->setContextProperty("cameraTreeModel", &cameraTreeModel);
    engine.rootContext()->setContextProperty("session", &session);
    engine.rootContext()->setContextProperty("streamController", &streamController);
    engine.rootContext()->setContextProperty("syncPlayback", &syncPlayback);
    engine.rootContext()->setContextProperty("shamsi", &shamsi);

    engine.loadFromModule("Vms.Client", "Main");

    // Restore a previous session (persisted refresh token) once QML is live.
    session.restore();
    return app.exec();
}
