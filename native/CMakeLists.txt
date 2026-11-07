cmake_minimum_required(VERSION 3.22)

project(RosellaView
        VERSION 0.1
        LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(RV_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

include(cmake/openssl.cmake)
include(cmake/libplist.cmake)
include(cmake/airplay.cmake)

# Pure Node-API module: no Amazon SDK code is compiled in. Only the link stub
# for the OS's libkeplerscript.so (Node-API) is linked; the SDK's static
# turbomoduleAPI/JsonContainer/Logger archives are not.
add_library(RosellaView SHARED
  src/fmp4.c
  src/module.c
  src/receiver.c
  src/sps.c
)
target_include_directories(RosellaView PRIVATE ${RV_THIRD_PARTY}/node-api-headers/include)
target_compile_definitions(RosellaView PRIVATE _GNU_SOURCE)
set_target_properties(RosellaView PROPERTIES C_VISIBILITY_PRESET hidden)

find_package(turbomoduleAPI CONFIG REQUIRED)
target_link_libraries(RosellaView PRIVATE
  rosella::airplay
  turbomoduleAPI::napiStub
  pthread
)
# Keep the static libraries' symbols out of the dynamic symbol table.
target_link_options(RosellaView PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)

install(TARGETS RosellaView)
