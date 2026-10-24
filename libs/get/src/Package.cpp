#include "./repos/Repo.hpp"
#include "Utils.hpp"
#include "ZipUtil.hpp"
#include "Cancel.hpp"
#include "./repos/constants.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>
#define u8 uint8_t

#if defined(__WIIU__)
// include xml files for legacy hb app store support
#include "tinyxml.h"
#endif

Package::Package(int state)
{
	this->pkg_name = "?";
	this->title = "???";
	this->author = "Unknown";
	this->version = "0.0.0";
	this->short_desc = "N/A";
	this->long_desc = "N/A";

	this->license = "";
	this->changelog = "";
	this->url = "";
	this->updated = "";
	this->updated_timestamp = 0;

	this->download_size = 0;
	this->extracted_size = 0;
	this->downloads = 0;

	this->category = "_all";
	this->binary = "none";

	this->status = state;
	this->screens = 0;
}

Package::~Package() = default;

std::string Package::toString() const
{
	return "[" + this->pkg_name + "] (" + this->version + ") \"" + this->title + "\" - " + this->short_desc;
}

bool Package::downloadZip(std::string_view tmp_path, float*, bool resume, int zipIndex, int zipTotal) const
{
	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_DOWNLOADING, zipIndex, zipTotal);

	// fetch zip file to tmp directory using curl
	printf("--> Downloading %s to %s%s\n", this->pkg_name.c_str(), tmp_path.data(), resume ? " (resuming)" : "");
	auto zipUrl = this->mRepo->getZipUrl(*this);

	if (zipUrl.empty()) {
		printf("--> ERROR: No download url found for %s\n", this->pkg_name.c_str());
		return false;
	}

	std::string downloadPath = std::string(tmp_path) + this->pkg_name + getUrlFileExt();
	
	// passing metadata allows us to note some headers for resume support
	download_metadata_t metadata = {};
	metadata.content_length = this->download_size;
	
	bool success = downloadFileToDiskWithMetadata(zipUrl, downloadPath, resume, &metadata);

	// cancelado por el usuario: se borra lo descargado a medias (no se reanuda)
	if (!success && libget_cancelled())
	{
		std::remove(downloadPath.c_str());
		std::remove(getMetadataPath(downloadPath).c_str());
		return false;
	}
	
	// if the download filed, but some data was received, record any metadata
	// (saves whether or not resume was requested)
	if (!success)
	{
		struct stat file_info = {};
		if (stat(downloadPath.c_str(), &file_info) == 0 && file_info.st_size > 0)
		{
			if (!metadata.etag.empty() || metadata.content_length > 0)
			{
				printf("--> Saving download metadata for resume validation\n");
				saveDownloadMetadata(downloadPath, metadata);
			}
		}
		
		if (resume)
		{
			printf("--> Resume failed. File may have changed on server. Try downloading without --resume\n");
		}
	}
	else
	{
		// Download succeeded, clean up any metadata file
		std::string metapath = getMetadataPath(downloadPath);
		std::remove(metapath.c_str());
	}
	
	return success;
}

bool Package::downloadZip2(std::string_view tmp_path, float*, bool resume, int zipIndex, int zipTotal) const
{
	if (this->zipUrl2.empty()) {
		return false; // no second zip, nothing to do
	}

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_DOWNLOADING, zipIndex, zipTotal);

	printf("--> Downloading second zip for %s to %s%s\n", this->pkg_name.c_str(), tmp_path.data(), resume ? " (resuming)" : "");

	// use a different temp filename to avoid overwriting the first zip
	std::string downloadPath = std::string(tmp_path) + this->pkg_name + "_2.zip";

	download_metadata_t metadata = {};

	bool success = downloadFileToDiskWithMetadata(this->zipUrl2, downloadPath, resume, &metadata);

	// cancelado por el usuario: se borra lo descargado a medias (no se reanuda)
	if (!success && libget_cancelled())
	{
		std::remove(downloadPath.c_str());
		std::remove(getMetadataPath(downloadPath).c_str());
		return false;
	}

	if (!success)
	{
		struct stat file_info = {};
		if (stat(downloadPath.c_str(), &file_info) == 0 && file_info.st_size > 0)
		{
			if (!metadata.etag.empty() || metadata.content_length > 0)
			{
				printf("--> Saving download metadata for resume validation\n");
				saveDownloadMetadata(downloadPath, metadata);
			}
		}

		if (resume)
		{
			printf("--> Resume failed. File may have changed on server. Try downloading without --resume\n");
		}
	}
	else
	{
		std::string metapath = getMetadataPath(downloadPath);
		std::remove(metapath.c_str());
	}

	return success;
}

std::string Package::getUrlFileExt() const
{
	std::string urlEnding = ".zip"; // assume zip

	auto zipUrl = this->mRepo->getZipUrl(*this);

	if (!zipUrl.ends_with(".zip")) { // only do something if it's not a .zip already
		auto lastDotPos = zipUrl.find_last_of(".");
		if (lastDotPos != std::string::npos) {
			// slash found, grab the file ending
			std::string ending = zipUrl.substr(lastDotPos);
			urlEnding = ending;
			// printf("--> Found file ending: %s\n", urlEnding.c_str());
		}
	}

	return urlEnding;
}

