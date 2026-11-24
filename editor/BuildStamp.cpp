// The editor's build stamp (__DATE__ " " __TIME__) for the crash handler, kept in this one tiny file.
// Why: ccache never reuses a file that uses __TIME__ (the time is part of its output), so while the stamp sat in main.cpp
// (56,000 lines, about 5 minutes to compile) every CI and release build recompiled main.cpp. Here only this file
// recompiles, in well under a second, and the stamp stays exact. Written by us; uses only the standard predefined macros.
const char* OkayEditorBuildStamp() { return __DATE__ " " __TIME__; }
