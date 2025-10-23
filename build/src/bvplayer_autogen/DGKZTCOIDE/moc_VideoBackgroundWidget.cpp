/****************************************************************************
** Meta object code from reading C++ file 'VideoBackgroundWidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.4)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../include/VideoBackgroundWidget.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'VideoBackgroundWidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.4. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_VideoBackgroundWidget_t {
    const uint offsetsAndSize[24];
    char stringdata0[150];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_VideoBackgroundWidget_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_VideoBackgroundWidget_t qt_meta_stringdata_VideoBackgroundWidget = {
    {
QT_MOC_LITERAL(0, 21), // "VideoBackgroundWidget"
QT_MOC_LITERAL(22, 11), // "mediaLoaded"
QT_MOC_LITERAL(34, 0), // ""
QT_MOC_LITERAL(35, 4), // "path"
QT_MOC_LITERAL(40, 20), // "playbackStateChanged"
QT_MOC_LITERAL(61, 7), // "playing"
QT_MOC_LITERAL(69, 16), // "playbackFinished"
QT_MOC_LITERAL(86, 15), // "positionChanged"
QT_MOC_LITERAL(102, 8), // "position"
QT_MOC_LITERAL(111, 8), // "duration"
QT_MOC_LITERAL(120, 16), // "processMpvEvents"
QT_MOC_LITERAL(137, 12) // "handleUpdate"

    },
    "VideoBackgroundWidget\0mediaLoaded\0\0"
    "path\0playbackStateChanged\0playing\0"
    "playbackFinished\0positionChanged\0"
    "position\0duration\0processMpvEvents\0"
    "handleUpdate"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_VideoBackgroundWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   50,    2, 0x06,    1 /* Public */,
       4,    1,   53,    2, 0x06,    3 /* Public */,
       6,    0,   56,    2, 0x06,    5 /* Public */,
       7,    2,   57,    2, 0x06,    6 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      10,    0,   62,    2, 0x08,    9 /* Private */,
      11,    0,   63,    2, 0x08,   10 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::Bool,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,    8,    9,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void VideoBackgroundWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<VideoBackgroundWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->mediaLoaded((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->playbackStateChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 2: _t->playbackFinished(); break;
        case 3: _t->positionChanged((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 4: _t->processMpvEvents(); break;
        case 5: _t->handleUpdate(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (VideoBackgroundWidget::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VideoBackgroundWidget::mediaLoaded)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (VideoBackgroundWidget::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VideoBackgroundWidget::playbackStateChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (VideoBackgroundWidget::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VideoBackgroundWidget::playbackFinished)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (VideoBackgroundWidget::*)(double , double );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&VideoBackgroundWidget::positionChanged)) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject VideoBackgroundWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_meta_stringdata_VideoBackgroundWidget.offsetsAndSize,
    qt_meta_data_VideoBackgroundWidget,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_VideoBackgroundWidget_t
, QtPrivate::TypeAndForceComplete<VideoBackgroundWidget, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QString &, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>


>,
    nullptr
} };


const QMetaObject *VideoBackgroundWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VideoBackgroundWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_VideoBackgroundWidget.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QOpenGLFunctions"))
        return static_cast< QOpenGLFunctions*>(this);
    return QOpenGLWidget::qt_metacast(_clname);
}

int VideoBackgroundWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QOpenGLWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void VideoBackgroundWidget::mediaLoaded(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void VideoBackgroundWidget::playbackStateChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void VideoBackgroundWidget::playbackFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void VideoBackgroundWidget::positionChanged(double _t1, double _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
