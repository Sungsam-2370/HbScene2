// Modified from Zarklord1 : https://github.com/Zarklord1/WiiU_Save_Manager (src/file_utils/Zip.cpp)
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <memory>
#include <optional>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "Utils.hpp"
#include "ZipUtil.hpp"
#include "Cancel.hpp"

#include <atomic>
#include <condition_variable>
#include <errno.h>
#include <mutex>
#include <thread>
#include <sys/stat.h>

// 0 = sin probar, 1 = O_EXCL funciona en este sistema de archivos, 2 = no
namespace {
int gExclMode = 0;

// contadores de escritura real a la SD (los de lectura estan en ZipBufferedIO.hpp)
std::atomic<uint64_t> gWriteNs{ 0 };
std::atomic<uint64_t> gWriteBytes{ 0 };

// Ajustes de extraccion, para poder probar distintas combinaciones en la consola SIN recompilar.
// Archivo opcional ".get/extraccion.cfg", una linea por ajuste:
//   hilo=1          (1 = hilo de escritura en archivos grandes, 0 = escribir en el mismo hilo)
//   bloque_kb=2048  (tamaño de bloque al descomprimir/escribir archivos grandes, 128..16384)
//   lectura_kb=2048 (tamaño de las lecturas del zip, 64..16384)
struct ExtractCfg
{
	bool async = true;
	uint32_t blockKb = 2048;
	uint32_t readKb = 2048;
	bool loaded = false;
};
ExtractCfg gCfg;

void loadExtractCfg()
{
	if (gCfg.loaded)
		return;
	gCfg.loaded = true;
	FILE* f = fopen("./.get/extraccion.cfg", "r");
	if (!f)
		return;
	char line[128];
	while (fgets(line, sizeof(line), f))
	{
		char* eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		long v = strtol(eq + 1, nullptr, 10);
		std::string k(line);
		if (k == "hilo")
			gCfg.async = (v != 0);
		else if (k == "bloque_kb" && v >= 128 && v <= 16384)
			gCfg.blockKb = (uint32_t)v;
		else if (k == "lectura_kb" && v >= 64 && v <= 16384)
			gCfg.readKb = (uint32_t)v;
	}
	fclose(f);
}
} // namespace

#if !defined(WIN32)
#include "ZipBufferedIO.hpp"
#endif

#if defined(SWITCH)
#include <switch.h>
#endif

#define u32 uint32_t
#define u8 uint8_t

#ifdef WIN32
#define aligned_alloc _aligned_malloc
#define cross_free _aligned_free
#define fsync _commit
#define memalign _aligned_malloc
#else
#define cross_free free
#endif

// on macos, wrap posix_memalign
// https://stackoverflow.com/a/45182325/4953343
#ifdef __APPLE__
void *memalign(size_t blocksize, size_t bytes)
{
  void *m;
  errno = posix_memalign(&m, blocksize, bytes);
  return errno ? NULL : m;
}
#else
#include <malloc.h>
#endif

// void info(const char* format, ...);

Zip::Zip(const std::string& zipPath)
{
	fileToZip = zipOpen(zipPath.c_str(), APPEND_STATUS_CREATE);
	if (fileToZip == nullptr) printf("--> Error Opening: %s for zipping files!\n", zipPath.c_str());
}

Zip::~Zip()
{
	Close();
}