// This method returns 0 through 100 representing how much of the file is downloaded, or 
// returns negative for a partial download not being possible. It can be called before passing resume=true
// to downloadZip()
int Package::hasPartialDownload(std::string_view tmp_path) const
{
	std::string downloadPath = std::string(tmp_path) + this->pkg_name + getUrlFileExt();
	struct stat file_info = {};
	
	// File must exist and not be empty
	if (stat(downloadPath.c_str(), &file_info) == 0 && file_info.st_size > 0)
	{
		// do we have any existing metadata?
		download_metadata_t saved_metadata = {};
		bool has_saved_metadata = loadDownloadMetadata(downloadPath, &saved_metadata);
		
		// validate whatever we can against the current headers (in case the file changed, eg on a new update)
		if (has_saved_metadata && !saved_metadata.etag.empty())
		{
			auto zipUrl = this->mRepo->getZipUrl(*this);
			if (!zipUrl.empty())
			{
				download_metadata_t remote_metadata = {};
				if (getRemoteFileMetadata(zipUrl, &remote_metadata))
				{
					// etag check
					if (!remote_metadata.etag.empty() && remote_metadata.etag != saved_metadata.etag)
					{
						printf("--> Warning: Remote file ETag changed (was %s, now %s)\n", 
							   saved_metadata.etag.c_str(), remote_metadata.etag.c_str());
						return -1;
					}
					
					// content length check
					if (remote_metadata.content_length > 0 && saved_metadata.content_length > 0 &&
						remote_metadata.content_length != saved_metadata.content_length)
					{
						printf("--> Warning: Remote file size changed (was %ld, now %ld)\n",
							   saved_metadata.content_length, remote_metadata.content_length);
						return -1;
					}
				}
			}
		}
		
		// How far along was the download given our partial?
		if (this->download_size > 0)
		{
			int percentage = (int)((file_info.st_size * 100) / this->download_size);
			return (percentage >= 100) ? 99 : percentage; // 100 would be misleading
		}
		else
		{
			// total size may not always be available, fallback just return 1%
			return 1;
		}
	}
	
	return -1; // cannot resume!
}

long Package::getPartialDownloadSize(std::string_view tmp_path) const
{
	std::string downloadPath = std::string(tmp_path) + this->pkg_name + getUrlFileExt();
	struct stat file_info = {};
	
	if (stat(downloadPath.c_str(), &file_info) == 0)
	{
		return file_info.st_size;
	}
	
	return 0;
}

bool Package::install(const std::string& pkg_path, const std::string& tmp_path, int zipIndex, int zipTotal)
{
	printf("Going to install %s\n", this->pkg_name.c_str());
	// assumes that download was called first
	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_ANALYZING, zipIndex, zipTotal);

	if (networking_callback != nullptr)
		networking_callback(nullptr, 1.0);

	std::string downloadedFilePath = tmp_path + this->pkg_name + getUrlFileExt();

	// Automatically delete the downloaded zip when this function exits,
	// whether it succeeds or fails (RAII guard).
	struct ZipGuard {
		const std::string& path;
		~ZipGuard() { std::remove(path.c_str()); }
	} zipGuard{ downloadedFilePath };

#ifdef NETWORK_MOCK
	// for network mocking, copy over a /mock.zip to the expected download path
	cp(ROOT_PATH "mock.zip", (downloadedFilePath).c_str());
