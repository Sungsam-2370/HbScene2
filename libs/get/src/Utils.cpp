// operating system level utilities
// contains directory utils, http utils, and helper methods

// CURLOPT_PROGRESSFUNCTION deprecated in 7.32.0 and is replaced with CURLOPT_XFERINFOFUNCTION
#include <curl/curl.h>
#if LIBCURL_VERSION_NUM >= 0x072000
	#define COMPAT_CURL_PROGRESS_OPTION CURLOPT_XFERINFOFUNCTION
#else
	#define COMPAT_CURL_PROGRESS_OPTION CURLOPT_PROGRESSFUNCTION
#endif

#if defined(WII) && !defined(NETWORK_MOCK)
#include <wiisocket.h>
#endif

#if defined(SWITCH)
#include <switch.h>
#endif

#if defined(_3DS)
#include <3ds.h>
#include <malloc.h>
#endif

#if defined(WIN32)
#include <sys/types.h>
#endif

#include <algorithm>
#include <deque>
#include <vector>
#include <cstdint>
#include <ctime>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <iomanip>
#include <locale>

#include "Utils.hpp"

// resinfs support, if present
#if defined(USE_RAMFS)
#define RAMFS "resin:/"
#else
#define RAMFS "resin/"
#endif

#define BUF_SIZE 0x800000 // 8MB.

libget_progress_callback_t networking_callback = nullptr;
int (*libget_status_callback)(int, int, int) = nullptr;
void* networking_callback_data = nullptr; // User data to pass to callback

static const char* USER_AGENT = "libget-unknown/0.0.0";

// different signature depending on curl version
#ifndef NETWORK_MOCK
#if LIBCURL_VERSION_NUM >= 0x072000
static int libget_curl_progress_wrapper(void* /*clientp*/, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t)
#else
static int libget_curl_progress_wrapper(void* /*clientp*/, double dltotal, double dlnow, double, double)
#endif
{
	if (networking_callback == nullptr)
		return 0;
	
	if (dltotal == 0)
		return networking_callback(networking_callback_data, 0.0);
	
	double progress = (double)dlnow / (double)dltotal;
	
	// don't go OOB
	if (progress > 1.0) {
		progress = 1.0;
	}
	
	return networking_callback(networking_callback_data, progress);
}
#endif

// reference to the curl handle so that we can re-use the connection
#ifndef NETWORK_MOCK
CURL* curl = nullptr;
std::string gLastCurlErrorMsg = "";
#endif

#define SOCU_ALIGN 0x1000
#define SOCU_BUFFERSIZE 0x100000

#ifndef SO_TCPSACK
#define SO_TCPSACK 0x00200 /* Allow TCP SACK (Selective acknowledgment) */
#endif

#ifndef SO_WINSCALE
#define SO_WINSCALE 0x00400 /* Set scaling window option */
#endif

#ifndef SO_RCVBUF
#define SO_RCVBUF 0x01002 /* Receive buffer size */
#endif

#ifndef NETWORK_MOCK
// networking optimizations adapted from:
//  - https://github.com/samdejong86/Arria-V-ADC-Ethernet-software/blob/master/ADC_Socket_bsp/iniche/src/h/socket.h
int sockopt_callback(void* clientp __attribute__((unused)), curl_socket_t curlfd, curlsocktype purpose __attribute__((unused)))
{
#if defined(SWITCH)
	// throughput per connection is limited by (receive window / RTT).
	// with 128KB and ~80ms RTT that is only ~1.5MB/s, so ask for a much bigger window
	// (must stay <= tcp_rx_buf_max_size configured in init_networking)
	int winscale = 1, rcvbuf = 0x80000, tcpsack = 1;
#else
	int winscale = 1, rcvbuf = 0x20000, tcpsack = 1;
#endif
#ifndef WIN32
	setsockopt(curlfd, SOL_SOCKET, SO_WINSCALE, &winscale, sizeof(int));
	setsockopt(curlfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(int));
	setsockopt(curlfd, SOL_SOCKET, SO_TCPSACK, &tcpsack, sizeof(int));
#endif
	return 0;
}
#endif

#if defined(_3DS)
u32* SOCUBuffer;
#endif

// sets the user agent for our requests
void setUserAgent(const char* agent)
{
	USER_AGENT = agent;
}

bool CreateSubfolder(std::string_view path)
{
	return mkpath(path.data());
}

// wrapper for unix mkdir
int my_mkdir(const std::string& path, int perms)
{
#if defined(WIN32)
	return mkdir(path.c_str());
#else
	return mkdir(path.c_str(), perms);
#endif
}

