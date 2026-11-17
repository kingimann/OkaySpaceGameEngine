// Module model_import: registers the readers for every model format except OBJ, STL and PLY (those are core).
// Written from the old slot dispatch in ModelImport.cpp, unchanged in behaviour. Linked only when the module is on.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Render/ModelReaderRegistry.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include "okay/Render/AmfReader.hpp"
#include "okay/Render/X3dReader.hpp"
#include "okay/Render/DxfReader.hpp"
#include "okay/Render/LwoReader.hpp"

namespace okay {
namespace modules {
void Register_model_import(ModuleContext&) {
    RegisterModelReader(3, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadColladaFile(path, out, err, flatten); });
    RegisterModelReader(4, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return Read3dsFile(path, out, err, flatten); });
    RegisterModelReader(5, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadBvhFile(path, out, err); });
    RegisterModelReader(6, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadOffFile(path, out, err); });
    RegisterModelReader(7, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadSmdFile(path, out, err); });
    RegisterModelReader(8, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadMd5File(path, out, err); });
    RegisterModelReader(9, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadMs3dFile(path, out, err); });
    RegisterModelReader(10, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadAseFile(path, out, err); });
    RegisterModelReader(11, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return Read3mfFile(path, out, err, flatten); });
    RegisterModelReader(12, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadAmfFile(path, out, err, flatten); });
    RegisterModelReader(13, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadX3dFile(path, out, err, flatten); });
    RegisterModelReader(14, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadDxfFile(path, out, err, flatten); });
    RegisterModelReader(15, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadLwoFile(path, out, err); });
    RegisterModelReader(16, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadBlendFile(path, out, err, flatten); });
    RegisterModelReader(17, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadMd2File(path, out, err); });
    RegisterModelReader(18, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadMd3File(path, out, err); });
    RegisterModelReader(19, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadIrrMeshFile(path, out, err); });
    RegisterModelReader(20, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadB3dFile(path, out, err); });
    RegisterModelReader(21, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadXFile(path, out, err); });
    RegisterModelReader(22, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadAc3dFile(path, out, err); });
    RegisterModelReader(23, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadOgreXmlFile(path, out, err); });
    RegisterModelReader(24, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadXglFile(path, out, err); });
    RegisterModelReader(25, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadIrrSceneFile(path, out, err, irrLoader); });
    RegisterModelReader(26, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadNffFile(path, out, err); });
    RegisterModelReader(27, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadTerragenFile(path, out, err); });
    RegisterModelReader(28, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadMdcFile(path, out, err); });
    RegisterModelReader(29, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadHmpFile(path, out, err); });
    RegisterModelReader(30, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadCsmFile(path, out, err); });
    RegisterModelReader(31, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadQ3dFile(path, out, err); });
    RegisterModelReader(33, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadRawFile(path, out, err); });
    RegisterModelReader(34, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadIqmFile(path, out, err); });
    RegisterModelReader(35, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadCobFile(path, out, err); });
    RegisterModelReader(36, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadNdoFile(path, out, err); });
    RegisterModelReader(39, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadPmxFile(path, out, err); });
    RegisterModelReader(40, [](const std::string& path, ImportedModel& out, std::string* err, bool flatten, const IrrMeshLoader* irrLoader) { (void)flatten; (void)irrLoader; return ReadLwsFile(path, out, err, irrLoader); });
}
} // namespace modules
} // namespace okay
