/****************************************************************************
** Meta object code from reading C++ file 'stream_controller.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/stream/stream_controller.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'stream_controller.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN3vms16StreamControllerE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::StreamController::qt_create_metaobjectdata<qt_meta_tag_ZN3vms16StreamControllerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::StreamController",
        "relayHostChanged",
        "",
        "cellFramePresented",
        "cellIndex",
        "utcUs",
        "cellStatusChanged",
        "statusFa",
        "attachLive",
        "cameraUuid",
        "profile",
        "QVideoSink*",
        "sink",
        "switchProfile",
        "attachPlayback",
        "startUtcUs",
        "detach",
        "detachAll",
        "profileForCellSize",
        "cellWidth",
        "gridWidth",
        "relayHost"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'relayHostChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'cellFramePresented'
        QtMocHelpers::SignalData<void(int, quint64)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::ULongLong, 5 },
        }}),
        // Signal 'cellStatusChanged'
        QtMocHelpers::SignalData<void(int, const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::QString, 7 },
        }}),
        // Method 'attachLive'
        QtMocHelpers::MethodData<void(int, const QString &, const QString &, QVideoSink *)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::QString, 9 }, { QMetaType::QString, 10 }, { 0x80000000 | 11, 12 },
        }}),
        // Method 'switchProfile'
        QtMocHelpers::MethodData<void(int, const QString &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::QString, 10 },
        }}),
        // Method 'attachPlayback'
        QtMocHelpers::MethodData<void(int, const QString &, QVideoSink *, quint64)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::QString, 9 }, { 0x80000000 | 11, 12 }, { QMetaType::ULongLong, 15 },
        }}),
        // Method 'detach'
        QtMocHelpers::MethodData<void(int)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Method 'detachAll'
        QtMocHelpers::MethodData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'profileForCellSize'
        QtMocHelpers::MethodData<QString(qreal, qreal) const>(18, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QReal, 19 }, { QMetaType::QReal, 20 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'relayHost'
        QtMocHelpers::PropertyData<QString>(21, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<StreamController, qt_meta_tag_ZN3vms16StreamControllerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::StreamController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms16StreamControllerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms16StreamControllerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms16StreamControllerE_t>.metaTypes,
    nullptr
} };

void vms::StreamController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<StreamController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->relayHostChanged(); break;
        case 1: _t->cellFramePresented((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint64>>(_a[2]))); break;
        case 2: _t->cellStatusChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 3: _t->attachLive((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QVideoSink*>>(_a[4]))); break;
        case 4: _t->switchProfile((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 5: _t->attachPlayback((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVideoSink*>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<quint64>>(_a[4]))); break;
        case 6: _t->detach((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->detachAll(); break;
        case 8: { QString _r = _t->profileForCellSize((*reinterpret_cast<std::add_pointer_t<qreal>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qreal>>(_a[2])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (StreamController::*)()>(_a, &StreamController::relayHostChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (StreamController::*)(int , quint64 )>(_a, &StreamController::cellFramePresented, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (StreamController::*)(int , const QString & )>(_a, &StreamController::cellStatusChanged, 2))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->relayHost(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setRelayHost(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *vms::StreamController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::StreamController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms16StreamControllerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int vms::StreamController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void vms::StreamController::relayHostChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::StreamController::cellFramePresented(int _t1, quint64 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void vms::StreamController::cellStatusChanged(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}
QT_WARNING_POP
