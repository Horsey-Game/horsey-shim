names = """GetFileVersionInfoA GetFileVersionInfoByHandle GetFileVersionInfoExA GetFileVersionInfoExW GetFileVersionInfoSizeA GetFileVersionInfoSizeExA GetFileVersionInfoSizeExW GetFileVersionInfoSizeW GetFileVersionInfoW VerFindFileA VerFindFileW VerInstallFileA VerInstallFileW VerLanguageNameA VerLanguageNameW VerQueryValueA VerQueryValueW""".split()

with open(r"D:\dev\SHIM\src\exports.def", "w") as f:
    f.write("LIBRARY version\nEXPORTS\n")
    for n in names:
        f.write("    %s\n" % n)

with open(r"D:\dev\SHIM\src\proxy.asm", "w") as f:
    f.write("EXTERN g_real:QWORD\n.code\n")
    for i, n in enumerate(names):
        f.write("%s PROC\n    jmp qword ptr g_real[%d]\n%s ENDP\n" % (n, i * 8, n))
    f.write("END\n")

with open(r"D:\dev\SHIM\src\names.h", "w") as f:
    f.write("static const char* const kNames[] = {\n")
    for n in names:
        f.write('    "%s",\n' % n)
    f.write("};\n")
