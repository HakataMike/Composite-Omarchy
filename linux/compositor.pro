QT += widgets
CONFIG += c++17 warn_on
TARGET = compositor-arc
TEMPLATE = app
SOURCES += src/main.cpp src/document.cpp src/canvas.cpp src/window.cpp
HEADERS += src/document.h src/canvas.h src/window.h
SOURCES += src/painting.cpp
HEADERS += src/painting.h
