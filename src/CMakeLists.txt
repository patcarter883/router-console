cmake_minimum_required(VERSION 3.24)
project(router-console LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Network)

qt_add_executable(router-console
    src/main.cpp
    src/AppEngine.cpp
)

qt_add_qml_module(router-console
    URI RouterConsole
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Connect.qml
)

target_link_libraries(router-console PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
)
