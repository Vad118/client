#-------------------------------------------------
#
# Project created by QtCreator 2014-05-08T14:48:01
#
#-------------------------------------------------

QT       += core

QT       -= gui

TARGET = client
CONFIG   += console
CONFIG   -= app_bundle
CONFIG += static

TEMPLATE = app


SOURCES += \
    worker.cpp

INCLUDEPATH += "C:/Program Files (x86)/Lua/5.1/include/"

LIBS += "C:/Program Files (x86)/Lua/5.1/lib/lua51.dll" \
        "C:/Program Files (x86)/Lua/5.1/lib/lua5.1.dll"

LIBS     += -lws2_32
LIBS += -static-libgcc
QMAKE_LFLAGS = -static
