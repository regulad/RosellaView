# libplist (C library only, LGPL-2.1+) compiled straight from third_party/libplist.
# Upstream builds with autotools; the handful of config.h facts it needs are
# fixed for glibc and live in native/cmake/libplist-config.h.

set(_rv_plist ${RV_THIRD_PARTY}/libplist)

add_library(rosella_plist STATIC
  ${_rv_plist}/src/base64.c
  ${_rv_plist}/src/bplist.c
  ${_rv_plist}/src/bytearray.c
  ${_rv_plist}/src/common.c
  ${_rv_plist}/src/hashtable.c
  ${_rv_plist}/src/jplist.c
  ${_rv_plist}/src/jsmn.c
  ${_rv_plist}/src/oplist.c
  ${_rv_plist}/src/out-default.c
  ${_rv_plist}/src/out-limd.c
  ${_rv_plist}/src/out-plutil.c
  ${_rv_plist}/src/plist.c
  ${_rv_plist}/src/ptrarray.c
  ${_rv_plist}/src/time64.c
  ${_rv_plist}/src/xplist.c
  ${_rv_plist}/libcnary/cnary.c
  ${_rv_plist}/libcnary/node.c
  ${_rv_plist}/libcnary/node_list.c
)
target_include_directories(rosella_plist
  PUBLIC ${_rv_plist}/include
  PRIVATE ${_rv_plist}/src ${_rv_plist}/libcnary/include ${CMAKE_CURRENT_LIST_DIR}/libplist-config
)
target_compile_definitions(rosella_plist PRIVATE HAVE_CONFIG_H)
target_compile_options(rosella_plist PRIVATE -fPIC -fvisibility=hidden -w)
add_library(rosella::plist ALIAS rosella_plist)