int Zip::AddFile(const std::string& internalPath, const std::string& path)
{
	zipOpenNewFileInZip(fileToZip, internalPath.c_str(), nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
	// info("Adding %s to zip, under path %s\n", path, internalPath);
	int code = Add(path);
	zipCloseFileInZip(fileToZip);
	return code;
}

int Zip::AddDir(const std::string& internalDir, const std::string& externalDir)
{
	DIR* dir = opendir(externalDir.c_str());
	if (dir == nullptr)
	{
		return -1;
	}

	struct dirent* dirent;
	while ((dirent = readdir(dir)) != nullptr)
	{
		if (strcmp(dirent->d_name, "..") == 0 || strcmp(dirent->d_name, ".") == 0)
			continue;

		std::string zipPath(internalDir);
		zipPath += '/';
		zipPath += dirent->d_name;
		std::string realPath(externalDir);
		realPath += '/';
		realPath += dirent->d_name;

		if (is_dir(externalDir, dirent))
		{
			AddDir(zipPath, realPath);
		}
		else
		{
			AddFile(zipPath, realPath);
		}
	}
	closedir(dir);
	return 0;
}

int Zip::Add(const std::string& path)
{
	int fileNumber = open(path.c_str(), O_RDONLY);
	if (fileNumber == -1)
		return -1;

	u32 filesize = lseek(fileNumber, 0, SEEK_END);
	lseek(fileNumber, 0, SEEK_SET);

	u32 blocksize = 0x80000;
	u8* buffer = (u8*)memalign(0x40, blocksize);
	if (buffer == nullptr)
	{
		close(fileNumber);
		return -2;
	}

	u32 done = 0;
	int readBytes;

	while (done < filesize)
	{
		if (done + blocksize > filesize)
		{
			blocksize = filesize - done;
		}
		readBytes = read(fileNumber, buffer, blocksize);
		if (readBytes <= 0)
			break;
		zipWriteInFileInZip(fileToZip, buffer, blocksize);
		done += readBytes;
	}
	close(fileNumber);
	cross_free(buffer);

	if (done != filesize)
	{
		return -3;
	}
	return 0;
}

void Zip::Close()
{
	zipClose(fileToZip, nullptr);
}

UnZip::UnZip(const std::string& zipPath)
{
	printf("Unzip Path: %s\n", zipPath.c_str());
	zipName = zipPath.substr(zipPath.find_last_of("/\\") == std::string::npos ? 0 : zipPath.find_last_of("/\\") + 1);
	loadExtractCfg();
#if !defined(WIN32)
	zipio::gBlock = (uint64_t)gCfg.readKb * 1024;
	snapReadNs = zipio::gReadNs.load();
	snapReadBytes = zipio::gReadBytes.load();
	snapWriteNs = gWriteNs.load();
	snapWriteBytes = gWriteBytes.load();
#endif
#if !defined(WIN32)
	// lectura con cache de dos ventanas (ver ZipBufferedIO.hpp); si por algo falla, la normal
	fileToUnzip = zipio::openZip(zipPath);
	if (fileToUnzip == nullptr)
#endif
		fileToUnzip = unzOpen(zipPath.c_str());
#if defined(SWITCH)
	// inflating a big zip is CPU heavy: ask for the faster clock while we have it open
	if (R_SUCCEEDED(appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad)))
		cpuBoosted = true;
#endif
}

UnZip::~UnZip()
{
	Close();
}

void UnZip::Close()
{
	if (fileToUnzip != nullptr)
	{
		unzClose(fileToUnzip);
		fileToUnzip = nullptr;

		if (statFiles > 0)
		{
			double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - openedAt).count();
			printf("--> Extraction stats: %llu files, %llu MB, %.1f s inside Extract(), %.1f s total with the zip open\n",
				   (unsigned long long)statFiles, (unsigned long long)(statBytes >> 20), statExtractMs / 1000.0,
				   totalMs / 1000.0);
			printf("--> Extraction breakdown (s): open/create %.2f | read+inflate %.2f | write %.2f | close %.2f | async files %llu\n",
				   statOpenMs / 1000.0, statInflateMs / 1000.0, statWriteMs / 1000.0, statCloseMs / 1000.0,
				   (unsigned long long)statAsyncFiles);

			// versiones anteriores guardaban un log de texto aqui: se borra una sola vez
			// (ya no se escribe nada en la SD, el desglose queda solo en la consola)
			static bool oldLogRemoved = false;
			if (!oldLogRemoved)
			{
				oldLogRemoved = true;
				remove("./.get/extraccion_log.txt");
			}
		}
	}
#if defined(SWITCH)
	if (cpuBoosted)
	{
		appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
		cpuBoosted = false;
	}
#endif
}

bool UnZip::IsValid()
{
	// check if the zip file is actually a zip file
	if (fileToUnzip == nullptr) {
        printf("Error: fileToUnzip is null\n");
        return false;
    }

	unz_global_info globalInfo;
	int result = unzGetGlobalInfo(fileToUnzip, &globalInfo);
	if (result != UNZ_OK) {
		printf("Failed to get global info from the ZIP file.\n");
		// Handle the error appropriately
		return false;
	}

    return result == UNZ_OK;
}

int UnZip::ExtractFile(const std::string& internalPath, const std::string& path)
{
	int code = unzLocateFile(fileToUnzip, internalPath.c_str(), 1);
	if (code == UNZ_END_OF_LIST_OF_FILE)
	{
		return -1;
	}

	unz_file_info_s fileInfo = GetFileInfo();
	// info("Extracting file %s to: %s\n", internalPath.c_str(), path.c_str());
	return Extract(path, fileInfo);
}

