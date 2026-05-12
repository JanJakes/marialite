# Copyright (c) 2026, MyLite contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/include
  ${CMAKE_SOURCE_DIR}/libmysqld
  ${CMAKE_SOURCE_DIR}/mylite/include
  ${CMAKE_SOURCE_DIR}/sql
  ${CMAKE_BINARY_DIR}/sql
  ${PCRE_INCLUDE_DIRS}
)

ADD_DEFINITIONS(-DEMBEDDED_LIBRARY -UMYSQL_CLIENT)

ADD_LIBRARY(libmylite STATIC
  mylite.cc
)
TARGET_INCLUDE_DIRECTORIES(libmylite PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
TARGET_COMPILE_DEFINITIONS(libmylite PRIVATE
  MYLITE_DEFAULT_LC_MESSAGES_DIR="${CMAKE_BINARY_DIR}/sql/share"
)
SET_TARGET_PROPERTIES(libmylite PROPERTIES
  OUTPUT_NAME mylite
)
TARGET_LINK_LIBRARIES(libmylite mysqlserver tpool)

MYSQL_ADD_EXECUTABLE(mylite-embedded-bootstrap-smoke
  bootstrap_smoke.cc
  COMPONENT Test
)
TARGET_LINK_LIBRARIES(mylite-embedded-bootstrap-smoke mysqlserver tpool)

MYSQL_ADD_EXECUTABLE(mylite-open-close-smoke
  open_close_smoke.cc
  COMPONENT Test
)
TARGET_LINK_LIBRARIES(mylite-open-close-smoke libmylite)

MYSQL_ADD_EXECUTABLE(mylite-digest-smoke
  digest_smoke.cc
  COMPONENT Test
)
TARGET_LINK_LIBRARIES(mylite-digest-smoke mysys_ssl)

MYSQL_ADD_EXECUTABLE(mylite-storage-engine-smoke
  storage_engine_smoke.cc
  COMPONENT Test
)
TARGET_LINK_LIBRARIES(mylite-storage-engine-smoke mysqlserver tpool)

MYSQL_ADD_EXECUTABLE(mylite-compatibility-smoke
  compatibility_smoke.cc
  COMPONENT Test
)
TARGET_LINK_LIBRARIES(mylite-compatibility-smoke mysqlserver tpool)
