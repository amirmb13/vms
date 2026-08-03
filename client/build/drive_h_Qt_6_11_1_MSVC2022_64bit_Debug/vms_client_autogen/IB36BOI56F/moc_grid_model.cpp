/****************************************************************************
** Meta object code from reading C++ file 'grid_model.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/grid/grid_model.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'grid_model.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms9GridModelE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::GridModel::qt_create_metaobjectdata<qt_meta_tag_ZN3vms9GridModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::GridModel",
        "apiBaseUrlChanged",
        "",
        "activeLayoutChanged",
        "loadingChanged",
        "errorFaChanged",
        "cellCameraChanged",
        "cellIndex",
        "cameraUuid",
        "reload",
        "applyLayoutAt",
        "row",
        "applyPresetGrid",
        "cols",
        "rows",
        "assignCamera",
        "mergeCells",
        "spanW",
        "spanH",
        "saveActiveLayout",
        "nameFa",
        "shared",
        "setAuthToken",
        "jwtAccessToken",
        "apiBaseUrl",
        "activeLayout",
        "QVariantMap",
        "loading",
        "errorFa",
        "Roles",
        "LayoutIdRole",
        "NameFaRole",
        "IsSharedRole",
        "CellCountRole"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'apiBaseUrlChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'activeLayoutChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'loadingChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'errorFaChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'cellCameraChanged'
        QtMocHelpers::SignalData<void(int, const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 7 }, { QMetaType::QString, 8 },
        }}),
        // Method 'reload'
        QtMocHelpers::MethodData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'applyLayoutAt'
        QtMocHelpers::MethodData<void(int)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 11 },
        }}),
        // Method 'applyPresetGrid'
        QtMocHelpers::MethodData<void(int, int)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 13 }, { QMetaType::Int, 14 },
        }}),
        // Method 'assignCamera'
        QtMocHelpers::MethodData<void(int, const QString &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 7 }, { QMetaType::QString, 8 },
        }}),
        // Method 'mergeCells'
        QtMocHelpers::MethodData<void(int, int, int)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 7 }, { QMetaType::Int, 17 }, { QMetaType::Int, 18 },
        }}),
        // Method 'saveActiveLayout'
        QtMocHelpers::MethodData<void(const QString &, bool)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 20 }, { QMetaType::Bool, 21 },
        }}),
        // Method 'setAuthToken'
        QtMocHelpers::MethodData<void(const QString &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 23 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'apiBaseUrl'
        QtMocHelpers::PropertyData<QString>(24, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'activeLayout'
        QtMocHelpers::PropertyData<QVariantMap>(25, 0x80000000 | 26, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 1),
        // property 'loading'
        QtMocHelpers::PropertyData<bool>(27, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'errorFa'
        QtMocHelpers::PropertyData<QString>(28, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'Roles'
        QtMocHelpers::EnumData<enum Roles>(29, 29, QMC::EnumFlags{}).add({
            {   30, Roles::LayoutIdRole },
            {   31, Roles::NameFaRole },
            {   32, Roles::IsSharedRole },
            {   33, Roles::CellCountRole },
        }),
    };
    return QtMocHelpers::metaObjectData<GridModel, qt_meta_tag_ZN3vms9GridModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::GridModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9GridModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9GridModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms9GridModelE_t>.metaTypes,
    nullptr
} };

void vms::GridModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GridModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->apiBaseUrlChanged(); break;
        case 1: _t->activeLayoutChanged(); break;
        case 2: _t->loadingChanged(); break;
        case 3: _t->errorFaChanged(); break;
        case 4: _t->cellCameraChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 5: _t->reload(); break;
        case 6: _t->applyLayoutAt((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->applyPresetGrid((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 8: _t->assignCamera((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 9: _t->mergeCells((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 10: _t->saveActiveLayout((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 11: _t->setAuthToken((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GridModel::*)()>(_a, &GridModel::apiBaseUrlChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (GridModel::*)()>(_a, &GridModel::activeLayoutChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (GridModel::*)()>(_a, &GridModel::loadingChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (GridModel::*)()>(_a, &GridModel::errorFaChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (GridModel::*)(int , const QString & )>(_a, &GridModel::cellCameraChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->apiBaseUrl(); break;
        case 1: *reinterpret_cast<QVariantMap*>(_v) = _t->activeLayout(); break;
        case 2: *reinterpret_cast<bool*>(_v) = _t->loading(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->errorFa(); break;
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

const QMetaObject *vms::GridModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::GridModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms9GridModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int vms::GridModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void vms::GridModel::apiBaseUrlChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::GridModel::activeLayoutChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void vms::GridModel::loadingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void vms::GridModel::errorFaChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void vms::GridModel::cellCameraChanged(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}
QT_WARNING_POP
