/****************************************************************************
** Meta object code from reading C++ file 'video_decoder.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/stream/video_decoder.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'video_decoder.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3vms12VideoDecoderE_t {};
} // unnamed namespace

template <> constexpr inline auto vms::VideoDecoder::qt_create_metaobjectdata<qt_meta_tag_ZN3vms12VideoDecoderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "vms::VideoDecoder",
        "decodingChanged",
        "",
        "framePresented",
        "utcUs",
        "keyframeAligned",
        "statusFaChanged",
        "startLive",
        "url",
        "QVideoSink*",
        "sink",
        "startPlayback",
        "startUtcUs",
        "switchTo",
        "seekTo",
        "setPaused",
        "paused",
        "setRate",
        "rate",
        "stop",
        "statusFa"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'decodingChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'framePresented'
        QtMocHelpers::SignalData<void(quint64)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Signal 'keyframeAligned'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'statusFaChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'startLive'
        QtMocHelpers::MethodData<void(const QString &, QVideoSink *)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { 0x80000000 | 9, 10 },
        }}),
        // Method 'startPlayback'
        QtMocHelpers::MethodData<void(const QString &, QVideoSink *, quint64)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 }, { 0x80000000 | 9, 10 }, { QMetaType::ULongLong, 12 },
        }}),
        // Method 'switchTo'
        QtMocHelpers::MethodData<void(const QString &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 8 },
        }}),
        // Method 'seekTo'
        QtMocHelpers::MethodData<void(quint64)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 4 },
        }}),
        // Method 'setPaused'
        QtMocHelpers::MethodData<void(bool)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 16 },
        }}),
        // Method 'setRate'
        QtMocHelpers::MethodData<void(double)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 18 },
        }}),
        // Method 'stop'
        QtMocHelpers::MethodData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'statusFa'
        QtMocHelpers::PropertyData<QString>(20, QMetaType::QString, QMC::DefaultPropertyFlags, 3),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<VideoDecoder, qt_meta_tag_ZN3vms12VideoDecoderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject vms::VideoDecoder::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12VideoDecoderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12VideoDecoderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3vms12VideoDecoderE_t>.metaTypes,
    nullptr
} };

void vms::VideoDecoder::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VideoDecoder *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->decodingChanged(); break;
        case 1: _t->framePresented((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1]))); break;
        case 2: _t->keyframeAligned(); break;
        case 3: _t->statusFaChanged(); break;
        case 4: _t->startLive((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVideoSink*>>(_a[2]))); break;
        case 5: _t->startPlayback((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVideoSink*>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<quint64>>(_a[3]))); break;
        case 6: _t->switchTo((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->seekTo((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1]))); break;
        case 8: _t->setPaused((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 9: _t->setRate((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 10: _t->stop(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QVideoSink* >(); break;
            }
            break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QVideoSink* >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (VideoDecoder::*)()>(_a, &VideoDecoder::decodingChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (VideoDecoder::*)(quint64 )>(_a, &VideoDecoder::framePresented, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (VideoDecoder::*)()>(_a, &VideoDecoder::keyframeAligned, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (VideoDecoder::*)()>(_a, &VideoDecoder::statusFaChanged, 3))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QString*>(_v) = _t->statusFa(); break;
        default: break;
        }
    }
}

const QMetaObject *vms::VideoDecoder::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *vms::VideoDecoder::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3vms12VideoDecoderE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int vms::VideoDecoder::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
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
void vms::VideoDecoder::decodingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void vms::VideoDecoder::framePresented(quint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void vms::VideoDecoder::keyframeAligned()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void vms::VideoDecoder::statusFaChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
