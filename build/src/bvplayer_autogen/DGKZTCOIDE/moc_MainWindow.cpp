/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.4)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../include/MainWindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.4. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    const uint offsetsAndSize[44];
    char stringdata0[368];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 10), // "MainWindow"
QT_MOC_LITERAL(11, 14), // "handleAddMedia"
QT_MOC_LITERAL(26, 0), // ""
QT_MOC_LITERAL(27, 15), // "handlePlayPause"
QT_MOC_LITERAL(43, 14), // "handlePlayNext"
QT_MOC_LITERAL(58, 18), // "handlePlayPrevious"
QT_MOC_LITERAL(77, 23), // "handlePlaylistActivated"
QT_MOC_LITERAL(101, 16), // "QListWidgetItem*"
QT_MOC_LITERAL(118, 4), // "item"
QT_MOC_LITERAL(123, 20), // "handleShuffleToggled"
QT_MOC_LITERAL(144, 16), // "handleRepeatMode"
QT_MOC_LITERAL(161, 26), // "handlePlaybackStateChanged"
QT_MOC_LITERAL(188, 7), // "playing"
QT_MOC_LITERAL(196, 22), // "handlePlaybackFinished"
QT_MOC_LITERAL(219, 19), // "handleVolumeChanged"
QT_MOC_LITERAL(239, 5), // "value"
QT_MOC_LITERAL(245, 27), // "handleProgressSliderPressed"
QT_MOC_LITERAL(273, 28), // "handleProgressSliderReleased"
QT_MOC_LITERAL(302, 25), // "handleProgressSliderMoved"
QT_MOC_LITERAL(328, 21), // "handlePositionChanged"
QT_MOC_LITERAL(350, 8), // "position"
QT_MOC_LITERAL(359, 8) // "duration"

    },
    "MainWindow\0handleAddMedia\0\0handlePlayPause\0"
    "handlePlayNext\0handlePlayPrevious\0"
    "handlePlaylistActivated\0QListWidgetItem*\0"
    "item\0handleShuffleToggled\0handleRepeatMode\0"
    "handlePlaybackStateChanged\0playing\0"
    "handlePlaybackFinished\0handleVolumeChanged\0"
    "value\0handleProgressSliderPressed\0"
    "handleProgressSliderReleased\0"
    "handleProgressSliderMoved\0"
    "handlePositionChanged\0position\0duration"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   98,    2, 0x08,    1 /* Private */,
       3,    0,   99,    2, 0x08,    2 /* Private */,
       4,    0,  100,    2, 0x08,    3 /* Private */,
       5,    0,  101,    2, 0x08,    4 /* Private */,
       6,    1,  102,    2, 0x08,    5 /* Private */,
       9,    0,  105,    2, 0x08,    7 /* Private */,
      10,    0,  106,    2, 0x08,    8 /* Private */,
      11,    1,  107,    2, 0x08,    9 /* Private */,
      13,    0,  110,    2, 0x08,   11 /* Private */,
      14,    1,  111,    2, 0x08,   12 /* Private */,
      16,    0,  114,    2, 0x08,   14 /* Private */,
      17,    0,  115,    2, 0x08,   15 /* Private */,
      18,    1,  116,    2, 0x08,   16 /* Private */,
      19,    2,  119,    2, 0x08,   18 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7,    8,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   15,
    QMetaType::Void, QMetaType::Double, QMetaType::Double,   20,   21,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->handleAddMedia(); break;
        case 1: _t->handlePlayPause(); break;
        case 2: _t->handlePlayNext(); break;
        case 3: _t->handlePlayPrevious(); break;
        case 4: _t->handlePlaylistActivated((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 5: _t->handleShuffleToggled(); break;
        case 6: _t->handleRepeatMode(); break;
        case 7: _t->handlePlaybackStateChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->handlePlaybackFinished(); break;
        case 9: _t->handleVolumeChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->handleProgressSliderPressed(); break;
        case 11: _t->handleProgressSliderReleased(); break;
        case 12: _t->handleProgressSliderMoved((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->handlePositionChanged((*reinterpret_cast< std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.offsetsAndSize,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_MainWindow_t
, QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>, QtPrivate::TypeAndForceComplete<double, std::false_type>


>,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