int UnZip::ExtractDir(const std::string& internalDir, const std::string& externalDir)
{
	int i = 0;
	for (;;)
	{
		int code;
		if (i == 0)
		{
			code = unzGoToFirstFile(fileToUnzip);
			i++;
		}
		else
		{
			code = unzGoToNextFile(fileToUnzip);
		}
		if (code == UNZ_END_OF_LIST_OF_FILE)
		{
			if (i > 1)
			{
				return 0;
			}
			else
			{
				return -1;
			}
		}
		unz_file_info_s fileInfo = GetFileInfo();

		std::string outputPath = GetFullFileName(fileInfo);
		if (outputPath.find(internalDir, 0) != 0)
		{
			continue;
		}

		outputPath.replace(0, internalDir.length(), externalDir);
		if (fileInfo.uncompressed_size != 0 && fileInfo.compression_method != 0)
		{
			// file
			i++;
			// info("Extracting %s to: %s\n", GetFullFileName(fileInfo).c_str(), outputPath.c_str());
			Extract(outputPath, fileInfo);
		}
	}
}

int UnZip::ExtractAll(const std::string& dirToExtract)
{
	int i = 0;
	for (;;)
	{
		int code;
		if (i == 0)
		{
			code = unzGoToFirstFile(fileToUnzip);
			i++;
		}
		else
		{
			code = unzGoToNextFile(fileToUnzip);
		}
		if (code == UNZ_END_OF_LIST_OF_FILE) return 0;

		unz_file_info_s fileInfo = GetFileInfo();
		std::string fileName(dirToExtract);
		fileName += '/';
		fileName += GetFullFileName(fileInfo);
		if (fileInfo.uncompressed_size != 0)
		{
			// file
			//  info("Extracting %s to: %s\n", GetFullFileName(fileInfo).c_str(), fileName.c_str());
			Extract(fileName, fileInfo);
		}
	}
}

// ---------------------------------------------------------------------------
// ExtractAllWithProgress
// Igual que ExtractAll pero llama a networking_callback despues de extraer
// cada archivo, con un progreso de 0.0 a 1.0 basado en bytes descomprimidos.
//
// Paso 1 (rapido): recorre el directorio central del ZIP para sumar
//                  uncompressed_size de todos los archivos.
// Paso 2:          extrae archivo por archivo, acumulando bytes y reportando
//                  progreso al callback en cada archivo.
//
// Si networking_callback es nullptr, se comporta identico a ExtractAll.
// ---------------------------------------------------------------------------
int UnZip::ExtractAllWithProgress(const std::string& dirToExtract)
{
	// Paso 1: sumar el total de bytes descomprimidos del ZIP.
	// unzGoToNextFile solo avanza el puntero en el directorio central
	// (ya cargado en memoria por unzOpen), no lee datos comprimidos.
	// En un ZIP de 1000 archivos esto tarda menos de 1ms.
	uint64_t totalBytes = 0;
	{
		int code = unzGoToFirstFile(fileToUnzip);
		while (code == UNZ_OK)
		{
			unz_file_info_s fi = GetFileInfo();
			if (fi.uncompressed_size != 0)
				totalBytes += fi.uncompressed_size;
			code = unzGoToNextFile(fileToUnzip);
		}
	}

	// Paso 2: extraer con progreso.
	uint64_t extractedBytes = 0;
	int i = 0;
	for (;;)
	{
		int code;
		if (i == 0)
		{
			code = unzGoToFirstFile(fileToUnzip);
			i++;
		}
		else
		{
			code = unzGoToNextFile(fileToUnzip);
		}
		if (code == UNZ_END_OF_LIST_OF_FILE) return 0;

		if (libget_cancelled()) return -9;

		unz_file_info_s fileInfo = GetFileInfo();
		std::string fileName(dirToExtract);
		fileName += '/';
		fileName += GetFullFileName(fileInfo);

		if (!fileName.empty() && fileName.back() != '/')
		{
			if (Extract(fileName, fileInfo) == -9) return -9;

			extractedBytes += fileInfo.uncompressed_size;

			// Reportar progreso si hay callback activo.
			// totalBytes > 0 siempre aqui (al menos un archivo con datos).
			if (networking_callback != nullptr && totalBytes > 0)
			{
				double progress = (double)extractedBytes / (double)totalBytes;
				// Clamp a [0.0, 1.0] por seguridad ante cualquier inconsistencia.
				if (progress > 1.0) progress = 1.0;
				networking_callback(nullptr, progress);
			}
		}
	}
}