// platform independent strptime
//
// NOTE: this used to be implemented with std::get_time() over an
// istringstream imbued with the current locale. That relies on the C++
// standard library's locale/time-parsing facilities, which are unreliable
// (often silently fail, always returning input.fail() == true) on the
// devkitA64/newlib toolchain used to build the Switch binary. When that
// happens EVERY package ends up with updated_timestamp == 0 (the default),
// which in turn makes any "sort by most recently updated" feature (like the
// "Novedades" category) silently degrade into alphabetical-by-name order,
// since all timestamps compare equal and sorting falls through to the
// tiebreaker. To avoid depending on locale support at all, only the two
// formats actually used in this codebase are hand-parsed with sscanf below.
char* my_strptime(const char* s,
	const char* f,
	struct tm* tm)
{
	*tm = {};

	int a = 0, b = 0, c = 0, hh = 0, mm = 0, ss = 0;

	if (strcmp(f, "%d/%m/%Y") == 0)
	{
		// day/month/year, e.g. "20/05/2026"
		if (sscanf(s, "%d/%d/%d", &a, &b, &c) != 3)
			return nullptr;

		tm->tm_mday = a;
		tm->tm_mon = b - 1;
		tm->tm_year = c - 1900;
	}
	else if (strcmp(f, "%Y-%m-%dT%H:%M:%SZ") == 0)
	{
		// ISO-8601, e.g. "2026-05-20T00:00:00Z"
		if (sscanf(s, "%d-%d-%dT%d:%d:%dZ", &c, &b, &a, &hh, &mm, &ss) != 6)
			return nullptr;

		tm->tm_mday = a;
		tm->tm_mon = b - 1;
		tm->tm_year = c - 1900;
		tm->tm_hour = hh;
		tm->tm_min = mm;
		tm->tm_sec = ss;
	}
	else
	{
		// unsupported format string; add a manual sscanf-based branch above
		// rather than falling back to locale-dependent parsing
		printf("--> my_strptime: unsupported format \"%s\"\n", f);
		return nullptr;
	}

	// basic sanity check on the parsed date, so obviously malformed input
	// (e.g. a field that wasn't actually a date) doesn't get treated as valid
	if (tm->tm_mon < 0 || tm->tm_mon > 11 || tm->tm_mday < 1 || tm->tm_mday > 31 || tm->tm_year < 0)
		return nullptr;

	tm->tm_isdst = -1;

	return (char*)(s + strlen(s));
}

// http://stackoverflow.com/a/11366985
bool mkpath(const std::string& path)
{
	bool bSuccess = false;
	int nRC = my_mkdir(path, 0775);
	if (nRC == -1)
	{
		switch (errno)
		{
		case ENOENT:
			// parent didn't exist, try to create it
			if (mkpath(path.substr(0, path.find_last_of('/'))))
				// Now, try to create again.
				bSuccess = 0 == my_mkdir(path, 0775);
			else
				bSuccess = false;
			break;
		case EEXIST:
			// Done!
			bSuccess = true;
			break;
		default:
			bSuccess = false;
			break;
		}
	}
	else
		bSuccess = true;
	return bSuccess;
}

#ifndef NETWORK_MOCK
void setPlatformCurlFlags(CURL* c)
{
	// // from https://github.com/GaryOderNichts/wiiu-examples/blob/main/curl-https/romfs/cacert.pem
	curl_easy_setopt(c, CURLOPT_CAINFO, RAMFS "res/cacert.pem");

	curl_easy_setopt(c, CURLOPT_SOCKOPTFUNCTION, sockopt_callback);

	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); // follow redirects

	// set the user agent
	curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);

	// read from the socket in bigger chunks (default is 16KB)
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 512L * 1024L);
	curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
}

// reset curl handle to a clean state to prevent state pollution between requests
void resetCurlToCleanState(CURL* c)
{
	if (!c) return;
		
	// clear callbacks which might reference freed objects
	curl_easy_setopt(c, COMPAT_CURL_PROGRESS_OPTION, nullptr);
	curl_easy_setopt(c, CURLOPT_PROGRESSDATA, nullptr);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1);
	
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, nullptr);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, nullptr);
	
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, nullptr);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, nullptr);
	
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, nullptr);
	curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0);
	curl_easy_setopt(c, CURLOPT_NOBODY, 0L);
	
	curl_easy_setopt(c, CURLOPT_URL, nullptr);
	
	// platform-specific flags are left set as they should persist across all requests
}
#endif

