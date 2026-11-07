/* config.h for libplist on Vega OS (glibc 2.35). Replaces the autotools-generated file. */
#ifndef ROSELLA_LIBPLIST_CONFIG_H
#define ROSELLA_LIBPLIST_CONFIG_H

#define PACKAGE_VERSION "2.8.1"

#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1

#define HAVE_STRDUP 1
#define HAVE_STRNDUP 1
#define HAVE_STRERROR 1
#define HAVE_GMTIME_R 1
#define HAVE_LOCALTIME_R 1
#define HAVE_TIMEGM 1
#define HAVE_STRPTIME 1
#define HAVE_MEMMEM 1

#define HAVE_TM_TM_GMTOFF 1
#define HAVE_TM_TM_ZONE 1

#define HAVE_FVISIBILITY 1

#define __LITTLE_ENDIAN__ 1

#endif