std::vector<std::string> UnZip::PathDump()
{
	int i = 0;
	std::vector<std::string> paths;
	for (;;)
	{
		int code;
		if (i == 0)
		{
			code = unzGoToFirstFile(fileToUnzip);
			i++;
		}
		else
		{
			code = unzGoToNextFile(fileToUnzip);
		}
		if (code == UNZ_END_OF_LIST_OF_FILE)
		{
			break;
		}

		unz_file_info_s fileInfo = GetFileInfo();
		std::string fileName = GetFullFileName(fileInfo);
		// solo se omiten las carpetas (terminan en '/'). Antes tambien se descartaban los archivos
		// guardados SIN compresion (metodo 0) y los vacios, asi que en un zip sin manifest.install
		// no se extraian (ej. .nro/.nsp "stored" o los archivos .flag vacios).
		if (!fileName.empty() && fileName.back() != '/')
		{
			// printf("PathDump: %s\n", fileName.c_str());
			paths.push_back(fileName);
		}
	}
	return paths;
}

std::unordered_map<std::string, unz_file_pos> UnZip::GetPathToFilePosMapping()
{
	int i = 0;
	std::unordered_map<std::string, unz_file_pos> paths;
	for (;;)
	{
		int code;
		if (i == 0)
		{
			code = unzGoToFirstFile(fileToUnzip);
			i++;
		}
		else
		{
			code = unzGoToNextFile(fileToUnzip);
		}
		if (code == UNZ_END_OF_LIST_OF_FILE)
		{
			break;
		}

		unz_file_info_s fileInfo = GetFileInfo();
		std::string fileName = GetFullFileName(fileInfo);

		unz_file_pos pos = {};
		unzGetFilePos(fileToUnzip, &pos);

		// save to map
		paths[fileName] = pos;
	}
	return paths;
}

int UnZip::Extract(const std::string& path, unz_file_pos& file_pos)
{
	// we have a file pos, seek to that file
	unzGoToFilePos(fileToUnzip, &file_pos);
	auto file_info = GetFileInfo();
	return Extract(path, file_info);
}

int UnZip::Extract(const std::string& path, const unz_file_info_s& fileInfo)
{
	auto t0 = std::chrono::steady_clock::now();
	int result = ExtractImpl(path, fileInfo);
	statExtractMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	statFiles++;
	statBytes += fileInfo.uncompressed_size;
	return result;
}

// Escritor en segundo plano para archivos grandes: mientras el hilo principal
// descomprime el siguiente bloque, este hilo escribe el anterior a la SD. La
// escritura es una llamada bloqueante al servicio de archivos, asi que la CPU
// queda libre para descomprimir en paralelo.
namespace {
class AsyncFileWriter
{
public:
	explicit AsyncFileWriter(int fd) : fd_(fd), th_([this] { loop(); }) {}
	~AsyncFileWriter() { finish(); }

	// encola un bloque (espera a que termine la escritura anterior). false si ya hubo un error
	bool submit(const uint8_t* p, size_t n)
	{
		std::unique_lock<std::mutex> lk(m_);
		idle_.wait(lk, [this] { return !busy_; });
		if (err_)
			return false;
		p_ = p;
		n_ = n;
		busy_ = true;
		lk.unlock();
		work_.notify_one();
		return true;
	}

	// espera la ultima escritura y cierra el hilo; false si alguna fallo
	bool finish()
	{
		{
			std::unique_lock<std::mutex> lk(m_);
			idle_.wait(lk, [this] { return !busy_; });
			stop_ = true;
		}
		work_.notify_one();
		if (th_.joinable())
			th_.join();
		return !err_;
	}

private:
	void loop()
	{
		for (;;)
		{
			std::unique_lock<std::mutex> lk(m_);
			work_.wait(lk, [this] { return busy_ || stop_; });
			if (!busy_ && stop_)
				return;
			const uint8_t* p = p_;
			size_t n = n_;
			lk.unlock();

			size_t off = 0;
			bool bad = false;
			const auto tw0 = std::chrono::steady_clock::now();
			while (off < n)
			{
				ssize_t w = write(fd_, p + off, n - off);
				if (w <= 0)
				{
					bad = true;
					break;
				}
				off += (size_t)w;
			}
			gWriteNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - tw0).count();
			gWriteBytes += off;

			lk.lock();
			if (bad)
				err_ = true;
			busy_ = false;
			lk.unlock();
			idle_.notify_all();
		}
	}

