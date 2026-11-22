#pragma once
// Why a model file did not import, in plain words, for the editor's "Model import failed" line. Before this the
// Console said only "Model import failed: <path>", whatever the reason (a moved file, an empty file, a picture renamed
// to .glb, a format this build does not read, a damaged file).
// Header note: keep this header free of <mutex>, <thread> and <condition_variable>.
#include <string>

namespace okay {

/// The reason `path` cannot be imported as a model, as one short sentence without the path ("the file is empty (0
/// bytes)", "this build does not read .xyz files", "it is not a glTF file: it does not start with '{'", the reader's own
/// error, ...). Call it after ImportModelScene / ImportModel failed: it looks at the file again (cheap checks first;
/// only for formats read through the import seam does it run the reader once more to get its error). Never throws.
std::string DescribeModelImportProblem(const std::string& path);

} // namespace okay
