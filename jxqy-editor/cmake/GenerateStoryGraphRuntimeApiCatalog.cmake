function(generate_story_graph_runtime_api_catalog
    runtime_script_source
    generated_output)
    set_property(DIRECTORY APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS
        "${runtime_script_source}")

    file(READ "${runtime_script_source}" runtime_script_text)
    string(FIND "${runtime_script_text}"
        "void Script::registerFunc()" register_start)
    string(FIND "${runtime_script_text}"
        "#undef regAlias" register_end)
    if(register_start LESS 0 OR
       register_end LESS 0 OR
       register_end LESS_EQUAL register_start)
        message(FATAL_ERROR
            "Unable to locate Script::registerFunc catalog boundaries")
    endif()

    math(EXPR register_length
        "${register_end} - ${register_start}")
    string(SUBSTRING "${runtime_script_text}"
        ${register_start}
        ${register_length}
        register_body)
    string(REGEX REPLACE
        "(^|[\r\n])[ \t]*(//|#)[^\r\n]*"
        "\\1"
        register_body
        "${register_body}")

    set(catalog_entries)
    string(REGEX MATCHALL
        "regFunc\\([A-Za-z_][A-Za-z0-9_]*\\)"
        canonical_matches
        "${register_body}")
    foreach(canonical_match IN LISTS canonical_matches)
        string(REGEX REPLACE
            "regFunc\\(([A-Za-z_][A-Za-z0-9_]*)\\)"
            "\\1"
            canonical_name
            "${canonical_match}")
        string(TOLOWER "${canonical_name}" registered_name)
        list(APPEND catalog_entries
            "${registered_name}|${registered_name}")
    endforeach()

    string(REGEX MATCHALL
        "regAlias\\(\"[^\"]+\"[ \t]*,[ \t]*[A-Za-z_][A-Za-z0-9_]*\\)"
        alias_matches
        "${register_body}")
    foreach(alias_match IN LISTS alias_matches)
        string(REGEX REPLACE
            "regAlias\\(\"([^\"]+)\"[ \t]*,[ \t]*([A-Za-z_][A-Za-z0-9_]*)\\)"
            "\\1"
            alias_name
            "${alias_match}")
        string(REGEX REPLACE
            "regAlias\\(\"([^\"]+)\"[ \t]*,[ \t]*([A-Za-z_][A-Za-z0-9_]*)\\)"
            "\\2"
            canonical_name
            "${alias_match}")
        string(TOLOWER "${alias_name}" registered_name)
        string(TOLOWER "${canonical_name}" canonical_name)
        list(APPEND catalog_entries
            "${registered_name}|${canonical_name}")
    endforeach()

    list(SORT catalog_entries)
    list(REMOVE_DUPLICATES catalog_entries)
    file(WRITE "${generated_output}"
        "// Generated from Script::registerFunc. Do not edit.\n")
    foreach(catalog_entry IN LISTS catalog_entries)
        string(REPLACE "|" ";" entry_parts "${catalog_entry}")
        list(GET entry_parts 0 registered_name)
        list(GET entry_parts 1 canonical_name)
        file(APPEND "${generated_output}"
            "    {\"${registered_name}\", \"${canonical_name}\"},\n")
    endforeach()
endfunction()
