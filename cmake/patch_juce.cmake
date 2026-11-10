# The plugin name is Cyrillic (Source/UbvName.h, UTF-8). On Windows JucePlugin_Name stays ASCII (see
# CMakeLists.txt), so every place a VST3 host reads the name gets UBV_DISPLAY_NAME instead:
# - PClassInfo/PClassInfo2 name (8-bit): the UTF-8 bytes. JUCE-based hosts read it as UTF-8 and find
#   the class to load by comparing it with the scanned name;
# - PClassInfoW name: JUCE widens the 8-bit name byte by byte (mojibake), so decode UTF-8 properly;
# - moduleinfo.json (written by the VST3 manifest helper, its own target): hence an absolute include.
function(ubv_patch_juce juce_dir name_header)
    set(info_file "${juce_dir}/modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h")
    set(helper_file "${juce_dir}/modules/juce_audio_plugin_client/VST3/juce_VST3ManifestHelper.cpp")

    file(READ "${info_file}" text)
    string(REPLACE "\r\n" "\n" text "${text}")   # git on Windows may check out CRLF
    string(FIND "${text}" "UBV_PATCHED" done)
    if(done EQUAL -1)
        set(anchor "            infoW.fromAscii (info2);\n")
        string(FIND "${text}" "${anchor}" at)
        string(FIND "${text}" "namespace juce\n{" at_ns)
        if(at EQUAL -1 OR at_ns EQUAL -1)
            message(FATAL_ERROR "patch_juce: anchor not found in ${info_file} (JUCE version changed?)")
        endif()
        set(patch [=[
            // UBV_PATCHED: 8-bit name as UTF-8
            {
                const char* s = UBV_DISPLAY_NAME;
                size_t o = 0;
                while (s[o] != 0 && o + 1 < sizeof (info2.name)) { info2.name[o] = s[o]; ++o; }
                info2.name[o] = 0;
            }

            infoW.fromAscii (info2);

            // UBV_PATCHED: UTF-8 -> UTF-16
            {
                const auto* s = reinterpret_cast<const unsigned char*> (UBV_DISPLAY_NAME);
                const size_t cap = sizeof (infoW.name) / sizeof (infoW.name[0]);
                size_t o = 0;

                while (*s != 0 && o + 1 < cap)
                {
                    unsigned int c = *s++;

                    if (c >= 0xc0 && c < 0xe0 && (s[0] & 0xc0) == 0x80)
                    {
                        c = ((c & 0x1fu) << 6) | (s[0] & 0x3fu);
                        s += 1;
                    }
                    else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80)
                    {
                        c = ((c & 0x0fu) << 12) | ((s[0] & 0x3fu) << 6) | (s[1] & 0x3fu);
                        s += 2;
                    }

                    infoW.name[o++] = (Steinberg::char16) c;
                }

                infoW.name[o] = 0;
            }
]=])
        string(REPLACE "${anchor}" "${patch}" text "${text}")
        string(REPLACE "namespace juce\n{" "#include \"${name_header}\"   // UBV_PATCHED\n\nnamespace juce\n{" text "${text}")
        file(WRITE "${info_file}" "${text}")
        message(STATUS "patch_juce: VST3 class names patched")
    endif()

    file(READ "${helper_file}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(FIND "${text}" "UBV_PATCHED" done)
    if(done EQUAL -1)
        set(anchor "moduleInfo.name = JucePlugin_Name;")
        string(FIND "${text}" "${anchor}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "patch_juce: anchor not found in ${helper_file} (JUCE version changed?)")
        endif()
        string(REPLACE "${anchor}" "moduleInfo.name = UBV_DISPLAY_NAME;   // UBV_PATCHED" text "${text}")
        file(WRITE "${helper_file}" "${text}")
        message(STATUS "patch_juce: VST3 manifest name patched")
    endif()
endfunction()