#endif

	// check file size to see if it's 0 bytes
	struct stat sbuff = {};
	stat((downloadedFilePath).c_str(), &sbuff);
	if (sbuff.st_size == 0)
	{
		printf("--> ERROR: Downloaded file is empty\n");
		return false;
	}

	printf("--> Downloaded file size: %lld\n", (long long)sbuff.st_size);

	// our internal path of where the manifest will be
	std::string ManifestPathInternal = "manifest.install";
	std::string ManifestPath = pkg_path + this->pkg_name + "/" + ManifestPathInternal;

	// before we uninstall, open up the current manifest, and get all the files in it
	// (later we will remove any that aren't in the new manifest)
	Manifest existingManifest(ManifestPath, ROOT_PATH);

	std::unordered_set<std::string> existing_package_paths;
	if (existingManifest.isValid() && manifest.isValid())
	{
		// go through its paths, add them our existing set
		for (const auto& entry : manifest.getEntries())
		{
			ManifestOp op = entry.operation;
			if (op == MUPDATE || op == MEXTRACT)
			{
				existing_package_paths.insert(entry.path);
			}
		}
	}

	printf("--> Existing manifest valid: %d\n", existingManifest.isValid());
	//! Open the Zip file
	UnZip HomebrewZip(downloadedFilePath);
	printf("--> Opened Zip file\n");

	// location of the info.json within the zip
	std::string jsonPathInternal = "info.json";
	std::string jsonPath = pkg_path + this->pkg_name + "/" + jsonPathInternal;
	
	printf("--> Extracting to %s\n", pkg_path.c_str());
	//! Check if it's a valid Zip file
	if (HomebrewZip.IsValid())
	{
		printf("--> Valid Zip file\n");
		//! First extract the Manifest
		HomebrewZip.ExtractFile(ManifestPathInternal, ManifestPath);

		//! Then extract the info.json file (to know what version we have installed and stuff)
		HomebrewZip.ExtractFile(jsonPathInternal, jsonPath);
	} else {
		printf("--> NOTICE: Downloaded file is not a valid zip, just copying to SD root\n");
		// TODO: support other file types, copying to their destinations
	}

	this->manifest = Manifest(ManifestPath, ROOT_PATH);

	printf("--> Manifest valid: %d\n", manifest.isValid());
	if (!manifest.isValid() && manifest.isFakeManifestPossible())
	{
		printf("--> ERROR: Manifest invalid/doesn't exist but recoverable\n");
#ifndef NETWORK_MOCK
		printf("--> Manifest invalid/doesn't exist but recoverable, generating pseudo-manifest\n");
		if (HomebrewZip.IsValid())
		{
			printf("HB zip is valid\n");
			// generate a pseudo-manifest from the zip file
			this->manifest = Manifest(HomebrewZip.PathDump(), ROOT_PATH);
		} else {
			printf("Not a zip but going for it\n");
			// we're not a zip, so just put our one file path in the manifest

			// since we don't know where to put the files, put them in a default app location
			// #if defined(__WIIU__)
			// 	platformDefault = "wiiu/apps/";
			// #elif defined(WII)
			// 	platformDefault = "apps/";
			// #elif defined(SWITCH)
			// 	platformDefault = "switch/";
			// #elif defined(_3DS)
			// 	platformDefault = "3ds/";
			// #endif

			std::string platformDefault = "";
			std::string ext = getUrlFileExt();
			if (ext == ".3dsx") {
				platformDefault = "3ds/";
			} else if (ext == ".cia") {
				// TODO: where should these go? they need a post install step
			} else if (ext == ".nro") {
				platformDefault = "switch/";
			} else if (ext == ".rpx" || ext == ".wuhb") {
				platformDefault = "wiiu/apps/";
			} else {
				// assume wii format
				platformDefault = "apps/";
			}

			printf("Continuing with %s\n", platformDefault.c_str());
			std::string destFilePath = platformDefault + this->pkg_name + ext;
			std::vector<std::string> entries = { destFilePath };
			this->manifest = Manifest(entries, ROOT_PATH);
		}

		// write the pseudo-manifest to the internal package .get directory
		mkpath((pkg_path + this->pkg_name).c_str());

		std::ofstream pseudomanifest(ManifestPath);
		printf("--> Writing pseudo-manifest to %s\n", ManifestPath.c_str());
		for (const auto& entry : manifest.getEntries()) {
			pseudomanifest << entry.raw << std::endl;
		}
		pseudomanifest.close();

		// write a pseudo-info.json here too
		// TODO: load other attributes from the package, besides version
		printf("--> Writing pseudo-info.json to %s\n", jsonPath.c_str());
		std::ofstream pseudojson(jsonPath);
		pseudojson << "{\"version\":\"" << this->version << "\"}" << std::endl;
		pseudojson.close();
#endif
	}

	std::unordered_set<std::string> incoming_package_paths;

	if (manifest.isValid() && HomebrewZip.IsValid())
	{
		// get all file info from within the zip, for every path
		auto infoMap = HomebrewZip.GetPathToFilePosMapping();

		if (libget_status_callback != nullptr)
			libget_status_callback(STATUS_INSTALLING, zipIndex, zipTotal);

		int i = 0;
		const auto& entries = manifest.getEntries();
		for (const auto& entry : entries)
		{
			if (libget_cancelled())
			{
				printf("--> Instalacion cancelada por el usuario\n");
				return false;
			}

			if (networking_callback != nullptr) {
				double progress = (double)(i + 1) / (double)entries.size();
				networking_callback(nullptr, progress);
			}

			i++;

			std::string Path = entry.zip_path;
			std::string ExtractPath = entry.path;
			auto pathCStr = Path.c_str();
			auto ePathCStr = ExtractPath.c_str();

			// track this specific file for later, when we remove files that we don't have entries for
			incoming_package_paths.insert(ExtractPath);

			// lookup this path from our map, to get its file info
			auto mapResult = infoMap.find(Path);
			if (mapResult == infoMap.end())
			{
				// auto onlyZipPaths = HomebrewZip.PathDump();
				// for (auto zipPath : onlyZipPaths)
				// {
				// 	printf("zip path: %s\n", zipPath.c_str());
				// }
				printf("--> ERROR: Could not find [%s] path in zip file\n", pathCStr);
				continue;
			}

			auto filePos = mapResult->second;

			int resp = 0;
			switch (entry.operation)
			{
			case MEXTRACT:
				//! Simply Extract, with no checks or anything, won't be deleted upon removal
				info("%s : EXTRACT\n", pathCStr);
				resp = HomebrewZip.Extract(ePathCStr, filePos);
				break;
			case MUPDATE:
				info("%s : UPDATE\n", pathCStr);
				resp = HomebrewZip.Extract(ePathCStr, filePos);
				break;
			case MGET:
			{
				info("%s : GET\n", pathCStr);
				struct stat sbuff = {};
				if (stat(ePathCStr, &sbuff) != 0) //! File doesn't exist, extract
					resp = HomebrewZip.Extract(ePathCStr, filePos);
				else
					info("File already exists, skipping...");
				break;
			}
			default:
				info("%s : NOP\n", ePathCStr);
				break;
			}

			if (resp < 0)
			{
				printf("--> Some issue happened while extracting! Error: %d\n", resp);
				return false;
			}
		}

		// done installing new files, go through the remaining files that we didn't just visit
		// and remove them (files that WERE in our old manifest, and AREN'T in the new one we got)
		for (auto& path : existing_package_paths)
		{
			// only continue if it's not in our incoming package path set
			if (incoming_package_paths.find(path) == incoming_package_paths.end())
			{
				std::remove(path.c_str());
				// printf("REMOVING: %s\n", path.c_str());
			}
		}
	}
	else
	{
		//! Extract the whole zip
		//		printf("No manifest found: extracting the Zip\n");
		//		HomebrewZip.ExtractAll("sdroot/");
		// TODO: generate a manifest here, it's needed for deletion

		if (!HomebrewZip.IsValid())
		{
			// our zip was no good, so just copy over the file to the destination
			// it's the first file in the pseudo-manifest
			
			// make a folder for the base name of the target file
			auto containingDir = dir_name(manifest.getEntries().front().path);
			mkpath(containingDir.c_str());

			auto firstEntry = manifest.getEntries().front();
			rename(downloadedFilePath.c_str(), firstEntry.path.c_str());
			printf("--> Moved %s to %s\n", downloadedFilePath.c_str(), firstEntry.path.c_str());
		}
		else if (!manifest.isFakeManifestPossible())
		{
			printf("--> Invalid/No manifest file found (or error writing manifest download)! Refusing to extract.\n");
			return false;
		}
	}

	return true;
}

