QT += core gui qml quick quickcontrols2 network dbus opengl svg

CONFIG += c++17 release
TARGET = needleeye
TEMPLATE = app

LIBS += -lzip -lGL

HEADERS += \
    src/backend.h \
    src/czmlparser.h \
    src/globeview.h \
    src/kmlparser.h \
    src/portalfilepicker.h \
    src/theme.h \
    src/tilemanager.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/czmlparser.cpp \
    src/globeview.cpp \
    src/kmlparser.cpp \
    src/portalfilepicker.cpp \
    src/theme.cpp \
    src/tilemanager.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
