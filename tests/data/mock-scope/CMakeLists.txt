include_directories(${SCOPESLIB_INCLUDE_DIRS})

add_library(mock-scope MODULE mock-scope.cpp)
target_link_libraries(mock-scope ${SCOPESLIB_LDFLAGS})

configure_file(mock-scope.ini.in mock-scope.ini)
configure_file(mock-scope-settings.ini mock-scope-settings.ini)
