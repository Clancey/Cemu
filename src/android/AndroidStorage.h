#pragma once

#include <string>

// Forward declaration to avoid circular dependencies
struct ANativeActivity;

namespace AndroidBridge
{
	namespace Storage
	{
		// Path management
		std::string GetInternalStoragePath(ANativeActivity* activity);
		std::string GetExternalStoragePath(ANativeActivity* activity);
		std::string GetCachePath(ANativeActivity* activity);

		// Cemu-specific paths
		std::string GetCemuDataPath();
		std::string GetConfigPath();
		std::string GetShaderCachePath();
		std::string GetMlcPath();
		std::string GetGamePath();
		std::string GetLogPath();

		// Directory creation
		bool CreateCemuDirectories();
		bool CreateDirectory(const std::string& path);
		bool CreateDirectoryRecursive(const std::string& path);

		// Permission helpers
		bool HasStoragePermission();
		bool HasExternalStoragePermission();
		void RequestStoragePermissions();

		// File utilities
		bool FileExists(const std::string& path);
		bool DirectoryExists(const std::string& path);
		bool IsWritable(const std::string& path);

		// Content URI handling for Android file access
		std::string ResolveContentUri(const std::string& contentUri);
		bool IsContentUri(const std::string& path);

		// Initialize storage system
		void Initialize(ANativeActivity* activity);
		void Shutdown();
	}
}