bool Package::downloadZip3(std::string_view tmp_path, float*, bool resume, int zipIndex, int zipTotal) const
{
	if (this->zipUrl3.empty())
		return false;

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_DOWNLOADING, zipIndex, zipTotal);

	printf("--> Downloading third zip for %s to %s%s\n", this->pkg_name.c_str(), tmp_path.data(), resume ? " (resuming)" : "");

	std::string downloadPath = std::string(tmp_path) + this->pkg_name + "_3.zip";
	download_metadata_t metadata = {};

	bool success = downloadFileToDiskWithMetadata(this->zipUrl3, downloadPath, resume, &metadata);

	// cancelado por el usuario: se borra lo descargado a medias (no se reanuda)
	if (!success && libget_cancelled())
	{
		std::remove(downloadPath.c_str());
		std::remove(getMetadataPath(downloadPath).c_str());
		return false;
	}

	if (!success)
	{
		struct stat file_info = {};
		if (stat(downloadPath.c_str(), &file_info) == 0 && file_info.st_size > 0)
		{
			if (!metadata.etag.empty() || metadata.content_length > 0)
			{
				printf("--> Saving download metadata for resume validation\n");
				saveDownloadMetadata(downloadPath, metadata);
			}
		}
		if (resume)
			printf("--> Resume failed. File may have changed on server. Try downloading without --resume\n");
	}
	else
	{
		std::string metapath = getMetadataPath(downloadPath);
		std::remove(metapath.c_str());
	}

	return success;
}

bool Package::downloadZip4(std::string_view tmp_path, float*, bool resume, int zipIndex, int zipTotal) const
{
	if (this->zipUrl4.empty())
		return false;

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_DOWNLOADING, zipIndex, zipTotal);

	printf("--> Downloading fourth zip for %s to %s%s\n", this->pkg_name.c_str(), tmp_path.data(), resume ? " (resuming)" : "");

	std::string downloadPath = std::string(tmp_path) + this->pkg_name + "_4.zip";
	download_metadata_t metadata = {};

	bool success = downloadFileToDiskWithMetadata(this->zipUrl4, downloadPath, resume, &metadata);

	// cancelado por el usuario: se borra lo descargado a medias (no se reanuda)
	if (!success && libget_cancelled())
	{
		std::remove(downloadPath.c_str());
		std::remove(getMetadataPath(downloadPath).c_str());
		return false;
	}

	if (!success)
	{
		struct stat file_info = {};
		if (stat(downloadPath.c_str(), &file_info) == 0 && file_info.st_size > 0)
		{
			if (!metadata.etag.empty() || metadata.content_length > 0)
			{
				printf("--> Saving download metadata for resume validation\n");
				saveDownloadMetadata(downloadPath, metadata);
			}
		}
		if (resume)
			printf("--> Resume failed. File may have changed on server. Try downloading without --resume\n");
	}
	else
	{
		std::string metapath = getMetadataPath(downloadPath);
		std::remove(metapath.c_str());
	}

	return success;
}

bool Package::install2(const std::string& pkg_path, const std::string& tmp_path, int zipIndex, int zipTotal)
{
	if (this->zipUrl2.empty())
		return true; // no second zip, nothing to do

	printf("Going to install second zip for %s\n", this->pkg_name.c_str());

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_INSTALLING, zipIndex, zipTotal);

	// forzar un redibujado inmediato para que el cambio de texto a
	// "Instalando... (2/2)" se vea reflejado en pantalla de inmediato,
	// en vez de quedar congelado con el ultimo frame de la descarga
	if (networking_callback != nullptr)
		networking_callback(nullptr, 0.0);  // resetea la barra a 0% al iniciar extraccion

	std::string downloadedFilePath = tmp_path + this->pkg_name + "_2.zip";

	struct ZipGuard {
		const std::string& path;
		~ZipGuard() { std::remove(path.c_str()); }
	} zipGuard{ downloadedFilePath };

	struct stat sbuff = {};
	stat(downloadedFilePath.c_str(), &sbuff);
	if (sbuff.st_size == 0)
	{
		printf("--> ERROR: Second downloaded file is empty\n");
		return false;
	}

	UnZip HomebrewZip(downloadedFilePath);
	if (!HomebrewZip.IsValid())
	{
		printf("--> ERROR: Second downloaded file is not a valid zip\n");
		return false;
	}

	// extract all files from the second zip directly, no manifest needed
	if (HomebrewZip.ExtractAllWithProgress(ROOT_PATH) == -9)
		return false; // cancelado por el usuario

	printf("--> Second zip installed for %s\n", this->pkg_name.c_str());
	return true;
}

