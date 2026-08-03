/****************************************************************************
** Meta object code from reading C++ file 'sync_playback.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/playback/sync_playback.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'sync_playback.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms12SyncPlaybackE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::SyncPlayback::qt_create_metaobjectdata<qt_meta_tag_ZN3vms12SyncPlaybackE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::SyncPlayback",
        "playingChanged",
        "",
        "rateChanged",
        "positionChanged",
        "windowChanged",
        "liveRequested",
        "play",
        "pause",
        "seek",
        "utcUs",
        "stepFrames",
        "frames",
        "jumpToLive",
        "playing",
        "rate",
        "positionUtcUs",
        "windowStartUtcUs",
        "windowEndUtcUs"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'playingChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'rateChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'positionChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'windowChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'liveRequested'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'play'
        QtMocHelpers::MethodData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'pause'
        QtMocHelpers::MethodData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'seek'
        QtMocHelpers::MethodData<void(quint64)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 10 },
        }}),
        // Method 'stepFrames'
        QtMocHelpers::MethodData<void(int)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Method 'jumpToLive'
        QtMocHelpers::MethodData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'playing'
        QtMocHelpers::PropertyData<bool>(14, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'rate'
        QtMocHelpers::PropertyData<double>(15, QMetaType::Double, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'positionUtcUs'
        QtMocHelpers::PropertyData<quint64>(16, QMetaType::ULongLong, QMC::DefaultPropertyFlags, 2),
        // property 'windowStartUtcUs'
        QtMocHelpers::PropertyData<quint64>(17, QMetaType::ULongLong, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'windowEndUtcUs'
        QtMocHelpers::PropertyData<quint64>(18, QMetaType::ULongLong, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SyncPlayback, qt_meta_tag_ZN3vms12SyncPlaybackE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::SyncPlayback::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12SyncPlaybackE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12SyncPlaybackE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms12SyncPlaybackE_t>.metaTypes,
    nullptr
} };

void vms::SyncPlayback::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SyncPlayback *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->playingChanged(); break;
        case 1: _t->rateChanged(); break;
        case 2: _t->positionChanged(); break;
        case 3: _t->windowChanged(); break;
        case 4: _t->liveRequested(); break;
        case 5: _t->play(); break;
        case 6: _t->pause(); break;
        case 7: _t->seek((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1]))); break;
        case 8: _t->stepFrames((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->jumpToLive(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SyncPlayback::*)()>(_a, &SyncPlayback::playingChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SyncPlayback::*)()>(_a, &SyncPlayback::rateChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SyncPlayback::*)()>(_a, &SyncPlayback::positionChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (SyncPlayback::*)()>(_a, &SyncPlayback::windowChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (SyncPlayback::*)()>(_a, &SyncPlayback::liveRequested, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->playing(); break;
        case 1: *reinterpret_cast<double*>(_v) = _t->rate(); break;
        case 2: *reinterpret_cast<quint64*>(_v) = _t->positionUtcUs(); break;
        case 3: *reinterpret_cast<quint64*>(_v) = _t->windowStartUtcUs(); break;
        case 4: *reinterpret_cast<quint64*>(_v) = _t->windowEndUtcUs(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 1: _t->setRate(*reinterpret_cast<double*>(_v)); break;
        case 3: _t->setWindowStartUtcUs(*reinterpret_cast<quint64*>(_v)); break;
        case 4: _t->setWindowEndUtcUs(*reinterpret_cast<quint64*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *vms::SyncPlayback::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::SyncPlayback::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12SyncPlaybackE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int vms::SyncPlayback::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void vms::SyncPlayback::playingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::SyncPlayback::rateChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void vms::SyncPlayback::positionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void vms::SyncPlayback::windowChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void vms::SyncPlayback::liveRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
