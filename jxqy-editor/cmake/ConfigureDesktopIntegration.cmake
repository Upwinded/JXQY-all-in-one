set(_JXQY_EDITOR_DESKTOP_INTEGRATION_MODULE_DIR
    "${CMAKE_CURRENT_LIST_DIR}")

# extension|Windows ProgID|display description|Linux MIME|macOS UTI|broad text
#
# This is the only production list of file types that the editor advertises to
# desktop operating systems. C++, Windows association handling, macOS bundle
# metadata, Linux MIME metadata, and the Linux desktop entry are all generated
# from these rows.
set(JXQY_EDITOR_DESKTOP_FILE_TYPES
    "mpc|JXQY.Editor.mpc|UPEdit-JXQY MPC image|application/x-jxqy-mpc|com.jxqy.editor.mpc|false"
    "shd|JXQY.Editor.shd|UPEdit-JXQY SHD image|application/x-jxqy-shd|com.jxqy.editor.shd|false"
    "asf|JXQY.Editor.asf|UPEdit-JXQY ASF image|application/x-jxqy-asf|com.jxqy.editor.asf|false"
    "pic|JXQY.Editor.pic|UPEdit-JXQY PIC image|application/x-jxqy-pic|com.jxqy.editor.pic|false"
    "imp|JXQY.Editor.imp|UPEdit-JXQY IMP image|application/x-jxqy-imp|com.jxqy.editor.imp|false"
    "img|JXQY.Editor.img|UPEdit-JXQY IMG image|application/x-jxqy-img|com.jxqy.editor.img|false"
    "map|JXQY.Editor.map|UPEdit-JXQY map|application/x-jxqy-map|com.jxqy.editor.map|false"
    "npc|JXQY.Editor.npc|UPEdit-JXQY NPC table|application/x-jxqy-npc|com.jxqy.editor.npc|false"
    "obj|JXQY.Editor.obj|UPEdit-JXQY object table|application/x-jxqy-obj|com.jxqy.editor.obj|false"
    "txt|JXQY.Editor.txt|UPEdit-JXQY script text|text/plain|public.plain-text|true"
)

