cmake_minimum_required(VERSION 3.16)
project(LIB_Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Encontra os pacotes Qt6 necessários
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Configurações para que o Qt funcione corretamente com o CMake
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Define o nome do executável e lista todos os seus arquivos-fonte
add_executable(lib_engine
    main.cpp
    checkerboard.cpp
    resources.qrc
    damas_core.cpp
    damas_ai.cpp
)

# Vincula (linka) seu aplicativo com as bibliotecas Qt
# Nota: A IA usa a biblioteca zstd para EGTB. Certifique-se de que ela esteja instalada
target_link_libraries(lib_engine PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets zstd)