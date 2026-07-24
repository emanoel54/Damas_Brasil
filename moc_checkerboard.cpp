/****************************************************************************
** Meta object code from reading C++ file 'checkerboard.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "checkerboard.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'checkerboard.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_CheckerBoard_t {
    uint offsetsAndSizes[50];
    char stringdata0[13];
    char stringdata1[17];
    char stringdata2[1];
    char stringdata3[12];
    char stringdata4[13];
    char stringdata5[9];
    char stringdata6[9];
    char stringdata7[13];
    char stringdata8[13];
    char stringdata9[15];
    char stringdata10[9];
    char stringdata11[6];
    char stringdata12[12];
    char stringdata13[9];
    char stringdata14[9];
    char stringdata15[19];
    char stringdata16[9];
    char stringdata17[5];
    char stringdata18[5];
    char stringdata19[12];
    char stringdata20[9];
    char stringdata21[5];
    char stringdata22[11];
    char stringdata23[6];
    char stringdata24[19];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CheckerBoard_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CheckerBoard_t qt_meta_stringdata_CheckerBoard = {
    {
        QT_MOC_LITERAL(0, 12),  // "CheckerBoard"
        QT_MOC_LITERAL(13, 16),  // "setupModeChanged"
        QT_MOC_LITERAL(30, 0),  // ""
        QT_MOC_LITERAL(31, 11),  // "inSetupMode"
        QT_MOC_LITERAL(43, 12),  // "startNewGame"
        QT_MOC_LITERAL(56, 8),  // "saveGame"
        QT_MOC_LITERAL(65, 8),  // "openGame"
        QT_MOC_LITERAL(74, 12),  // "savePosition"
        QT_MOC_LITERAL(87, 12),  // "openPosition"
        QT_MOC_LITERAL(100, 14),  // "enterSetupMode"
        QT_MOC_LITERAL(115, 8),  // "setScale"
        QT_MOC_LITERAL(124, 5),  // "scale"
        QT_MOC_LITERAL(130, 11),  // "rotateBoard"
        QT_MOC_LITERAL(142, 8),  // "undoMove"
        QT_MOC_LITERAL(151, 8),  // "redoMove"
        QT_MOC_LITERAL(160, 18),  // "setUndoRedoActions"
        QT_MOC_LITERAL(179, 8),  // "QAction*"
        QT_MOC_LITERAL(188, 4),  // "undo"
        QT_MOC_LITERAL(193, 4),  // "redo"
        QT_MOC_LITERAL(198, 11),  // "setGameMode"
        QT_MOC_LITERAL(210, 8),  // "GameMode"
        QT_MOC_LITERAL(219, 4),  // "mode"
        QT_MOC_LITERAL(224, 10),  // "setAiLevel"
        QT_MOC_LITERAL(235, 5),  // "level"
        QT_MOC_LITERAL(241, 18)   // "onSetupModeChanged"
    },
    "CheckerBoard",
    "setupModeChanged",
    "",
    "inSetupMode",
    "startNewGame",
    "saveGame",
    "openGame",
    "savePosition",
    "openPosition",
    "enterSetupMode",
    "setScale",
    "scale",
    "rotateBoard",
    "undoMove",
    "redoMove",
    "setUndoRedoActions",
    "QAction*",
    "undo",
    "redo",
    "setGameMode",
    "GameMode",
    "mode",
    "setAiLevel",
    "level",
    "onSetupModeChanged"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CheckerBoard[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      15,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  104,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       4,    0,  107,    2, 0x0a,    3 /* Public */,
       5,    0,  108,    2, 0x0a,    4 /* Public */,
       6,    0,  109,    2, 0x0a,    5 /* Public */,
       7,    0,  110,    2, 0x0a,    6 /* Public */,
       8,    0,  111,    2, 0x0a,    7 /* Public */,
       9,    0,  112,    2, 0x0a,    8 /* Public */,
      10,    1,  113,    2, 0x0a,    9 /* Public */,
      12,    0,  116,    2, 0x0a,   11 /* Public */,
      13,    0,  117,    2, 0x0a,   12 /* Public */,
      14,    0,  118,    2, 0x0a,   13 /* Public */,
      15,    2,  119,    2, 0x0a,   14 /* Public */,
      19,    1,  124,    2, 0x0a,   17 /* Public */,
      22,    1,  127,    2, 0x0a,   19 /* Public */,
      24,    1,  130,    2, 0x08,   21 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Double,   11,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 16, 0x80000000 | 16,   17,   18,
    QMetaType::Void, 0x80000000 | 20,   21,
    QMetaType::Void, QMetaType::Int,   23,
    QMetaType::Void, QMetaType::Bool,    3,

       0        // eod
};

Q_CONSTINIT const QMetaObject CheckerBoard::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_CheckerBoard.offsetsAndSizes,
    qt_meta_data_CheckerBoard,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CheckerBoard_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CheckerBoard, std::true_type>,
        // method 'setupModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'startNewGame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'saveGame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openGame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'savePosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openPosition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'enterSetupMode'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setScale'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'rotateBoard'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'undoMove'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'redoMove'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setUndoRedoActions'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QAction *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QAction *, std::false_type>,
        // method 'setGameMode'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<GameMode, std::false_type>,
        // method 'setAiLevel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSetupModeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void CheckerBoard::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CheckerBoard *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->setupModeChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->startNewGame(); break;
        case 2: _t->saveGame(); break;
        case 3: _t->openGame(); break;
        case 4: _t->savePosition(); break;
        case 5: _t->openPosition(); break;
        case 6: _t->enterSetupMode(); break;
        case 7: _t->setScale((*reinterpret_cast< std::add_pointer_t<double>>(_a[1]))); break;
        case 8: _t->rotateBoard(); break;
        case 9: _t->undoMove(); break;
        case 10: _t->redoMove(); break;
        case 11: _t->setUndoRedoActions((*reinterpret_cast< std::add_pointer_t<QAction*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QAction*>>(_a[2]))); break;
        case 12: _t->setGameMode((*reinterpret_cast< std::add_pointer_t<GameMode>>(_a[1]))); break;
        case 13: _t->setAiLevel((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 14: _t->onSetupModeChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CheckerBoard::*)(bool );
            if (_t _q_method = &CheckerBoard::setupModeChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *CheckerBoard::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CheckerBoard::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CheckerBoard.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CheckerBoard::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void CheckerBoard::setupModeChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
