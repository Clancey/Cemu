#include "FilesystemAndroid.h"

namespace FilesystemAndroid
{
	static std::shared_ptr<FilesystemCallbacks> s_filesystemCallbacks;

	void SetFilesystemCallbacks(const std::shared_ptr<FilesystemCallbacks> &filesystemCallbacks)
	{
		s_filesystemCallbacks = filesystemCallbacks;
	}

	int OpenContentUri(const std::filesystem::path &uri)
	{
		if (s_filesystemCallbacks)
			return s_filesystemCallbacks->OpenContentUri(uri);
		return -1;
	}

	std::vector<std::filesystem::path> ListFiles(const std::filesystem::path &uri)
	{
		if (s_filesystemCallbacks)
			return s_filesystemCallbacks->ListFiles(uri);
		return {};
	}

	bool IsDirectory(const std::filesystem::path &uri)
	{
		if (s_filesystemCallbacks)
			return s_filesystemCallbacks->IsDirectory(uri);
		return false;
	}

	bool IsFile(const std::filesystem::path &uri)
	{
		if (s_filesystemCallbacks)
			return s_filesystemCallbacks->IsFile(uri);
		return false;
	}

	bool Exists(const std::filesystem::path& uri)
	{
		if (s_filesystemCallbacks)
			return s_filesystemCallbacks->Exists(uri);
		return false;
	}

	bool IsContentUri(const std::string &uri)
	{
		return uri.starts_with("content://");
	}
}