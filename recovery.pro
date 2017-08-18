#-------------------------------------------------
#
# Project created by QtCreator 2013-04-30T12:10:31
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = recovery
TEMPLATE = app
lessThan(QT_MAJOR_VERSION, 5): LIBS += -lqjson

system(sh updateqm.sh 2>/dev/null)

GIT_VERSION = $$system(git --git-dir $$PWD/.git --work-tree $$PWD describe --always --tags)

DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"

SOURCES += main.cpp\
        mainwindow.cpp \
    languagedialog.cpp \
    keydetection.cpp \
    progressslideshowdialog.cpp \
    json.cpp \
    multiimagewritethread.cpp \
    util.cpp \
    twoiconsdelegate.cpp 

HEADERS  += mainwindow.h \
    languagedialog.h \
    config.h \
    keydetection.h \
    mbr.h \
    progressslideshowdialog.h \
    json.h \
    multiimagewritethread.h \
    util.h \
    twoiconsdelegate.h 

target.path = /usr/bin
INSTALLS += target

FORMS    += mainwindow.ui \
    languagedialog.ui \
    progressslideshowdialog.ui

RESOURCES += \
    icons.qrc

TRANSLATIONS += translation_nl.ts \
    translation_de.ts \
    translation_pt.ts \
    translation_ja.ts \
    translation_fr.ts \
    translation_hu.ts \
    translation_fi.ts

OTHER_FILES += \
    README.txt
