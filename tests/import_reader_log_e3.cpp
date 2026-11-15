// The IQM and COB fixtures for test_import_reader_log.cpp. tests/e3_fixtures.hpp defines helpers with the same names as
// tests/batche_fixtures.hpp, so it lives in its own translation unit.
#include "e3_fixtures.hpp"

#include <string>

std::string ReaderLogIqmFixture() { return fx::BuildIqm(); }
std::string ReaderLogCobFixture() { return fx::CobBinary(); }
