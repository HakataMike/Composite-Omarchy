QT += widgets testlib
CONFIG += c++17 testcase warn_on
TARGET = compositor-tests
TEMPLATE = app
SOURCES += tests.cpp ../src/document.cpp ../src/canvas.cpp ../src/window.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/window.h