bool Package::install3(const std::string& pkg_path, const std::string& tmp_path, int zipIndex, int zipTotal)
{
	if (this->zipUrl3.empty())
		return true;

	printf("Going to install third zip for %s\n", this->pkg_name.c_str());

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_INSTALLING, zipIndex, zipTotal);

	// forzar redibujado inmediato del texto de estado
	if (networking_callback != nullptr)
		networking_callback(nullptr, 0.0);  // resetea la barra a 0% al iniciar extraccion

	std::string downloadedFilePath = tmp_path + this->pkg_name + "_3.zip";

	struct ZipGuard {
		const std::string& path;
		~ZipGuard() { std::remove(path.c_str()); }
	} zipGuard{ downloadedFilePath };

	struct stat sbuff = {};
	stat(downloadedFilePath.c_str(), &sbuff);
	if (sbuff.st_size == 0)
	{
		printf("--> ERROR: Third downloaded file is empty\n");
		return false;
	}

	UnZip HomebrewZip(downloadedFilePath);
	if (!HomebrewZip.IsValid())
	{
		printf("--> ERROR: Third downloaded file is not a valid zip\n");
		return false;
	}

	if (HomebrewZip.ExtractAllWithProgress(ROOT_PATH) == -9)
		return false; // cancelado por el usuario

	printf("--> Third zip installed for %s\n", this->pkg_name.c_str());
	return true;
}

bool Package::install4(const std::string& pkg_path, const std::string& tmp_path, int zipIndex, int zipTotal)
{
	if (this->zipUrl4.empty())
		return true;

	printf("Going to install fourth zip for %s\n", this->pkg_name.c_str());

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_INSTALLING, zipIndex, zipTotal);

	// forzar redibujado inmediato del texto de estado
	if (networking_callback != nullptr)
		networking_callback(nullptr, 0.0);  // resetea la barra a 0% al iniciar extraccion

	std::string downloadedFilePath = tmp_path + this->pkg_name + "_4.zip";

	struct ZipGuard {
		const std::string& path;
		~ZipGuard() { std::remove(path.c_str()); }
	} zipGuard{ downloadedFilePath };

	struct stat sbuff = {};
	stat(downloadedFilePath.c_str(), &sbuff);
	if (sbuff.st_size == 0)
	{
		printf("--> ERROR: Fourth downloaded file is empty\n");
		return false;
	}

	UnZip HomebrewZip(downloadedFilePath);
	if (!HomebrewZip.IsValid())
	{
		printf("--> ERROR: Fourth downloaded file is not a valid zip\n");
		return false;
	}

	if (HomebrewZip.ExtractAllWithProgress(ROOT_PATH) == -9)
		return false; // cancelado por el usuario

	printf("--> Fourth zip installed for %s\n", this->pkg_name.c_str());
	return true;
}

// ---------------------------------------------------------------------------
// Actualizacion incremental ("actualizacion" en repo.json)
//
// El zip de actualizacion trae SOLO los archivos que cambiaron, mas:
//   - info.json         (con la version nueva)
//   - manifest.install  (con TODOS los componentes del paquete, no solo los nuevos)
//
// Los archivos que aparecen en el manifest nuevo pero no vienen en el zip se
// consideran sin cambios y se dejan como estan en la SD.
// ---------------------------------------------------------------------------
bool Package::downloadUpdateZip(std::string_view tmp_path) const
{
	if (this->update_url.empty())
		return false;

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_DOWNLOADING, 1, 1);

	printf("--> Downloading update zip for %s\n", this->pkg_name.c_str());

	std::string downloadPath = std::string(tmp_path) + this->pkg_name + "_upd.zip";

	// never resume an old partial: always start clean
	std::remove(getMetadataPath(downloadPath).c_str());
	std::remove(downloadPath.c_str());

	download_metadata_t metadata = {};
	bool success = downloadFileToDiskWithMetadata(this->update_url, downloadPath, false, &metadata);

	// cancelado por el usuario: se borra lo descargado a medias (no se reanuda)
	if (!success && libget_cancelled())
	{
		std::remove(downloadPath.c_str());
		std::remove(getMetadataPath(downloadPath).c_str());
		return false;
	}
	if (!success)
		std::remove(downloadPath.c_str());

	return success;
}

