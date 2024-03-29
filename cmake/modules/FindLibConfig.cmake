# GPLv3 from https://github.com/schnorr/pajeng/blob/master/cmake/FindLibConfig.cmake
# TODO clarify copyright

# Find the CUnit includes and library
#
# This module defines
# LIBCONFIG_INCLUDE_DIR, where to find cppunit include files, etc.
# LIBCONFIG_LIBRARIES, the libraries to link against to use CppUnit.
# LIBCONFIG_STATIC_LIBRARIY_PATH
# LIBCONFIG_FOUND, If false, do not try to use CppUnit.

# also defined, but not for general use are
# LIBCONFIG_LIBRARY, where to find the CUnit library.

#MESSAGE("Searching for libconfig library")

FIND_PATH(LIBCONFIG_INCLUDE_DIR libconfig.h
  /usr/local/include
  /usr/include
)

FIND_PATH(LIBCONFIGPP_INCLUDE_DIR libconfig.h++
  /usr/local/include
  /usr/include
)

FIND_LIBRARY(LIBCONFIG_LIBRARY config
  /usr/local/lib
  /usr/lib
)

FIND_LIBRARY(LIBCONFIGPP_LIBRARY config++
  /usr/local/lib
  /usr/lib
)

FIND_LIBRARY(LIBCONFIG_STATIC_LIBRARY "libconfig${CMAKE_STATIC_LIBRARY_SUFFIX}"
  /usr/local/lib
  /usr/lib
)

FIND_LIBRARY(LIBCONFIGPP_STATIC_LIBRARY "libconfig++${CMAKE_STATIC_LIBRARY_SUFFIX}"
    /usr/local/lib
    /usr/lib
)


IF(LIBCONFIG_INCLUDE_DIR)
  IF(LIBCONFIG_LIBRARY)
    SET(LIBCONFIG_FOUND TRUE)
    SET(LIBCONFIG_LIBRARIES ${LIBCONFIG_LIBRARY})
    SET(LIBCONFIG_STATIC_LIBRARY_PATH ${LIBCONFIG_STATIC_LIBRARY})
  ENDIF(LIBCONFIG_LIBRARY)
ENDIF(LIBCONFIG_INCLUDE_DIR)

IF(LIBCONFIGPP_INCLUDE_DIR)
  IF(LIBCONFIGPP_LIBRARY)
    SET(LIBCONFIGPP_FOUND TRUE)
    SET(LIBCONFIGPP_LIBRARIES ${LIBCONFIGPP_LIBRARY})
    SET(LIBCONFIGPP_STATIC_LIBRARY_PATH ${LIBCONFIGPP_STATIC_LIBRARY})
  ENDIF(LIBCONFIGPP_LIBRARY)
ENDIF(LIBCONFIGPP_INCLUDE_DIR)

IF (LIBCONFIG_FOUND)
   IF (NOT LibConfig_FIND_QUIETLY)
      MESSAGE(STATUS "Found LibConfig++: ${LIBCONFIGPP_LIBRARIES}" )
      MESSAGE(STATUS "Found LibConfig: ${LIBCONFIG_LIBRARIES}")
      MESSAGE(STATUS "static LibConfig path: ${LIBCONFIG_STATIC_LIBRARY_PATH}")
   ENDIF (NOT LibConfig_FIND_QUIETLY)
ELSE (LIBCONFIG_FOUND)
   IF (LibConfig_FIND_REQUIRED)
      MESSAGE(SEND_ERROR "Could NOT find LibConfig")
   ENDIF (LibConfig_FIND_REQUIRED)
ENDIF (LIBCONFIG_FOUND)

MARK_AS_ADVANCED(LIBCONFIG_INCLUDE_DIR LIBCONFIG_LIBRARIES)