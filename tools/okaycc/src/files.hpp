#pragma once

#include <map>
#include <string>

namespace okaycc {

// Where #include reads from. Tests use a memory map; the driver uses the disk.
class FileStore {
public:
    virtual ~FileStore() = default;
    virtual bool read(const std::string& path, std::string& out) const = 0;
};

class MemoryFiles : public FileStore {
public:
    void add(std::string path, std::string contents);
    bool read(const std::string& path, std::string& out) const override;

private:
    std::map<std::string, std::string> files_;
};

class DiskFiles : public FileStore {
public:
    bool read(const std::string& path, std::string& out) const override;
};

std::string normalizePath(const std::string& path);
std::string dirOf(const std::string& path);
std::string joinPath(const std::string& dir, const std::string& rel);

}  // namespace okaycc
