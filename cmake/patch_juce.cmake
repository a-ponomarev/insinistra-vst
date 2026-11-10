# VST3 class info: JUCE does infoW.fromAscii (info2), which widens the UTF-8 bytes of the name one
# by one. After it, decode UBV_DISPLAY_NAME (Source/UbvName.h, UTF-8) into infoW.name properly.
# The VST3 manifest helper compiles the same header without our include path: hence __has_include.
function(ubv_patch_juce juce_dir)
    set(file "${juce_dir}/modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h")
    file(READ "${file}" text)
    string(REPLACE "\r\n" "\n" text "${text}")   # git on Windows may check out CRLF
    string(FIND "${text}" "UBV_PATCHED" done)
    if(NOT done EQUAL -1)
        return()
    endif()
    set(anchor "            infoW.fromAscii (info2);\n")
    string(FIND "${text}" "${anchor}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "patch_juce: anchor not found in ${file} (JUCE version changed?)")
    endif()
    set(patch [=[
            infoW.fromAscii (info2);
            // UBV_PATCHED: UTF-8 display name -> UTF-16
           #if __has_include ("UbvName.h")
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
           #endif
]=])
    string(REPLACE "${anchor}" "${patch}" text "${text}")
    # the header must see the name before the class entries
    string(REPLACE "namespace juce\n{" "#if __has_include (\"UbvName.h\")\n #include \"UbvName.h\"\n#endif\n\nnamespace juce\n{" text "${text}")
    file(WRITE "${file}" "${text}")
    message(STATUS "patch_juce: VST3 Unicode class name patched")
endfunction()
