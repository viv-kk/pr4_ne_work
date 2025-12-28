cmake_minimum_required(VERSION 3.10)
project(NoSQL_DBMS_Network)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -Wextra")

# Общие файлы для всех проектов
set(COMMON_SOURCES
    JsonParser.cpp
    network_protocol.cpp
)

# === Сервер БД (db_server) ===
# Проверяем, есть ли необходимые файлы
set(SERVER_SOURCES
    db_server.cpp
    database.cpp
    collection.cpp
    document.cpp
    QueryCondition.cpp
)

# Проверяем существование файлов
set(ALL_SERVER_SOURCES_FOUND TRUE)
foreach(source_file ${SERVER_SOURCES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source_file})
        message(WARNING "Server source file not found: ${source_file}")
        set(ALL_SERVER_SOURCES_FOUND FALSE)
    endif()
endforeach()

if(ALL_SERVER_SOURCES_FOUND)
    add_executable(db_server
        server_main.cpp
        ${SERVER_SOURCES}
        ${COMMON_SOURCES}
    )
    
    target_include_directories(db_server PRIVATE .)
    target_compile_options(db_server PRIVATE -pthread)
    target_link_libraries(db_server pthread)
    
    message(STATUS "DB server target added")
else()
    message(WARNING "DB server target NOT added - missing source files")
endif()

# === Клиент БД (db_client) ===
set(CLIENT_SOURCES
    db_client.cpp
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/client_main.cpp AND 
   EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/db_client.cpp)
    add_executable(db_client
        client_main.cpp
        ${CLIENT_SOURCES}
        ${COMMON_SOURCES}
    )
    
    target_include_directories(db_client PRIVATE .)
    target_compile_options(db_client PRIVATE -pthread)
    target_link_libraries(db_client pthread)
    
    message(STATUS "DB client target added")
else()
    message(WARNING "DB client target NOT added - missing source files")
endif()

# === SIEM Agent ===
set(SIEM_SOURCES
    siem_agent.cpp
    siem_agent_main.cpp
    event_processor.cpp      # <-- Добавьте этот файл
    inotify_wrapper.cpp      # <-- Добавьте этот файл
    persistent_buffer.cpp    # <-- Добавьте этот файл
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/siem_agent.cpp AND 
   EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/siem_agent_main.cpp)
    add_executable(siem_agent
        ${SIEM_SOURCES}
        ${CLIENT_SOURCES}
        ${COMMON_SOURCES}
    )
    
    target_include_directories(siem_agent PRIVATE .)
    target_compile_options(siem_agent PRIVATE -pthread)
    target_link_libraries(siem_agent pthread)
    
    message(STATUS "SIEM agent target added")
else()
    message(WARNING "SIEM agent target NOT added - missing source files")
endif()

# === Тестовый клиент SIEM ===
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_siem.cpp)
    add_executable(test_siem
        test_siem.cpp
        ${CLIENT_SOURCES}
        ${COMMON_SOURCES}
    )
    
    target_include_directories(test_siem PRIVATE .)
    target_compile_options(test_siem PRIVATE -pthread)
    target_link_libraries(test_siem pthread)
    
    message(STATUS "Test SIEM client target added")
endif()

# Копирование конфигурационных файлов
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/siem_config.json)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/siem_config.json
        ${CMAKE_CURRENT_BINARY_DIR}/siem_config.json
        COPYONLY
    )
    message(STATUS "SIEM config file will be copied")
endif()

# Информация о доступных целях
message(STATUS "==============================================")
message(STATUS "Available targets:")
if(ALL_SERVER_SOURCES_FOUND)
    message(STATUS "  db_server    - NoSQL database server")
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/client_main.cpp)
    message(STATUS "  db_client    - Database client")
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/siem_agent.cpp)
    message(STATUS "  siem_agent   - SIEM security agent")
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_siem.cpp)
    message(STATUS "  test_siem    - SIEM test client")
endif()
message(STATUS "==============================================")
message(STATUS "")
message(STATUS "Build instructions:")
message(STATUS "  make db_server    - Build database server")
message(STATUS "  make siem_agent   - Build SIEM agent")
message(STATUS "  make all          - Build all targets")
message(STATUS "==============================================")