	int fd_;
	std::mutex m_;
	std::condition_variable work_, idle_;
	const uint8_t* p_ = nullptr;
	size_t n_ = 0;
	bool busy_ = false;
	bool stop_ = false;
	bool err_ = false;
	std::thread th_; // se declara al final: arranca cuando ya existe todo lo demas
};

constexpr uint32_t kAsyncMinSize = 1536 * 1024; // a partir de aqui vale la pena el hilo de escritura
} // namespace

// Crea (o trunca) el archivo destino. Para poder deshacer una cancelacion hace falta saber
// cuales archivos son NUEVOS; en vez de un stat() por archivo (una llamada mas al sistema
// de archivos por cada uno, miles en un zip grande) se usa O_EXCL: si el open() crea el
// archivo, es nuevo. Como no todos los sistemas de archivos respetan O_EXCL, la primera vez
// se prueba con un archivo temporal; si no funciona, se usa el stat() de siempre.
int UnZip::openForWrite(const std::string& path)
{
	if (gExclMode == 0)
	{
		const std::string probe = path + ".hbs_probe";
		int a = open(probe.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0777);
		int b = (a >= 0) ? open(probe.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0777) : -1;
		const bool ok = (a >= 0 && b < 0 && errno == EEXIST);
		if (a >= 0) close(a);
		if (b >= 0) close(b);
		if (a >= 0) unlink(probe.c_str());
		gExclMode = ok ? 1 : 2;
		printf("--> O_EXCL %s: se %s\n", ok ? "funciona" : "no es fiable", ok ? "evitan los stat() por archivo" : "usa stat() por archivo");
	}

	if (gExclMode == 1)
	{
		int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0777);
		if (fd >= 0)
		{
			g_libget_created.addFile(path); // lo creamos nosotros: es nuevo
			return fd;
		}
		if (errno == EEXIST)
			return open(path.c_str(), O_WRONLY | O_TRUNC, 0777); // ya existia: se sobreescribe
		return -1;
	}

	g_libget_created.before(path); // anota solo si no existia
	return open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0777);
}

