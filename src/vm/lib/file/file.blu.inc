// Generated automatically from src/vm/lib/file/file.blu. Do not edit.
static const char* fileSource =
"class File {\n"
"\n"
"    fn __init(name, mode) {\n"
"        @_name = name\n"
"        @_mode = mode\n"
"    }\n"
"\n"
"    static fn open(name, mode) {\n"
"        return File(name, mode).open()\n"
"    }\n"
"\n"
"    // fn open()\n"
"\n"
"    // fn close()\n"
"\n"
"    // fn rewind()\n"
"\n"
"    // fn readLine()\n"
"\n"
"    fn lines() {\n"
"        @rewind()\n"
"\n"
"        var lines = []\n"
"\n"
"        var line = @readLine()\n"
"\n"
"        while line {\n"
"            lines.push(line)\n"
"\n"
"            line = @readLine()\n"
"        }\n"
"\n"
"        return lines\n"
"    }\n"
"\n"
"}\n";