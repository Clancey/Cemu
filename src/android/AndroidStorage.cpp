#include "AndroidStorage.h"
#include "AndroidMain.h"

#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <jni.h>

#define LOG_TAG "CemuAndroidStorage"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace AndroidBridge
{
	namespace Storage
	{
		static std::string s_internalStoragePath;
		static std::string s_externalStoragePath;
		static std::string s_cachePath;
		static ANativeActivity* s_activity = nullptr;

		std::string GetInternalStoragePath(ANativeActivity* activity)
		{
			if (s_internalStoragePath.empty() && activity)
			{
				s_internalStoragePath = std::string(activity->internalDataPath);
			}
			return s_internalStoragePath;
		}

		std::string GetExternalStoragePath(ANativeActivity* activity)
		{
			if (s_externalStoragePath.empty() && activity)
			{
				s_externalStoragePath = std::string(activity->externalDataPath);
			}
			return s_externalStoragePath;
		}

		std::string GetCachePath(ANativeActivity* activity)
		{
			if (s_cachePath.empty() && activity)
			{
				// Try to get cache path from Java
				JNIEnv* env;
				if (activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK)
				{
					jclass activityClass = env->GetObjectClass(activity->clazz);
					jmethodID getCacheDir = env->GetMethodID(activityClass, "getCacheDir", "()Ljava/io/File;");

					if (getCacheDir)
					{
						jobject cacheDir = env->CallObjectMethod(activity->clazz, getCacheDir);
						if (cacheDir)
						{
							jclass fileClass = env->GetObjectClass(cacheDir);
							jmethodID getAbsolutePath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
							jstring pathString = (jstring)env->CallObjectMethod(cacheDir, getAbsolutePath);

							const char* pathChars = env->GetStringUTFChars(pathString, nullptr);
							s_cachePath = std::string(pathChars);
							env->ReleaseStringUTFChars(pathString, pathChars);

							env->DeleteLocalRef(fileClass);
							env->DeleteLocalRef(cacheDir);
							env->DeleteLocalRef(pathString);
						}
					}
					env->DeleteLocalRef(activityClass);
				}

				if (s_cachePath.empty())
				{
					// Fallback to internal storage + cache
					s_cachePath = GetInternalStoragePath(activity) + "/cache";
				}
			}
			return s_cachePath;
		}

		std::string GetCemuDataPath()
		{
			if (!s_activity) return "";
			return GetInternalStoragePath(s_activity) + "/cemu";
		}

		std::string GetConfigPath()
		{
			return GetCemuDataPath() + "/config";
		}

		std::string GetShaderCachePath()
		{
			if (!s_activity) return "";
			return GetCachePath(s_activity) + "/cemu/shaderCache";
		}

		std::string GetMlcPath()
		{
			return GetCemuDataPath() + "/mlc01";
		}

		std::string GetGamePath()
		{
			if (!s_activity) return "";
			// Try external storage first for game ROMs (larger files)
			std::string externalPath = GetExternalStoragePath(s_activity);
			if (!externalPath.empty())
			{
				return externalPath + "/games";
			}
			return GetCemuDataPath() + "/games";
		}

		std::string GetLogPath()
		{
			return GetCemuDataPath() + "/logs";
		}

		bool CreateDirectory(const std::string& path)
		{
			if (path.empty()) return false;

			struct stat st;
			if (stat(path.c_str(), &st) == 0)
			{
				return S_ISDIR(st.st_mode);
			}

			if (mkdir(path.c_str(), 0755) == 0)
			{
				LOGD("Created directory: %s", path.c_str());
				return true;
			}

			LOGE("Failed to create directory: %s", path.c_str());
			return false;
		}

		bool CreateDirectoryRecursive(const std::string& path)
		{
			if (path.empty()) return false;

			try
			{
				std::filesystem::create_directories(path);
				return DirectoryExists(path);
			}
			catch (const std::exception& e)
			{
				LOGE("Failed to create directory recursively %s: %s", path.c_str(), e.what());
				return false;
			}
		}

		bool CreateCemuDirectories()
		{
			bool success = true;

			// Create base Cemu data directory
			std::string cemuDataPath = GetCemuDataPath();
			success &= CreateDirectoryRecursive(cemuDataPath);

			// Create config directory
			success &= CreateDirectoryRecursive(GetConfigPath());

			// Create shader cache directory
			success &= CreateDirectoryRecursive(GetShaderCachePath());

			// Create MLC directory and subdirectories
			std::string mlcPath = GetMlcPath();
			success &= CreateDirectoryRecursive(mlcPath);
			success &= CreateDirectoryRecursive(mlcPath + "/sys");
			success &= CreateDirectoryRecursive(mlcPath + "/usr");
			success &= CreateDirectoryRecursive(mlcPath + "/usr/title");
			success &= CreateDirectoryRecursive(mlcPath + "/usr/save");

			// Create game directory
			success &= CreateDirectoryRecursive(GetGamePath());

			// Create log directory
			success &= CreateDirectoryRecursive(GetLogPath());

			// Create additional shader cache subdirectories
			std::string shaderCachePath = GetShaderCachePath();
			success &= CreateDirectoryRecursive(shaderCachePath + "/driver");
			success &= CreateDirectoryRecursive(shaderCachePath + "/driver/nvidia");
			success &= CreateDirectoryRecursive(shaderCachePath + "/transferable");

			if (success)
			{
				LOGD("Successfully created all Cemu directories");
			}
			else
			{
				LOGE("Failed to create some Cemu directories");
			}

			return success;
		}

		bool HasStoragePermission()
		{
			// On Android, internal storage doesn't require permissions
			// External storage might require permissions depending on Android version
			return true; // For now, assume we have permissions
		}

		bool HasExternalStoragePermission()
		{
			// For Android 6.0+ (API 23+), we need runtime permission for external storage
			// This would typically be handled in Java/Kotlin side
			return true; // Assume granted for now
		}

		void RequestStoragePermissions()
		{
			// This would typically trigger a permission request dialog
			// Implementation would be on Java/Kotlin side
			LOGD("Storage permissions requested (implementation needed in Java/Kotlin)");
		}

		bool FileExists(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}

		bool DirectoryExists(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		bool IsWritable(const std::string& path)
		{
			if (path.empty()) return false;
			return access(path.c_str(), W_OK) == 0;
		}

		std::string ResolveContentUri(const std::string& contentUri)
		{
			// Content URIs on Android need to be resolved through the content resolver
			// This requires Java/JNI call to ContentResolver.openInputStream()
			// For now, return as-is and handle in higher-level code
			return contentUri;
		}

		bool IsContentUri(const std::string& path)
		{
			return path.find("content://") == 0;
		}

		void Initialize(ANativeActivity* activity)
		{
			s_activity = activity;

			if (!activity)
			{
				LOGE("AndroidStorage::Initialize called with null activity");
				return;
			}

			LOGD("Initializing Android storage system");

			// Get storage paths
			std::string internalPath = GetInternalStoragePath(activity);
			std::string externalPath = GetExternalStoragePath(activity);
			std::string cachePath = GetCachePath(activity);

			LOGD("Internal storage: %s", internalPath.c_str());
			LOGD("External storage: %s", externalPath.c_str());
			LOGD("Cache path: %s", cachePath.c_str());

			// Create Cemu directories
			if (!CreateCemuDirectories())
			{
				LOGE("Failed to create Cemu directories");
			}

			LOGD("Android storage system initialized");
		}

		void Shutdown()
		{
			LOGD("Shutting down Android storage system");
			s_activity = nullptr;
			s_internalStoragePath.clear();
			s_externalStoragePath.clear();
			s_cachePath.clear();
		}
	}
}