# UxPlay's protocol library (lib/: "libairplay"), with its built-in mDNS responder
# (lib/mdnsd), llhttp and playfair. renderers/ and uxplay.cpp (GStreamer, X11,
# the CLI) are not built; src/receiver.c takes uxplay.cpp's place.

set(_rv_ux ${RV_THIRD_PARTY}/UxPlay/lib)

file(GLOB _rv_airplay_sources CONFIGURE_DEPENDS
  ${_rv_ux}/*.c
  ${_rv_ux}/mdnsd/*.c
  ${_rv_ux}/llhttp/*.c
  ${_rv_ux}/playfair/*.c
)

add_library(rosella_airplay STATIC ${_rv_airplay_sources})
target_include_directories(rosella_airplay
  PUBLIC ${_rv_ux}
  PRIVATE ${_rv_ux}/playfair ${_rv_ux}/llhttp ${_rv_ux}/mdnsd
)
# Mirrors UxPlay's own Linux flags (lib/CMakeLists.txt). EXTERNAL_DNS_SD stays
# undefined so lib/mdnsd is used. libplist >= 2.3 enables both PLIST_* paths.
target_compile_definitions(rosella_airplay PRIVATE
  STANDALONE __STDC_CONSTANT_MACROS __STDC_LIMIT_MACROS TARGET_POSIX _LINUX
  PIC _REENTRANT _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64 NOHOLD
  PLIST_210 PLIST_230
)
target_compile_options(rosella_airplay PRIVATE -fPIC -O2 -fvisibility=hidden -w)
target_link_libraries(rosella_airplay PUBLIC rosella::plist rosella::crypto pthread m)
add_library(rosella::airplay ALIAS rosella_airplay)
