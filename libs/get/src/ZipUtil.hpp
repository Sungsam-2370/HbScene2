#pragma once

// use included minizip library
#include "unzip.h"
#include "zip.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
class Zip
{
public:
	explicit Zip(const std::string& zipPath);
	void Close();
	~Zip();
	int AddFile(const std::string& internalPath, const std::string& path);
	int AddDir(const std::string& internalDir, const std::string& externalDir);

private:
	int Add(const std::string& path);
	zipFile fileToZip;
};
class UnZip
{
public:
	explicit UnZip(const std::string& zipPath);
	~UnZip();
	void Close();

	bool IsValid();
	int Extract(const std::string& path, unz_file_pos& file_pos);
	int Extract(const std::string& path, const unz_file_info_s& fileInfo);
	int ExtractFile(const std::string& internalPath, const std::string& path);
	int ExtractAll(const std::string& dirToExtract);
	int ExtractAllWithProgress(const std::string& dirToExtract);
	int ExtractDir(const std::string& internalDir, const std::string& externalDir);
	std::vector<std::string> PathDump();
	std::unordered_map<std::string, unz_file_pos> GetPathToFilePosMapping();

private:
	std::string GetFileName(const unz_file_info_s& fileInfo);
	std::string GetFullFileName(const unz_file_info_s& fileInfo);
	unz_file_info_s GetFileInfo();
	int ExtractImpl(const std::string& path, const unz_file_info_s& fileInfo);
	unzFile fileToUnzip;
	bool cpuBoosted = false;

	// folders already created while extracting (avoids a mkdir per file)
	std::unordered_set<std::string> createdDirs;

	// timing stats, printed when the zip is closed
	std::chrono::steady_clock::time_point openedAt = std::chrono::steady_clock::now();
	uint64_t statFiles = 0;
	uint64_t statBytes = 0;
	double statExtractMs = 0;
};