static size_t MemoryWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

// used to fill out our metadata files for partial download support
static size_t HeaderCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	auto* metadata = (download_metadata_t*)userp;
	size_t realsize = size * nmemb;
	std::string header((char*)contents, realsize);
	
	if (header.find("ETag:") == 0 || header.find("etag:") == 0) {
		size_t start = header.find(':') + 1;
		size_t end = header.find('\r', start);
		if (end == std::string::npos) end = header.find('\n', start);
		if (start != std::string::npos && end != std::string::npos) {
			metadata->etag = header.substr(start, end - start);
			metadata->etag.erase(0, metadata->etag.find_first_not_of(" \t"));
			metadata->etag.erase(metadata->etag.find_last_not_of(" \t\r\n") + 1);
		}
	}
	else if (header.find("Last-Modified:") == 0 || header.find("last-modified:") == 0) {
		size_t start = header.find(':') + 1;
		size_t end = header.find('\r', start);
		if (end == std::string::npos) end = header.find('\n', start);
		if (start != std::string::npos && end != std::string::npos) {
			metadata->last_modified = header.substr(start, end - start);
			metadata->last_modified.erase(0, metadata->last_modified.find_first_not_of(" \t"));
			metadata->last_modified.erase(metadata->last_modified.find_last_not_of(" \t\r\n") + 1);
		}
	}
	
	return realsize;
}

static size_t DiskWriteCallback(void* contents, size_t size, size_t num_files, void* userp)
{
	auto* data_struct = (ntwrk_struct_t*)userp;
	size_t realsize = size * num_files;

	if (realsize + data_struct->offset >= data_struct->data_size)
	{
		fwrite(data_struct->data, data_struct->offset, 1, data_struct->out);
		data_struct->offset = 0;
	}

	memcpy(&data_struct->data[data_struct->offset], contents, realsize);
	data_struct->offset += realsize;
	data_struct->data[data_struct->offset] = 0;
	return realsize;
}

// https://gist.github.com/alghanmi/c5d7b761b2c9ab199157
// if data_struct is specified, file will go straight to disk as it downloads
bool downloadFileCommon(const std::string& path, std::string* buffer = nullptr, ntwrk_struct_t* data_struct = nullptr)
{
#ifndef NETWORK_MOCK
	if (!buffer && !data_struct)
	{
		return false;
	}

	if (!curl)
		return false;

	setPlatformCurlFlags(curl);

	curl_easy_setopt(curl, CURLOPT_URL, path.c_str());
	
	if (networking_callback != nullptr) {
		printf("[downloadFileCommon] Setting progress callback (networking_callback=%p)\n", networking_callback);
		curl_easy_setopt(curl, COMPAT_CURL_PROGRESS_OPTION, libget_curl_progress_wrapper);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, curl);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	} else {
		printf("[downloadFileCommon] Disabling progress callbacks (networking_callback is nullptr)\n");
		curl_easy_setopt(curl, COMPAT_CURL_PROGRESS_OPTION, nullptr);
		curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, nullptr);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	}

	bool skipDisk = data_struct == nullptr;

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, skipDisk ? MemoryWriteCallback : DiskWriteCallback);

	if (skipDisk)
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
	else
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, data_struct);

	CURLcode curlResult = curl_easy_perform(curl);
	bool success = (curlResult == CURLE_OK);

	if (!success)
	{
		gLastCurlErrorMsg = curl_easy_strerror(curlResult);
		printf("[downloadFileCommon] curl error: %s\n", gLastCurlErrorMsg.c_str());
	}
	else
	{
		gLastCurlErrorMsg.clear();
	}

	resetCurlToCleanState(curl);
	
	return success;
#else
	return true;
#endif
}

bool downloadFileToMemory(const std::string& path, std::string* buffer)
{
	return downloadFileCommon(path, buffer, nullptr);
}

bool downloadFileToDisk(const std::string& remote_path, const std::string& local_path, bool resume)
{
	long resume_from = 0;
	
	if (resume)
	{
		struct stat file_info = {};
		if (stat(local_path.c_str(), &file_info) == 0)
		{
			resume_from = file_info.st_size;
			printf("--> Resuming download from byte %ld\n", resume_from);
		}
	}
	
	// either append or write mode depending on resuming status
	FILE* out_file = fopen(local_path.c_str(), resume && resume_from > 0 ? "ab" : "wb");
	if (!out_file)
		return false;

	// automatically close the file if it leaves the scope.
	auto file = std::unique_ptr<FILE, int (*)(FILE*)>(out_file, &::fclose);

	auto buf = std::make_unique<uint8_t[]>(BUF_SIZE); // 8MB.
	if (buf == nullptr)
	{
		return false;
	}

	ntwrk_struct_t data_struct = { buf.get(), BUF_SIZE, 0, out_file };

#ifndef NETWORK_MOCK
	if (resume && resume_from > 0)
	{
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resume_from);
	}
	else
	{
		// no resume
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0);
	}
