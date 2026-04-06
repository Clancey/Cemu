#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace FilesystemAndroid
{
class FilesystemCallbacks
{
   public:
    virtual int OpenContentUri(const std::filesystem::path &uri) = 0;
    virtual std::vector<std::filesystem::path> ListFiles(const std::filesystem::path &uri) = 0;
    virtual bool IsDirectory(const std::filesystem::path &uri) = 0;
    virtual bool IsFile(const std::filesystem::path &uri) = 0;
    virtual bool Exists(const std::filesystem::path &uri) = 0;
};

void SetFilesystemCallbacks(const std::shared_ptr<FilesystemCallbacks> &filesystemCallbacks);

int OpenContentUri(const std::filesystem::path &uri);

std::vector<std::filesystem::path> ListFiles(const std::filesystem::path &uri);

bool IsDirectory(const std::filesystem::path &uri);

bool IsFile(const std::filesystem::path &uri);

bool Exists(const std::filesystem::path& uri);

bool IsContentUri(const std::string &uri);

}  // namespace FilesystemAndroid