int Package::installUpdate(const std::string& pkg_path, const std::string& tmp_path)
{
	printf("Going to install the update zip for %s\n", this->pkg_name.c_str());

	// si la actualizacion no se puede aplicar NO se descargan los zips completos
	// (si el repo trae "actualizacion", actualizar usa solo eso): se guarda el motivo
	// para mostrarlo al usuario
	auto fb = [this](const char* why) {
		this->runtime_install_status = std::string("No se pudo aplicar la actualizacion:\n") + why;
		return UPDATE_FALLBACK;
	};

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_ANALYZING, 1, 1);
	if (networking_callback != nullptr)
		networking_callback(nullptr, 1.0);

	const std::string zipPath = tmp_path + this->pkg_name + "_upd.zip";

	// delete the downloaded zip when we leave, whatever happens
	struct ZipGuard
	{
		const std::string& path;
		~ZipGuard() { std::remove(path.c_str()); }
	} zipGuard{ zipPath };

	struct stat sbuff = {};
	if (stat(zipPath.c_str(), &sbuff) != 0 || sbuff.st_size == 0)
	{
		printf("--> Update zip is missing or empty\n");
		return fb("El archivo de actualizacion no se descargo bien (vacio o inexistente).");
	}

	const std::string pkgDir = pkg_path + this->pkg_name + "/";
	const std::string manifestPath = pkgDir + "manifest.install";
	const std::string infoPath = pkgDir + "info.json";
	// the new files are written next to the real ones and only swapped in at the very end,
	// so a bad update zip never leaves the package with a half-new manifest/version
	const std::string newManifestPath = manifestPath + ".new";
	const std::string newInfoPath = infoPath + ".new";

	auto cleanupStaging = [&]()
	{
		std::remove(newManifestPath.c_str());
		std::remove(newInfoPath.c_str());
	};

	// we can only update something that is installed (and knows its files)
	Manifest oldManifest(manifestPath, ROOT_PATH);
	if (!oldManifest.isValid())
	{
		printf("--> No valid installed manifest, cannot apply an incremental update\n");
		return fb("El componente instalado no tiene un manifest valido, asi que no se puede aplicar la actualizacion.");
	}

	UnZip zip(zipPath);
	if (!zip.IsValid())
	{
		printf("--> Update file is not a valid zip (server error page?)\n");
		return fb("El archivo de actualizacion no es un zip valido.");
	}

	if (zip.ExtractFile("manifest.install", newManifestPath) != 0 || zip.ExtractFile("info.json", newInfoPath) != 0)
	{
		printf("--> Update zip must contain manifest.install and info.json\n");
		cleanupStaging();
		return fb("El zip de actualizacion debe incluir manifest.install e info.json.");
	}

	Manifest newManifest(newManifestPath, ROOT_PATH);
	if (!newManifest.isValid())
	{
		printf("--> Update zip has an invalid manifest.install\n");
		cleanupStaging();
		return fb("El manifest.install del zip de actualizacion no es valido.");
	}

	// the version inside the new info.json must be the one the repo advertises,
	// otherwise the package would keep showing "update available" forever
	{
		std::ifstream ifs(newInfoPath.c_str());
		rapidjson::IStreamWrapper isw(ifs);
		rapidjson::Document doc;
		rapidjson::ParseResult ok = doc.ParseStream(isw);
		std::string newVersion;
		if (ok && doc.IsObject() && doc.HasMember("version") && doc["version"].IsString())
			newVersion = doc["version"].GetString();

		if (newVersion != this->version)
		{
			printf("--> Update zip version (%s) doesn't match the repo version (%s)\n", newVersion.c_str(),
				   this->version.c_str());
			cleanupStaging();
			return fb("La version dentro del zip de actualizacion no coincide con la del repositorio.");
		}
	}

	auto infoMap = zip.GetPathToFilePosMapping();

	// the file of the "instalacion" order (.nsp/.nsz/.xci) is deleted from the SD
	// right after it is installed, so it is never "already on the SD card"
	std::string installFilePath;
	if (!this->install_nsp.empty())
	{
		std::string rel = this->install_nsp;
		while (!rel.empty() && rel.front() == '/')
			rel.erase(0, 1);
		installFilePath = std::string(ROOT_PATH) + rel;
	}

	// los archivos del manifest nuevo que no vienen en este zip se dan por "sin
	// cambios" y se esperan ya en la SD. Si alguno no esta, NO se aborta (ya no hay
	// descarga completa de respaldo, y un manifest con una ruta que nunca existio no
	// debe bloquear la actualizacion): se anota para avisar al terminar.
	std::vector<std::string> missingOnSd;
	for (const auto& entry : newManifest.getEntries())
	{
		if (entry.operation != MEXTRACT && entry.operation != MUPDATE && entry.operation != MGET)
			continue;
		if (infoMap.find(entry.zip_path) != infoMap.end())
			continue;
		if (!installFilePath.empty() && entry.path == installFilePath)
			continue; // already installed and deleted: nothing to check

		struct stat fileStat = {};
		if (stat(entry.path.c_str(), &fileStat) != 0)
		{
			printf("--> File [%s] is listed in the new manifest but is neither in the update zip nor on the SD card\n",
				   entry.path.c_str());
			missingOnSd.push_back(entry.zip_path); // solo para el registro de la consola
		}
	}

	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_INSTALLING, 1, 1);

	const auto& entries = newManifest.getEntries();
	int i = 0;
	for (const auto& entry : entries)
	{
		if (libget_cancelled())
		{
			cleanupStaging(); // el manifest/info.json instalados no se tocaron
			return UPDATE_CANCELLED;
		}

		if (networking_callback != nullptr)
			networking_callback(nullptr, (double)(i + 1) / (double)entries.size());
		i++;

		auto found = infoMap.find(entry.zip_path);
		if (found == infoMap.end())
			continue; // unchanged, keep the file that is already installed

		int resp = 0;
		switch (entry.operation)
		{
		case MEXTRACT:
		case MUPDATE:
			resp = zip.Extract(entry.path, found->second);
			break;
		case MGET:
		{
			struct stat fileStat = {};
			if (stat(entry.path.c_str(), &fileStat) != 0)
				resp = zip.Extract(entry.path, found->second);
			break;
		}
		default:
			break;
		}

		if (resp == -9)
		{
			cleanupStaging();
			return UPDATE_CANCELLED;
		}
		if (resp < 0)
		{
			printf("--> Some issue happened while applying the update! Error: %d\n", resp);
			cleanupStaging();
			return UPDATE_FAILED;
		}
	}

	// NOTE: files of the old version that are not in the new manifest are left
	// alone on purpose (an incremental update never deletes anything).

	// swap in the new manifest first and info.json (the version marker) last
	std::remove(manifestPath.c_str());
	if (rename(newManifestPath.c_str(), manifestPath.c_str()) != 0)
	{
		printf("--> Could not write the new manifest\n");
		cleanupStaging();
		return UPDATE_FAILED;
	}
	std::remove(infoPath.c_str());
	if (rename(newInfoPath.c_str(), infoPath.c_str()) != 0)
	{
		printf("--> Could not write the new info.json\n");
		cleanupStaging();
		return UPDATE_FAILED;
	}

	this->manifest = Manifest(manifestPath, ROOT_PATH);
	printf("--> Incremental update applied for %s\n", this->pkg_name.c_str());

	return UPDATE_OK;
}