#endif

	if (!downloadFileCommon(remote_path, nullptr, &data_struct))
	{
		return false;
	}

	// write remaining data to file before free.
	fwrite(data_struct.data, data_struct.offset, 1, data_struct.out);
	return true;
}

std::string getMetadataPath(const std::string& filepath)
{
	return filepath + ".metadata";
}

bool saveDownloadMetadata(const std::string& filepath, const download_metadata_t& metadata)
{
	std::string metapath = getMetadataPath(filepath);
	std::ofstream metafile(metapath);
	if (!metafile)
		return false;
	
	metafile << "etag=" << metadata.etag << std::endl;
	metafile << "last_modified=" << metadata.last_modified << std::endl;
	metafile << "content_length=" << metadata.content_length << std::endl;
	
	return true;
}

bool loadDownloadMetadata(const std::string& filepath, download_metadata_t* metadata)
{
	std::string metapath = getMetadataPath(filepath);
	std::ifstream metafile(metapath);
	if (!metafile)
		return false;
	
	std::string line;
	while (std::getline(metafile, line))
	{
		// basic .ini-ish key=value parsing
		auto pos = line.find('=');
		if (pos != std::string::npos)
		{
			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);
			
			if (key == "etag")
				metadata->etag = value;
			else if (key == "last_modified")
				metadata->last_modified = value;
			else if (key == "content_length")
				metadata->content_length = std::stol(value);
		}
	}
	
	return true;
}

// performs a blocking HEAD request to get remote file metadata
bool getRemoteFileMetadata(const std::string& remote_path, download_metadata_t* metadata)
{
#ifndef NETWORK_MOCK
	if (!curl || !metadata)
		return false;
	
	setPlatformCurlFlags(curl);
	
	curl_easy_setopt(curl, CURLOPT_URL, remote_path.c_str());
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, metadata);
	
	CURLcode res = curl_easy_perform(curl);
	
	// reset curl to clean state for next time
	resetCurlToCleanState(curl);
	
	if (res == CURLE_OK) {
		// the target file size
		curl_off_t content_length = 0;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
		metadata->content_length = content_length;
		return true;
	}
#endif
	return false;
}

#ifndef NETWORK_MOCK
// ---------------------------------------------------------------------------
// segmented download: several HTTP Range requests in parallel (curl_multi)
//
// A single TCP connection is limited to (receive window / RTT), which is very
// low on the consoles. Using a few connections at once multiplies that limit.
//
// Chunks are written to disk strictly in order (append only), so the file on
// disk is always a valid prefix of the remote file. That keeps resume working.
// ---------------------------------------------------------------------------
#define SEG_MIN_SIZE      (8LL * 1024 * 1024) // smaller files use the regular path
#define SEG_CHUNK_SIZE    (4LL * 1024 * 1024) // bytes requested per Range request
#define SEG_WINDOW_CHUNKS 8                   // max chunks in memory at once
#define SEG_MAX_RETRIES   3

struct SegProbe
{
	std::string etag;
	bool accept_ranges = false;
};

struct SegChunk
{
	std::string data;
	size_t expected = 0;
	bool done = false;
	int retries = 0;
};

static size_t SegProbeHeaderCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	auto* probe = (SegProbe*)userp;
	size_t realsize = size * nmemb;
	std::string line = toLower(std::string((char*)contents, realsize));

	if (line.compare(0, 5, "http/") == 0)
	{
		// new response (e.g. after a redirect): forget previous headers
		probe->etag.clear();
		probe->accept_ranges = false;
	}
	else if (line.compare(0, 5, "etag:") == 0)
	{
		// keep original case, the value is compared byte by byte later
		std::string raw((char*)contents + 5, realsize - 5);
		size_t b = raw.find_first_not_of(" \t");
		size_t e = raw.find_last_not_of(" \t\r\n");
		if (b != std::string::npos && e != std::string::npos && e >= b)
			probe->etag = raw.substr(b, e - b + 1);
	}
	else if (line.compare(0, 14, "accept-ranges:") == 0)
	{
		probe->accept_ranges = line.find("bytes") != std::string::npos;
	}
	return realsize;
}