int UnZip::ExtractImpl(const std::string& path, const unz_file_info_s& fileInfo)
{
	if (libget_cancelled())
		return -9;

	using clk = std::chrono::steady_clock;
	auto msBetween = [](clk::time_point a, clk::time_point b) {
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	const auto tOpen0 = clk::now();

	if (unzOpenCurrentFile(fileToUnzip) != UNZ_OK)
	{
		return -2;
	}

	size_t last_slash_idx = path.find_last_of("/\\");
	if (last_slash_idx != std::string::npos)
	{
		// only ask the filesystem once per folder (thousands of files share a few folders)
		std::string dir = path.substr(0, last_slash_idx);
		if (createdDirs.insert(dir).second)
		{
			g_libget_created.beforeDir(dir); // anota las carpetas que se van a crear (para cancelar)
			CreateSubfolder(dir);
		}
	}

	// small files don't need a big buffer (zips can have thousands of them)
	u32 blocksize = 0x80000;
	if (fileInfo.uncompressed_size >= 0x80000)
		blocksize = gCfg.blockKb * 1024; // archivos grandes: bloque configurable (por defecto 2048 KB)
	if (fileInfo.uncompressed_size < blocksize)
		blocksize = (fileInfo.uncompressed_size < 0x1000) ? 0x1000 : (u32)fileInfo.uncompressed_size;
	u8* buffer = (u8*)aligned_alloc(0x1000, (blocksize + 0xFFF) & ~0xFFFu);
	if (buffer == nullptr)
	{
		unzCloseCurrentFile(fileToUnzip);
		return -3;
	}
	// archivos grandes: segundo bloque para descomprimir mientras se escribe el anterior
	u8* buffer2 = nullptr;
	if (gCfg.async && fileInfo.uncompressed_size >= kAsyncMinSize)
		buffer2 = (u8*)aligned_alloc(0x1000, (blocksize + 0xFFF) & ~0xFFFu);

	int fd = openForWrite(path);
	if (fd == -1)
	{
		unzCloseCurrentFile(fileToUnzip);
		cross_free(buffer);
		if (buffer2) cross_free(buffer2);
		return -4;
	}
	statOpenMs += msBetween(tOpen0, clk::now());

	u32 done = 0;
	bool failed = false;

	if (buffer2 != nullptr)
	{
		// ---- archivo grande: lectura+descompresion en este hilo, escritura en otro ----
		statAsyncFiles++;
		AsyncFileWriter writer(fd);
		u8* bufs[2] = { buffer, buffer2 };
		int cur = 0;
		while (done < fileInfo.uncompressed_size && !failed)
		{
			u32 want = blocksize;
			if (done + want > fileInfo.uncompressed_size)
				want = fileInfo.uncompressed_size - done;

			libget_poll(); // el usuario puede cancelar (B 5 s) incluso a mitad de un archivo grande
			if (libget_cancelled())
			{
				failed = true;
				break;
			}

			auto t0 = clk::now();
			int readBytes = unzReadCurrentFile(fileToUnzip, bufs[cur], want);
			statInflateMs += msBetween(t0, clk::now());
			if (readBytes <= 0)
			{
				failed = true;
				break;
			}

			// submit espera la escritura anterior (bufs[cur^1]); solo ese tiempo
			// de espera cuenta como "write": el resto se solapo con la descompresion
			auto t1 = clk::now();
			bool queued = writer.submit(bufs[cur], (size_t)readBytes);
			statWriteMs += msBetween(t1, clk::now());
			if (!queued)
			{
				failed = true;
				break;
			}
			cur ^= 1;
			done += readBytes;
		}
		auto t2 = clk::now();
		if (!writer.finish())
			failed = true;
		statWriteMs += msBetween(t2, clk::now());
	}
	else
	{
		while (done < fileInfo.uncompressed_size && !failed)
		{
			u32 want = blocksize;
			if (done + want > fileInfo.uncompressed_size)
				want = fileInfo.uncompressed_size - done;

			// el usuario puede cancelar (B 5 s) incluso a mitad de un archivo grande
			libget_poll();
			if (libget_cancelled())
			{
				failed = true;
				break;
			}

			auto t0 = clk::now();
			int readBytes = unzReadCurrentFile(fileToUnzip, buffer, want);
			statInflateMs += msBetween(t0, clk::now());
			if (readBytes <= 0)
			{
				// read/inflate error or truncated zip: never write garbage and call it a success
				failed = true;
				break;
			}

			// write() may write less than requested
			auto t1 = clk::now();
			int off = 0;
			while (off < readBytes)
			{
				int writeBytes = write(fd, buffer + off, readBytes - off);
				if (writeBytes <= 0)
				{
					failed = true;
					break;
				}
				off += writeBytes;
			}
			{
				const auto t2 = clk::now();
				statWriteMs += msBetween(t1, t2);
				gWriteNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
				gWriteBytes += (uint64_t)off;
			}
			done += off;
		}
	}

	// no fsync() here: it forced a flush to the SD card for every single file,
	// which is very slow with many files. close() already hands the data to the filesystem.
	auto tClose0 = clk::now();
	close(fd);
	cross_free(buffer);
	if (buffer2) cross_free(buffer2);
	unzCloseCurrentFile(fileToUnzip);
	statCloseMs += msBetween(tClose0, clk::now());

	if (done != fileInfo.uncompressed_size)
	{
		return -4;
	}

	return 0;
}

std::string UnZip::GetFileName(const unz_file_info_s& fileInfo)
{
	auto filePath = GetFullFileName(fileInfo);
	const size_t last_slash_idx = filePath.find_last_of("/\\");
	if (last_slash_idx != std::string::npos)
	{
		return filePath.substr(last_slash_idx + 1);
	}
	return filePath;
}

std::string UnZip::GetFullFileName(const unz_file_info_s& fileInfo)
{
	std::string path;
	path.resize(fileInfo.size_filename);
	unzGetCurrentFileInfo(fileToUnzip, (unz_file_info_s*)&fileInfo, path.data(), path.size(), nullptr, 0, nullptr, 0);
	return path;
}

unz_file_info_s UnZip::GetFileInfo()
{
	unz_file_info_s fileInfo = {};
	unzGetCurrentFileInfo(fileToUnzip, &fileInfo, nullptr, 0, nullptr, 0, nullptr, 0);
	return fileInfo;
}