void Package::removeTempDownloads(std::string_view tmp_path) const
{
	const std::string base = std::string(tmp_path) + this->pkg_name;
	const std::string files[] = { base + getUrlFileExt(), base + "_2.zip", base + "_3.zip", base + "_4.zip", base + "_upd.zip" };
	for (const auto& f : files)
	{
		std::remove(f.c_str());
		std::remove(getMetadataPath(f).c_str());
	}
}

bool Package::writeDirectInstallRecord(const std::string& pkg_path)
{
	const std::string dir = pkg_path + this->pkg_name + "/";
	if (!mkpath(dir))
		return false;

	// Manifest de una sola linea que no es operacion (se ignora): queda valido
	// pero no lista ningun archivo, asi que desinstalar no borra nada de la SD.
	const std::string manifestPath = dir + "manifest.install";
	std::ofstream manifestFile(manifestPath);
	manifestFile << "# instalacion directa (sin archivos en la SD)" << std::endl;
	manifestFile.close();
	if (!manifestFile)
		return false;

	const std::string infoPath = dir + "info.json";
	std::ofstream infoFile(infoPath);
	infoFile << "{\"version\":\"" << this->version << "\"}" << std::endl;
	infoFile.close();
	return static_cast<bool>(infoFile);
}

bool Package::remove(std::string_view pkg_path)
{
	if (libget_status_callback != nullptr)
		libget_status_callback(STATUS_REMOVING, 1, 1);

	// perform an uninstall of the current package, parsing the cached metadata
	std::string ManifestPathInternal = "manifest.install";
	std::string ManifestPath = std::string(pkg_path) + this->pkg_name + "/" + ManifestPathInternal;

	info("HomebrewManager::Delete\n");
	std::unordered_set<std::string> uniq_folders;

	//! Parse the manifest
	info("Parsing the Manifest\n");
	if (!manifest.isValid())
	{
		this->manifest = Manifest(ManifestPath, ROOT_PATH); // Load and parse manifest if not yet done
	}
	if (this->manifest.isValid())
	{
		int i = 0;
		const auto& entries = manifest.getEntries();
		for (const auto& entry : entries)
		{
			if (networking_callback != nullptr) {
				double progress = (double)(i + 1) / (double)entries.size();
				networking_callback(nullptr, progress);
			}
			i++;
			const std::string& DeletePath = entry.path;

			// the current directory
			std::string cur_dir = dir_name(DeletePath);
			uniq_folders.insert(cur_dir);

			ManifestOp op = entry.operation;
			if (op != NOP && op != MEXTRACT) // get, upgrade, and local
			{
				info("Removing %s\n", DeletePath.c_str());
				std::remove(DeletePath.c_str());
			}
		}
	}
	else
	{
		printf("--> ERROR: Manifest missing or invalid at %s\n", ManifestPath.c_str());
		return false;
	}

	// sort unique folders from longest to shortest
	std::vector<std::string> folders;
	for (auto& folder : uniq_folders)
	{
		folders.push_back(folder);
	}
	std::sort(folders.begin(), folders.end(), compareLen);

	std::vector<std::string> intermediate_folders;

	// rmdir (only works if folders are empty!) out all uniq dirs...
	std::string fsroot(ROOT_PATH);
	for (auto& folder : folders)
	{
		auto parent = dir_name(folder);
		while (!parent.empty())
		{
			std::cout << "processing... " << parent << "\n";
			if ((uniq_folders.find(parent) == uniq_folders.end()) && (parent.length() > fsroot.length()))
			{
				std::cout << "adding " << parent << "\n";

				// folder not already seen, track it
				uniq_folders.insert(parent);
				intermediate_folders.push_back(parent);
			}
			parent = dir_name(parent);
		}
	}

	// have to re-add these outside of the loop because we can't
	// modify the vector while iterating through it
	for (auto& folder : intermediate_folders)
		folders.push_back(folder);

	// re-sort it
	std::sort(folders.begin(), folders.end(), compareLen);

	for (auto& folder : folders)
	{
		rmdir(folder.c_str());
	}

	printf("--> Removing manifest...\n");

	std::remove(ManifestPath.c_str());
	auto full_pkg_path = std::string(pkg_path) + this->pkg_name;
	std::remove((full_pkg_path + "/info.json").c_str());
	std::remove((full_pkg_path + "/icon.jpg").c_str()); // clean up icon if present
	std::remove((full_pkg_path + "/icon.png").c_str()); // por si quedo un icono viejo cacheado en png de una version anterior

	rmdir((std::string(pkg_path) + this->pkg_name).c_str());

	// package removed, clean up empty directories
	// TODO: potentially prompt user to remove some known config files for a given package
	// see: https://github.com/vgmoose/get/issues/1
	// remove_empty_dirs(ROOT_PATH, 0);

	printf("--> Homebrew removed\n");

	return true;
}