static size_t SegWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	auto* chunk = (SegChunk*)userp;
	size_t realsize = size * nmemb;
	// a server that ignores Range would send the whole file: refuse to buffer it
	if (chunk->data.size() + realsize > chunk->expected)
		return 0;
	chunk->data.append((char*)contents, realsize);
	return realsize;
}

// number of parallel connections for big downloads (1 disables segmented downloads)
#if defined(_3DS) || defined(WII) || defined(WIIU)
int libget_download_connections = 1; // not enough RAM to spare
#else
int libget_download_connections = 4;
#endif

// Returns true if the whole file was downloaded.
// *handled == false means nothing was done and the caller should use the regular single connection path.
// *resume_from is updated if we decided to restart from scratch.
static bool downloadFileSegmented(const std::string& remote_path, const std::string& local_path, bool resume,
								  long* resume_from, const download_metadata_t& old_metadata,
								  download_metadata_t* metadata, bool* handled)
{
	*handled = false;

	const int connections = libget_download_connections;
	if (connections <= 1 || !curl)
		return false;

	// 1. ask the server about the file (size, ETag, Range support)
	SegProbe probe;
	curl_off_t total = 0;
	long code = 0;

	setPlatformCurlFlags(curl);
	curl_easy_setopt(curl, CURLOPT_URL, remote_path.c_str());
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, SegProbeHeaderCallback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &probe);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	CURLcode pres = curl_easy_perform(curl);
	if (pres == CURLE_OK)
	{
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &total);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	}
	resetCurlToCleanState(curl);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

	if (pres != CURLE_OK || code != 200 || !probe.accept_ranges || total < SEG_MIN_SIZE)
	{
		printf("--> Segmented download not used (curl=%d http=%ld ranges=%d size=%lld)\n", (int)pres, code,
			   (int)probe.accept_ranges, (long long)total);
		return false;
	}

	// 2. decide where to start
	curl_off_t start = *resume_from;
	if (start > 0)
	{
		bool etag_changed = !old_metadata.etag.empty() && !probe.etag.empty() && old_metadata.etag != probe.etag;
		if (etag_changed || start >= total)
		{
			printf("--> Remote file changed, restarting download from the beginning\n");
			start = 0;
			*resume_from = 0;
		}
	}

	FILE* out = fopen(local_path.c_str(), (resume && start > 0) ? "ab" : "wb");
	if (!out)
		return false;

	*handled = true;
	if (metadata)
	{
		metadata->etag = probe.etag;
		metadata->content_length = (long)total;
	}

	printf("--> Segmented download: %lld bytes, %d connections, starting at %lld\n", (long long)total, connections,
		   (long long)start);

	// 3. set up chunks and workers
	const curl_off_t remaining = total - start;
	const long nchunks = (long)((remaining + SEG_CHUNK_SIZE - 1) / SEG_CHUNK_SIZE);
	std::vector<SegChunk> chunks(nchunks);
	for (long i = 0; i < nchunks; i++)
	{
		curl_off_t b = start + (curl_off_t)i * SEG_CHUNK_SIZE;
		curl_off_t e = std::min<curl_off_t>(b + SEG_CHUNK_SIZE, total);
		chunks[i].expected = (size_t)(e - b);
	}

	struct Worker
	{
		CURL* easy = nullptr;
		long chunk = -1;
	};
	const int nworkers = (int)std::min<long>(connections, nchunks);
	std::vector<Worker> workers(nworkers);

	CURLM* multi = curl_multi_init();
	curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, (long)nworkers);
#ifdef CURLPIPE_NOTHING
	// we want separate TCP connections, not one multiplexed connection
	curl_multi_setopt(multi, CURLMOPT_PIPELINING, (long)CURLPIPE_NOTHING);