function(jxqy_configure_desktop_integration output_directory)
    file(MAKE_DIRECTORY "${output_directory}")

    set(cpp_file_types "")
    set(linux_mime_types "")
    set(linux_mime_definitions "")
    set(mac_document_types "")
    set(mac_exported_types "")

    foreach(file_type_row IN LISTS JXQY_EDITOR_DESKTOP_FILE_TYPES)
        string(REPLACE "|" ";" file_type_fields "${file_type_row}")
        list(LENGTH file_type_fields file_type_field_count)
        if(NOT file_type_field_count EQUAL 6)
            message(FATAL_ERROR
                "Invalid JXQY editor desktop file type row: ${file_type_row}")
        endif()

        list(GET file_type_fields 0 extension)
        list(GET file_type_fields 1 program_id)
        list(GET file_type_fields 2 description)
        list(GET file_type_fields 3 mime_type)
        list(GET file_type_fields 4 uniform_type_identifier)
        list(GET file_type_fields 5 broad_text_type)

        if(broad_text_type STREQUAL "true")
            set(cpp_broad_text_type true)
        elseif(broad_text_type STREQUAL "false")
            set(cpp_broad_text_type false)
        else()
            message(FATAL_ERROR
                "Invalid broad-text flag in desktop file type row: ${file_type_row}")
        endif()

        string(APPEND cpp_file_types
            "        {QStringLiteral(\".${extension}\"), "
            "QStringLiteral(\"${program_id}\"), "
            "QStringLiteral(\"${description}\"), "
            "QStringLiteral(\"${mime_type}\"), "
            "QStringLiteral(\"${uniform_type_identifier}\"), "
            "${cpp_broad_text_type}},\n")

        string(APPEND linux_mime_types "${mime_type};")
        if(NOT mime_type STREQUAL "text/plain")
            string(APPEND linux_mime_definitions
                "  <mime-type type=\"${mime_type}\">\n"
                "    <comment>${description}</comment>\n"
                "    <glob pattern=\"*.${extension}\" weight=\"80\"/>\n"
                "  </mime-type>\n")
        endif()

        string(APPEND mac_document_types
            "    <dict>\n"
            "      <key>CFBundleTypeName</key>\n"
            "      <string>${description}</string>\n"
            "      <key>CFBundleTypeRole</key>\n"
            "      <string>Editor</string>\n"
            "      <key>LSHandlerRank</key>\n"
            "      <string>Alternate</string>\n"
            "      <key>LSItemContentTypes</key>\n"
            "      <array>\n"
            "        <string>${uniform_type_identifier}</string>\n"
            "      </array>\n"
            "    </dict>\n")

        if(NOT uniform_type_identifier MATCHES "^public\\.")
            string(APPEND mac_exported_types
                "    <dict>\n"
                "      <key>UTTypeIdentifier</key>\n"
                "      <string>${uniform_type_identifier}</string>\n"
                "      <key>UTTypeDescription</key>\n"
                "      <string>${description}</string>\n"
                "      <key>UTTypeConformsTo</key>\n"
                "      <array>\n"
                "        <string>public.data</string>\n"
                "        <string>public.content</string>\n"
                "      </array>\n"
                "      <key>UTTypeTagSpecification</key>\n"
                "      <dict>\n"
                "        <key>public.filename-extension</key>\n"
                "        <array>\n"
                "          <string>${extension}</string>\n"
                "        </array>\n"
                "        <key>public.mime-type</key>\n"
                "        <string>${mime_type}</string>\n"
                "      </dict>\n"
                "    </dict>\n")
        endif()
    endforeach()

    set(generated_cpp_file_types
        "${output_directory}/DesktopFileTypesGenerated.inc")
    set(existing_cpp_file_types "")
    if(EXISTS "${generated_cpp_file_types}")
        file(READ "${generated_cpp_file_types}" existing_cpp_file_types)
    endif()
    if(NOT existing_cpp_file_types STREQUAL cpp_file_types)
        file(WRITE "${generated_cpp_file_types}" "${cpp_file_types}")
    endif()

    set(JXQY_EDITOR_LINUX_MIME_TYPES "${linux_mime_types}")
    set(JXQY_EDITOR_LINUX_MIME_DEFINITIONS "${linux_mime_definitions}")
    set(JXQY_EDITOR_MAC_DOCUMENT_TYPES "${mac_document_types}")
    set(JXQY_EDITOR_MAC_EXPORTED_TYPES "${mac_exported_types}")

    set(generated_linux_desktop
        "${output_directory}/com.jxqy.Editor.desktop")
    set(generated_linux_mime
        "${output_directory}/com.jxqy.Editor.xml")
    set(generated_macos_plist
        "${output_directory}/Info.plist")

    configure_file(
        "${_JXQY_EDITOR_DESKTOP_INTEGRATION_MODULE_DIR}/../packaging/linux/com.jxqy.Editor.desktop.in"
        "${generated_linux_desktop}"
        @ONLY)
    configure_file(
        "${_JXQY_EDITOR_DESKTOP_INTEGRATION_MODULE_DIR}/../packaging/linux/com.jxqy.Editor.mime.xml.in"
        "${generated_linux_mime}"
        @ONLY)
    configure_file(
        "${_JXQY_EDITOR_DESKTOP_INTEGRATION_MODULE_DIR}/../packaging/macos/Info.plist.in"
        "${generated_macos_plist}"
        @ONLY)

    set(JXQY_EDITOR_GENERATED_CPP_FILE_TYPES
        "${generated_cpp_file_types}" PARENT_SCOPE)
    set(JXQY_EDITOR_GENERATED_LINUX_DESKTOP
        "${generated_linux_desktop}" PARENT_SCOPE)
    set(JXQY_EDITOR_GENERATED_LINUX_MIME
        "${generated_linux_mime}" PARENT_SCOPE)
    set(JXQY_EDITOR_GENERATED_MACOS_PLIST
        "${generated_macos_plist}" PARENT_SCOPE)
    set(JXQY_EDITOR_DESKTOP_INTEGRATION_OUTPUT_DIRECTORY
        "${output_directory}" PARENT_SCOPE)
endfunction()
