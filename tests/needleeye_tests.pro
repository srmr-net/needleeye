QT += core gui quick quickcontrols2 network testlib dbus opengl

CONFIG += c++17 testcase
TARGET = needleeye_tests
TEMPLATE = app

LIBS += -lzip -lGL
INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/czmlparser.h \
    ../src/kmlparser.h \
    ../src/portalfilepicker.h \
    ../src/theme.h \
    ../src/tilemanager.h \
    ../src/translator.h

SOURCES += \
    needleeye_tests.cpp \
    ../src/backend.cpp \
    ../src/czmlparser.cpp \
    ../src/kmlparser.cpp \
    ../src/portalfilepicker.cpp \
    ../src/theme.cpp \
    ../src/tilemanager.cpp \
    ../src/translator.cpp