#endif

	// only continue if the file did not change between requests
	struct curl_slist* headers = nullptr;
	if (!probe.etag.empty() && probe.etag.compare(0, 2, "W/") != 0)
	{
		std::string if_range = "If-Range: " + probe.etag;
		headers = curl_slist_append(headers, if_range.c_str());
	}

	for (auto& w : workers)
	{
		w.easy = curl_easy_init();
		setPlatformCurlFlags(w.easy);
		curl_easy_setopt(w.easy, CURLOPT_URL, remote_path.c_str());
		curl_easy_setopt(w.easy, CURLOPT_WRITEFUNCTION, SegWriteCallback);
		curl_easy_setopt(w.easy, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(w.easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
		curl_easy_setopt(w.easy, CURLOPT_CONNECTTIMEOUT, 20L);
		// give up on a connection that is basically dead (<1 KB/s for 30 s)
		curl_easy_setopt(w.easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
		curl_easy_setopt(w.easy, CURLOPT_LOW_SPEED_TIME, 30L);
		if (headers)
			curl_easy_setopt(w.easy, CURLOPT_HTTPHEADER, headers);
	}

	long nextAssign = 0; // next never-requested chunk
	long nextWrite = 0;  // next chunk to append to the file
	std::deque<long> retryQueue;
	curl_off_t written = start;
	bool failed = false, aborted = false;

	auto startChunk = [&](Worker& w, long idx)
	{
		curl_off_t b = start + (curl_off_t)idx * SEG_CHUNK_SIZE;
		curl_off_t e = b + (curl_off_t)chunks[idx].expected - 1;
		char range[64];
		snprintf(range, sizeof(range), "%lld-%lld", (long long)b, (long long)e);
		chunks[idx].data.clear();
		chunks[idx].data.reserve(chunks[idx].expected);
		curl_easy_setopt(w.easy, CURLOPT_RANGE, range);
		curl_easy_setopt(w.easy, CURLOPT_WRITEDATA, &chunks[idx]);
		curl_multi_add_handle(multi, w.easy);
		w.chunk = idx;
	};

	// hands out work to idle workers, returns true if anything was started
	auto assignWork = [&]() -> bool
	{
		bool any = false;
		for (auto& w : workers)
		{
			if (w.chunk >= 0)
				continue;
			if (!retryQueue.empty())
			{
				long idx = retryQueue.front();
				retryQueue.pop_front();
				startChunk(w, idx);
				any = true;
			}
			else if (nextAssign < nchunks && nextAssign < nextWrite + SEG_WINDOW_CHUNKS)
			{
				startChunk(w, nextAssign++);
				any = true;
			}
		}
		return any;
	};

	assignWork();

	while (!failed && !aborted && nextWrite < nchunks)
	{
		int running = 0;
		curl_multi_perform(multi, &running);

		// finished transfers
		int msgs_left = 0;
		while (CURLMsg* msg = curl_multi_info_read(multi, &msgs_left))
		{
			if (msg->msg != CURLMSG_DONE)
				continue;
			Worker* w = nullptr;
			for (auto& cand : workers)
				if (cand.easy == msg->easy_handle)
					w = &cand;
			if (!w || w->chunk < 0)
				continue;

			long http = 0;
			curl_easy_getinfo(w->easy, CURLINFO_RESPONSE_CODE, &http);
			curl_multi_remove_handle(multi, w->easy);

			SegChunk& c = chunks[w->chunk];
			bool ok = msg->data.result == CURLE_OK && http == 206 && c.data.size() == c.expected;
			if (ok)
			{
				c.done = true;
			}
			else
			{
				printf("--> chunk %ld failed (curl=%d http=%ld got=%zu/%zu), retry %d\n", w->chunk,
					   (int)msg->data.result, http, c.data.size(), c.expected, c.retries + 1);
				gLastCurlErrorMsg = curl_easy_strerror(msg->data.result);
				if (++c.retries > SEG_MAX_RETRIES)
					failed = true;
				else
					retryQueue.push_back(w->chunk);
			}
			w->chunk = -1;
		}

		// append finished chunks to the file, in order
		while (!failed && nextWrite < nchunks && chunks[nextWrite].done)
		{
			SegChunk& c = chunks[nextWrite];
			if (fwrite(c.data.data(), 1, c.data.size(), out) != c.data.size())
			{
				gLastCurlErrorMsg = "write error (disk full?)";
				failed = true;
				break;
			}
			written += (curl_off_t)c.data.size();
			std::string().swap(c.data); // free the memory
			nextWrite++;
		}

		// progress (also lets the UI cancel the download)
		if (networking_callback != nullptr)
		{
			curl_off_t have = written;
			for (long i = nextWrite; i < nextAssign; i++)
				have += (curl_off_t)chunks[i].data.size();
			double progress = (double)have / (double)total;
			if (progress > 1.0)
				progress = 1.0;
			if (networking_callback(networking_callback_data, progress) != 0)
				aborted = true;
		}

		if (failed || aborted)
			break;

		if (assignWork())
			continue; // new transfers need a perform() right away

		int numfds = 0;
		curl_multi_wait(multi, nullptr, 0, 100, &numfds);
		if (numfds == 0)
			usleep(2000); // avoid spinning if there is nothing to wait on
	}

	// 4. cleanup
	for (auto& w : workers)
	{
		if (w.chunk >= 0)
			curl_multi_remove_handle(multi, w.easy);
		curl_easy_cleanup(w.easy);
	}
	curl_multi_cleanup(multi);
	if (headers)
		curl_slist_free_all(headers);
	fclose(out); // flushes: what is on disk is exactly [0, written)

	if (failed || aborted)
	{
		printf("--> Segmented download %s at byte %lld\n", aborted ? "cancelled" : "failed", (long long)written);
		// nothing received at all: maybe the server lies about Range support, let the regular path try
		if (failed && written == start)
			*handled = false;
		return false;
	}

	return true;
}
#endif

// downloads happen metadata being observed, but not necessarily saved
bool downloadFileToDiskWithMetadata(const std::string& remote_path, const std::string& local_path, bool resume, download_metadata_t* metadata)
{
	long resume_from = 0;
	download_metadata_t old_metadata = {};
	
	if (resume)
	{
		struct stat file_info = {};
		if (stat(local_path.c_str(), &file_info) == 0)
		{
			resume_from = file_info.st_size;
			
			// validate existing metadat first
			if (loadDownloadMetadata(local_path, &old_metadata))
			{
				printf("--> Found existing download metadata (ETag: %s)\n", old_metadata.etag.c_str());
			} else {
				// no metadata found, cannot resume (old clients might have partials without metadata)
				resume_from = 0;
			}
			
			printf("--> Resuming download from byte %ld\n", resume_from);
		}
	}
	
#ifndef NETWORK_MOCK
	// big files: try several parallel Range requests first
	{
		bool handled = false;
		bool seg_ok = downloadFileSegmented(remote_path, local_path, resume, &resume_from, old_metadata, metadata, &handled);
		if (handled)
			return seg_ok;
	}
#endif

	// open our tmp data in append mode if resuming, otherwise write mode
	FILE* out_file = fopen(local_path.c_str(), resume && resume_from > 0 ? "ab" : "wb");
	if (!out_file)
		return false;

	// automatically close the file if it leaves the scope.
	auto file = std::unique_ptr<FILE, int (*)(FILE*)>(out_file, &::fclose);

	auto buf = std::make_unique<uint8_t[]>(BUF_SIZE); // 8MB.
	if (buf == nullptr)
	{
		return false;
	}

	ntwrk_struct_t data_struct = { buf.get(), BUF_SIZE, 0, out_file };

#ifndef NETWORK_MOCK
	if (metadata) {
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, metadata);
	}
	
	struct curl_slist* headers = nullptr;
	if (resume && resume_from > 0)
	{
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resume_from);
		
		// If-Range header with etag if we have it
		if (!old_metadata.etag.empty())
		{
			std::string if_range = "If-Range: " + old_metadata.etag;
			headers = curl_slist_append(headers, if_range.c_str());
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		}
	}
	else
	{
		// Don't use resuming if it's not enabled
		curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
	}
#endif

	bool success = downloadFileCommon(remote_path, nullptr, &data_struct);

	// write remaining data to file before free.
	fwrite(data_struct.data, data_struct.offset, 1, data_struct.out);
	
#ifndef NETWORK_MOCK
	// store content-length
	if (success && metadata)
	{
		curl_off_t content_length = 0;
		curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
		if (content_length > 0) {
			metadata->content_length = content_length + resume_from; // Total expected size
		}
	}
	
	if (headers) {
		curl_slist_free_all(headers);
		headers = nullptr;
	}
	
	// reset curl to clean state
	resetCurlToCleanState(curl);
#endif
	
	// saveDownloadMetadata() must be explicitly called to save the file
	
	return success;
}

