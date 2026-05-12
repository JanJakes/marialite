# Copyright (c) 2012, Oracle and/or its affiliates. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335 USA

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/mysys_ssl
                    ${SSL_INCLUDE_DIRS})

IF(SSL_DEFINES)
ADD_DEFINITIONS(${SSL_DEFINES})
ENDIF()

OPTION(MYLITE_DISABLE_OPENSSL_DIGESTS
  "Disable OpenSSL-backed digest wrappers in the MyLite minsize profile" OFF)

IF(MYLITE_DISABLE_OPENSSL_DIGESTS AND
   (NOT MYLITE_DISABLE_VIO_SSL OR
    NOT MYLITE_DISABLE_DYNAMIC_PLUGIN_LOADING OR
    NOT MYLITE_DISABLE_SQL_CRYPTO_FUNCTIONS OR
    NOT MYLITE_DISABLE_SERVER_ENCRYPTION OR
    NOT MYLITE_DISABLE_DES_FUNCTIONS OR
    NOT MYLITE_DISABLE_KDF_FUNCTION))
  MESSAGE(FATAL_ERROR
    "MYLITE_DISABLE_OPENSSL_DIGESTS requires the minsize profile to disable "
    "VIO SSL, dynamic plugin loading, SQL crypto functions, server "
    "encryption, DES functions, and KDF")
ENDIF()

IF(MYLITE_DISABLE_OPENSSL_DIGESTS)
  SET(MYSYS_SSL_HIDDEN_SOURCES
      mylite_digest.cc
     )
ELSE()
  SET(MYSYS_SSL_HIDDEN_SOURCES
      my_sha1.cc
      my_sha224.cc
      my_sha256.cc
      my_sha384.cc
      my_sha512.cc
      my_md5.cc
      openssl.c
     )
ENDIF()

SET(MYSYS_SSL_SOURCES ${MYSYS_SSL_HIDDEN_SOURCES})
IF(NOT MYLITE_DISABLE_OPENSSL_DIGESTS)
  LIST(APPEND MYSYS_SSL_SOURCES my_crypt.cc)
ENDIF()

# In order to get correct symbol visibility, these files
# must be compiled with "-fvisibility=hidden"
IF(WITH_SSL STREQUAL "bundled" AND HAVE_VISIBILITY_HIDDEN)
  SET_SOURCE_FILES_PROPERTIES(
    ${MYSYS_SSL_HIDDEN_SOURCES}
    PROPERTIES COMPILE_FLAGS "-fvisibility=hidden")
ENDIF()

ADD_CONVENIENCE_LIBRARY(mysys_ssl ${MYSYS_SSL_SOURCES})
IF(MYLITE_DISABLE_OPENSSL_DIGESTS)
  TARGET_LINK_LIBRARIES(mysys_ssl dbug strings)
ELSEIF(MYLITE_DISABLE_VIO_SSL AND OPENSSL_CRYPTO_LIBRARY)
  TARGET_LINK_LIBRARIES(mysys_ssl dbug strings ${OPENSSL_CRYPTO_LIBRARY})
ELSE()
  TARGET_LINK_LIBRARIES(mysys_ssl dbug strings ${SSL_LIBRARIES})
ENDIF()
DTRACE_INSTRUMENT(mysys_ssl)
