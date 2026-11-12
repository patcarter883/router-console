set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Network)

add_library(router-console-core STATIC
    AppEngine.cpp
    models/InterfacesModel.cpp
    config/ConfigEditor.cpp
    config/ConfigModels.cpp
    config/DomainRegistry.cpp
    DiscoveredHostsModel.cpp
    plugins/PluginHttpFetcher.cpp
    plugins/PluginHub.cpp
    plugins/PluginSession.cpp
    plugins/PluginSpec.cpp
    protocol/MdnsDiscovery.cpp
    protocol/UbusClient.cpp
    protocol/UciCodec.cpp
)

target_include_directories(router-console-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(router-console-core PUBLIC Qt6::Core Qt6::Network)

# Dev runs execute build/router-console, so the plugin root the sources own is
# the fallback search path (plugins/ next to the binary wins when present).
target_compile_definitions(router-console-core PRIVATE
    RC_SOURCE_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/plugins")

qt_add_executable(router-console
    main.cpp
)

qt_add_qml_module(router-console
    URI RouterConsole
    VERSION 1.0
    QML_FILES
        qml/ConfigWindow.qml
        qml/InterfaceWindow.qml
        qml/Main.qml
        qml/Connect.qml
        qml/PluginWindow.qml
        qml/TrafficGraph.qml
)

target_link_libraries(router-console PRIVATE
    router-console-core
    Qt6::Quick
    Qt6::QuickControls2
)