const char* plural(int amount)
{
	return (amount == 1) ? "" : "s";
}

std::string dir_name(const std::string& file_path)
{
	// turns "/hi/man/thing.txt to /hi/man"
	size_t pos = file_path.find_last_of('/');

	// no "/" in string, return empty string
	if (pos == std::string::npos)
		return "";

	return file_path.substr(0, pos);
}

// sorting function: put bigger strings at the front
bool compareLen(const std::string& a, const std::string& b)
{
	return (a.size() > b.size());
}

int init_networking()
{
#if defined(SWITCH)
	{
		// bigger TCP buffers: the defaults (64KB rx) cap a single connection to a few MB/s
		SocketInitConfig cfg = *socketGetDefaultInitConfig();
		cfg.tcp_tx_buf_size     = 0x10000;
		cfg.tcp_rx_buf_size     = 0x100000; // 1MB
		cfg.tcp_tx_buf_max_size = 0x40000;
		cfg.tcp_rx_buf_max_size = 0x100000; // 1MB
		cfg.sb_efficiency       = 4;
		if (R_FAILED(socketInitialize(&cfg)))
		{
			// not enough memory (e.g. applet mode)? fall back to the stock config
			socketInitializeDefault();
		}
	}
#endif
#if defined(_3DS)
	SOCUBuffer = (u32*)memalign(SOCU_ALIGN, SOCU_BUFFERSIZE);
	socInit(SOCUBuffer, SOCU_BUFFERSIZE);
#endif

#if defined(WII) && !defined(NETWORK_MOCK)
	// TODO: network initialization on the wii is *extremly* slow (~10s)
	// It's probably a good idea to use wiisocket_init_async and
	// show something on the screen during that interval
	wiisocket_init();
#endif

#ifndef NETWORK_MOCK
	curl_global_init(CURL_GLOBAL_ALL);

	// init our curl handle
	curl = curl_easy_init();

#endif
	return 1;
}

