/****************************************************************************
** Meta object code from reading C++ file 'shamsi_formatter.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/i18n/shamsi_formatter.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'shamsi_formatter.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms15ShamsiFormatterE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::ShamsiFormatter::qt_create_metaobjectdata<qt_meta_tag_ZN3vms15ShamsiFormatterE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::ShamsiFormatter",
        "nowShamsi",
        "",
        "toShamsi",
        "utcMicroseconds",
        "toShamsiShort",
        "toShamsiDate",
        "toPersianDigits",
        "text",
        "shamsiDateToUtcUs",
        "jy",
        "jm",
        "jd",
        "shamsiYear",
        "shamsiMonth",
        "shamsiDay",
        "shamsiMonthName"
    };

    QtMocHelpers::UintData qt_methods {
        // Method 'nowShamsi'
        QtMocHelpers::MethodData<QString() const>(1, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'toShamsi'
        QtMocHelpers::MethodData<QString(qulonglong) const>(3, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'toShamsiShort'
        QtMocHelpers::MethodData<QString(qulonglong) const>(5, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'toShamsiDate'
        QtMocHelpers::MethodData<QString(qulonglong) const>(6, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'toPersianDigits'
        QtMocHelpers::MethodData<QString(const QString &) const>(7, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::QString, 8 },
        }}),
        // Method 'shamsiDateToUtcUs'
        QtMocHelpers::MethodData<qulonglong(int, int, int) const>(9, 2, QMC::AccessPublic, QMetaType::ULongLong, {{
            { QMetaType::Int, 10 }, { QMetaType::Int, 11 }, { QMetaType::Int, 12 },
        }}),
        // Method 'shamsiYear'
        QtMocHelpers::MethodData<int(qulonglong) const>(13, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'shamsiMonth'
        QtMocHelpers::MethodData<int(qulonglong) const>(14, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'shamsiDay'
        QtMocHelpers::MethodData<int(qulonglong) const>(15, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'shamsiMonthName'
        QtMocHelpers::MethodData<QString(int) const>(16, 2, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 11 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ShamsiFormatter, qt_meta_tag_ZN3vms15ShamsiFormatterE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::ShamsiFormatter::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15ShamsiFormatterE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15ShamsiFormatterE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms15ShamsiFormatterE_t>.metaTypes,
    nullptr
} };

void vms::ShamsiFormatter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ShamsiFormatter *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: { QString _r = _t->nowShamsi();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 1: { QString _r = _t->toShamsi((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 2: { QString _r = _t->toShamsiShort((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 3: { QString _r = _t->toShamsiDate((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 4: { QString _r = _t->toPersianDigits((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 5: { qulonglong _r = _t->shamsiDateToUtcUs((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3])));
            if (_a[0]) *reinterpret_cast<qulonglong*>(_a[0]) = std::move(_r); }  break;
        case 6: { int _r = _t->shamsiYear((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 7: { int _r = _t->shamsiMonth((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 8: { int _r = _t->shamsiDay((*reinterpret_cast<std::add_pointer_t<qulonglong>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 9: { QString _r = _t->shamsiMonthName((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *vms::ShamsiFormatter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::ShamsiFormatter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms15ShamsiFormatterE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int vms::ShamsiFormatter::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
    return _id;
}
QT_WARNING_POP
