set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/JxqyEngineVersion.inc")
file(READ
    "${CMAKE_CURRENT_LIST_DIR}/JxqyEngineVersion.inc"
    _JXQY_ENGINE_VERSION_LITERAL)
string(STRIP
    "${_JXQY_ENGINE_VERSION_LITERAL}"
    _JXQY_ENGINE_VERSION_LITERAL)
if(NOT _JXQY_ENGINE_VERSION_LITERAL MATCHES
        "^\"([0-9]+\\.[0-9]+\\.[0-9]+)\"$")
    message(FATAL_ERROR
        "JxqyEngineVersion.inc must contain one quoted major.minor.patch version")
endif()
set(JXQY_ENGINE_VERSION "${CMAKE_MATCH_1}")
unset(_JXQY_ENGINE_VERSION_LITERAL)
