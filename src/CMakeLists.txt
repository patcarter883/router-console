set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Network)

add_library(router-console-core STATIC
    AppEngine.cpp
    DiscoveredHostsModel.cpp
    protocol/MdnsDiscovery.cpp
    protocol/UbusClient.cpp
)

target_include_directories(router-console-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(router-console-core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(router-console
    main.cpp
)

qt_add_qml_module(router-console
    URI RouterConsole
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Connect.qml
)

target_link_libraries(router-console PRIVATE
    router-console-core
    Qt6::Quick
    Qt6::QuickControls2
)
