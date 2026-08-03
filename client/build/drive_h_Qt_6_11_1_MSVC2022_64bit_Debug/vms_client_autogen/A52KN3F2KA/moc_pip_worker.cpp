/****************************************************************************
** Meta object code from reading C++ file 'pip_worker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/playback/pip_worker.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'pip_worker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms9PipWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::PipWorker::qt_create_metaobjectdata<qt_meta_tag_ZN3vms9PipWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::PipWorker",
        "activeChanged",
        "",
        "positionChanged",
        "open",
        "cameraUuid",
        "startUtcUs",
        "QVideoSink*",
        "sink",
        "seek",
        "utcUs",
        "setPaused",
        "paused",
        "setRate",
        "rate",
        "close",
        "active",
        "positionUtcUs"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'activeChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'positionChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'open'
        QtMocHelpers::MethodData<void(const QString &, quint64, QVideoSink *)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 5 }, { QMetaType::ULongLong, 6 }, { 0x80000000 | 7, 8 },
        }}),
        // Method 'seek'
        QtMocHelpers::MethodData<void(quint64)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 10 },
        }}),
        // Method 'setPaused'
        QtMocHelpers::MethodData<void(bool)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 12 },
        }}),
        // Method 'setRate'
        QtMocHelpers::MethodData<void(double)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 14 },
        }}),
        // Method 'close'
        QtMocHelpers::MethodData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'active'
        QtMocHelpers::PropertyData<bool>(16, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'positionUtcUs'
        QtMocHelpers::PropertyData<quint64>(17, QMetaType::ULongLong, QMC::DefaultPropertyFlags, 1),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PipWorker, qt_meta_tag_ZN3vms9PipWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::PipWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9PipWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9PipWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms9PipWorkerE_t>.metaTypes,
    nullptr
} };

void vms::PipWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PipWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->activeChanged(); break;
        case 1: _t->positionChanged(); break;
        case 2: _t->open((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<quint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QVideoSink*>>(_a[3]))); break;
        case 3: _t->seek((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1]))); break;
        case 4: _t->setPaused((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->setRate((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 6: _t->close(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PipWorker::*)()>(_a, &PipWorker::activeChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PipWorker::*)()>(_a, &PipWorker::positionChanged, 1))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->active(); break;
        case 1: *reinterpret_cast<quint64*>(_v) = _t->positionUtcUs(); break;
        default: break;
        }
    }
}

const QMetaObject *vms::PipWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::PipWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9PipWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int vms::PipWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void vms::PipWorker::activeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::PipWorker::positionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
