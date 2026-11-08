cmake_minimum_required(VERSION 3.22)

project(RosellaSurface
        VERSION 0.1
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

# A native UI component, built the SDK's way (.claude/docs/DECISIONS.md D10): the IDL
# import library generates the interface glue for the OS's libapmf.so. No
# vendor static library is linked.
find_package(Apmf REQUIRED)
find_package(Kepler REQUIRED)

kepler_add_idl_import_library(rosella_surface_idl
  PACKAGES
  com.amazon.kepler.buffer
  com.amazon.kepler.buffer_queue
  com.amazon.kepler.graphics
  com.amazon.kepler.media_descriptor
  com.amazon.kepler.media_transform
  com.amazon.kepler.uitoolkit.graphics
  com.amazon.kepler.uitoolkit.view
  com.amazon.kepler.uitoolkit.react
)

# The runtime loads the component by its name: /xyz.regulad.rosellaview.surface
# → xyz.regulad.rosellaview.surface.so (react-native.config.js uiComponentName).
kepler_add_library(RosellaSurface SHARED
  src/RosellaSurface.cpp
  src/Decoder.cpp
)
set_target_properties(RosellaSurface PROPERTIES
  PREFIX ""
  OUTPUT_NAME xyz.regulad.rosellaview.surface)
# The bridge header is shared with the receiver (native/include).
target_include_directories(RosellaSurface PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../native/include)
target_link_libraries(RosellaSurface PRIVATE rosella_surface_idl pthread dl)

kepler_install_components(RosellaSurface IN_PROCESS "/xyz.regulad.rosellaview.surface")