int deinit_networking()
{
#ifndef NETWORK_MOCK
	curl_easy_cleanup(curl);
	curl_global_cleanup();
#endif

#if defined(WII) && !defined(NETWORK_MOCK)
	wiisocket_deinit();
#endif

#if defined(SWITCH)
	socketExit();
#endif

	return 1;
}

void cp(const char* from, const char* to)
{
	std::ifstream src(from, std::ios::binary);
	std::ofstream dst(to, std::ios::binary);

	dst << src.rdbuf();
}

int remove_empty_dirs(const char* name, int count)
{
	// from incoming path, recursively ensure all directories are deleted
	// return the number of files remaining (0 if totally erased and successful)

	// based on https://stackoverflow.com/a/8438663

	int starting_count = count;

	DIR* dir;
	struct dirent* entry;

	// already deleted
	if (!(dir = opendir(name)))
		return true;

	auto dirclose_handler = std::unique_ptr<DIR, int (*)(DIR*)>(dir, &::closedir);

	// go through files in directory
	while ((entry = readdir(dir)) != nullptr)
	{
		if (is_dir(name, entry))
		{
			char path[1024];
			// skip current dir or parent dir
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
				continue;

			// update path so far
			snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);

			// recursively go into this directory too
			count += remove_empty_dirs(path, count);
		}
		else
		{
			// file found, increase file count
			count++;
		}
	}

	// now that we've been through this directory, check if it was an empty directory
	if (count == starting_count)
	{
		// empty, try rmdir (should only remove if empty anyway)
		rmdir(name);
	}

	// return number of files at this level (total count minus starting)
	return count - starting_count;
}

bool libget_reset_data(const char* path)
{
	time_t seconds;
	time(&seconds);

	auto current_time = static_cast<long int>(seconds);

	// move the contents of the .get folder to .trash/get_backup_date
	std::stringstream ss;
	ss << ".trash/get_backup_" << current_time;
	printf("--> Info: %ld\n", current_time);

	printf("--> Renaming %s to %s\n", path, ss.str().c_str());

	std::stringstream ss2;
	ss2 << path << "../.trash";
	mkpath(ss2.str());

	int res = std::rename(path, ss.str().c_str());
	if (res == 0)
		printf("Folder renamed!\n");
	else
		printf("Issue renaming folder... %d: %s\n", errno, strerror(errno));

	return !res;
}

bool is_dir(std::string_view path __attribute__((unused)), struct dirent* entry)
{
#ifndef WIN32
	return entry->d_type & DT_DIR;
#else
	// windows check, using stat
	struct stat s;
	// get full path using dir and entry
	std::string full_path = std::string(path) + "/" + std::string(entry->d_name);
	stat(full_path.c_str(), &s);
	return s.st_mode & S_IFDIR;
#endif
}

std::string getHumanReadableBytes(uint64_t bytes)
{
	const char* suffixes[] = { "bytes", "KB", "MB", "GB", "TB" };
	int suffix = 0;
	double size = bytes;

	while (size >= 1024 && suffix < 4)
	{
		size /= 1024;
		suffix++;
	}

	std::stringstream ss;
	// limit to up to 2 decimal places
	int places = 0;
    double fractional_part = size - static_cast<int>(size);
    if (fractional_part > 0) {
		// there's a decimal component, set to either 1 or 2
        places = (fractional_part * 100 >= 1) ? 2 : 1;
    }
	ss.precision(places);
	ss << std::fixed << size << " " << suffixes[suffix];
	return ss.str();
}