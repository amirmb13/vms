/****************************************************************************
** Meta object code from reading C++ file 'camera_tree_model.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/models/camera_tree_model.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'camera_tree_model.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms15CameraTreeModelE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::CameraTreeModel::qt_create_metaobjectdata<qt_meta_tag_ZN3vms15CameraTreeModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::CameraTreeModel",
        "apiBaseUrlChanged",
        "",
        "loadingChanged",
        "errorFaChanged",
        "reload",
        "setAuthToken",
        "jwtAccessToken",
        "apiBaseUrl",
        "loading",
        "errorFa",
        "Roles",
        "NameFaRole",
        "IsCameraRole",
        "CameraUuidRole",
        "CameraCountRole",
        "RecordingEnabledRole",
        "GroupIdRole"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'apiBaseUrlChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'loadingChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'errorFaChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'reload'
        QtMocHelpers::MethodData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'setAuthToken'
        QtMocHelpers::MethodData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'apiBaseUrl'
        QtMocHelpers::PropertyData<QString>(8, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'loading'
        QtMocHelpers::PropertyData<bool>(9, QMetaType::Bool, QMC::DefaultPropertyFlags, 1),
        // property 'errorFa'
        QtMocHelpers::PropertyData<QString>(10, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'Roles'
        QtMocHelpers::EnumData<enum Roles>(11, 11, QMC::EnumFlags{}).add({
            {   12, Roles::NameFaRole },
            {   13, Roles::IsCameraRole },
            {   14, Roles::CameraUuidRole },
            {   15, Roles::CameraCountRole },
            {   16, Roles::RecordingEnabledRole },
            {   17, Roles::GroupIdRole },
        }),
    };
    return QtMocHelpers::metaObjectData<CameraTreeModel, qt_meta_tag_ZN3vms15CameraTreeModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::CameraTreeModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractItemModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15CameraTreeModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15CameraTreeModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms15CameraTreeModelE_t>.metaTypes,
    nullptr
} };

void vms::CameraTreeModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CameraTreeModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->apiBaseUrlChanged(); break;
        case 1: _t->loadingChanged(); break;
        case 2: _t->errorFaChanged(); break;
        case 3: _t->reload(); break;
        case 4: _t->setAuthToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CameraTreeModel::*)()>(_a, &CameraTreeModel::apiBaseUrlChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CameraTreeModel::*)()>(_a, &CameraTreeModel::loadingChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CameraTreeModel::*)()>(_a, &CameraTreeModel::errorFaChanged, 2))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->apiBaseUrl(); break;
        case 1: *reinterpret_cast<bool*>(_v) = _t->loading(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->errorFa(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setApiBaseUrl(*reinterpret_cast<QString*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *vms::CameraTreeModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::CameraTreeModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15CameraTreeModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractItemModel::qt_metacast(_clname);
}

int vms::CameraTreeModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractItemModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void vms::CameraTreeModel::apiBaseUrlChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::CameraTreeModel::loadingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void vms::CameraTreeModel::errorFaChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