void Package::updateStatus(const std::string& pkg_path)
{
	// check if the manifest for this package exists
	std::string ManifestPathInternal = "manifest.install";
	std::string ManifestPath = pkg_path + this->pkg_name + "/" + ManifestPathInternal;

	struct stat sbuff = {};
	if (stat(ManifestPath.c_str(), &sbuff) == 0)
	{
		// manifest exists, we are at least installed
		this->status = INSTALLED;

		this->manifest = Manifest(ManifestPath, ROOT_PATH);
	}

	// check for info.json, parse version out of it
	// and compare against the package's to know whether
	// it's an update or not
	std::string jsonPathInternal = "info.json";
	std::string jsonPath = pkg_path + this->pkg_name + "/" + jsonPathInternal;

	if (INSTALLED && stat(jsonPath.c_str(), &sbuff) == 0)
	{
		// pull out the version number and check if it's
		// different than the one on the repo
		std::ifstream ifs(jsonPath.c_str());
		rapidjson::IStreamWrapper isw(ifs);

		if (!ifs.good())
		{
			printf("--> Could not locate %s", jsonPath.c_str());
			this->status = UPDATE; // issue opening info.json, assume update
			return;
		}

		rapidjson::Document doc;
		rapidjson::ParseResult ok = doc.ParseStream(isw);
		std::string tmpVersion;

		if (ok && doc.HasMember("version"))
		{
			const rapidjson::Value& info_doc = doc["version"];
			tmpVersion = info_doc.GetString();
		}
		else
			tmpVersion = "0.0.0";

		this->installed_version = tmpVersion;

		if (tmpVersion != this->version)
			this->status = UPDATE;

		// we're eithe ran update or an install at this point
		return;
	}
	else if (this->status == INSTALLED)
	{
		this->status = UPDATE; // manifest, but no info, always update
		return;
	}

	// if we're down here, and it's not a local package
	// already, it's probably a get package (package was
	// available, but the manifest wasn't installed)
	if (this->status != LOCAL)
		this->status = GET;

	// check for any homebrew that may have been previously installed
	// TODO: see https://github.com/vgmoose/hb-appstore/issues/20
	this->status = this->isPreviouslyInstalled();
}

int Package::isPreviouslyInstalled()
{
	// TODO: check for and scan Switch NRO files
#if defined(__WIIU__)
	// we're on a Wii U, so let's check for any HBL meta.xml files that match this package's name,
	// and if it exists check the version based on that
	// TODO: check for and scan WUHB files
	TiXmlDocument xmlDoc((std::string(ROOT_PATH) + "wiiu/apps/" + this->pkg_name + "/meta.xml").c_str());
	bool xmlExists = xmlDoc.LoadFile();

	if (xmlExists)
	{
		TiXmlElement* appNode = xmlDoc.FirstChildElement("app");
		if (appNode)
		{
			TiXmlElement* node = appNode->FirstChildElement("version");
			if (node && node->FirstChild() && node->FirstChild()->Value())
			{
				// version exists, we should compare the value to the one on the server (this package)
				if (this->version != node->FirstChild()->Value())
					return UPDATE;
				else
					return LOCAL;
			}
		}
	}
#endif

	// since we are appstore and know that what version we're supposed to be, mark us local or updated if needed
	// TODO: make version check here dynamic, and also support other NROs or hint files
	// notice: this means that even if appstore isn't installed but is running, it will show as an update
	if (this->pkg_name == APP_SHORTNAME)
	{
		// it's app store, but wasn't detected as installed
		if (this->version == APP_VERSION)
			return LOCAL;
		else
			return UPDATE;
	}

	return this->status;
}

const char* Package::statusString() const
{
	switch (this->status)
	{
	case LOCAL:
		return "LOCAL";
	case INSTALLED:
		return "INSTALLED";
	case UPDATE:
		return "UPDATE";
	case GET:
		return "GET";
	}
	return "UNKNOWN";
}

const char* Package::statusStringDisplay() const
{
	switch (this->status)
	{
	case LOCAL:
		return "LOCAL";
	case INSTALLED:
		return "INSTALADO";
	case UPDATE:
		return "ACTUALIZAR";
	case GET:
		return "OBTENER";
	}
	return "DESCONOCIDO";
}

std::string Package::getIconUrl() const
{
	// ask the parent repo for the icon url TODO: some fallback?
	if (this->mRepo == nullptr)
	{
		printf("--> ERROR: Parent repo not set for package %s\n", this->pkg_name.c_str());
		return "";
	}
	return this->mRepo->getIconUrl(*this);
}

std::string Package::getBannerUrl() const
{
	return this->mRepo->getUrl() + "/packages/" + this->pkg_name + "/screen.jpg";
}

std::string Package::getScreenShotUrl(int count) const
{
	if (!this->mRepo) {
		return "";
	}
	return this->mRepo->getUrl() + "/packages/" + this->pkg_name + "/screen" + std::to_string(count) + ".jpg";
}

std::string Package::getManifestUrl() const
{
	if (!this->mRepo) {
		return "";
	}
	return this->mRepo->getUrl() + "/packages/" + this->pkg_name + "/manifest.install";
}
