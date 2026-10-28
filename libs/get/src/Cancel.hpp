#pragma once

// ---------------------------------------------------------------------------
// Cancelacion de descargas / extracciones / instalaciones.
//
// - g_libget_cancel: bandera global. La UI la activa (mantener B 5 segundos) y
//   las capas de abajo la revisan en sus bucles para parar cuanto antes.
// - g_libget_poll: gancho opcional que la UI instala para poder revisar el
//   boton B incluso durante operaciones largas que no reportan progreso
//   (por ejemplo extraer un archivo enorme de un zip).
// - g_libget_created: lista de archivos/carpetas que la operacion en curso
//   CREO (no existian antes). Si se cancela, se borran solo esos: lo que ya
//   estaba en la SD no se toca.
//
// Todo inline en este header (sin .cpp) para no tocar el Makefile.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

inline std::atomic<bool> g_libget_cancel{false};
inline void (*g_libget_poll)() = nullptr;

inline bool libget_cancelled() { return g_libget_cancel.load(); }
inline void libget_cancel_request() { g_libget_cancel.store(true); }
inline void libget_cancel_reset() { g_libget_cancel.store(false); }
inline void libget_poll() { if (g_libget_poll) g_libget_poll(); }

class LibgetCreatedTracker
{
public:
	void clear()
	{
		files_.clear();
		dirs_.clear();
	}

	// llamar ANTES de escribir `path`: si no existe todavia, se anota (junto con
	// las carpetas que falten en su camino)
	void before(const std::string& path)
	{
		struct stat st = {};
		if (stat(path.c_str(), &st) == 0)
			return;
		files_.push_back(path);

		std::string p = parent(path);
		while (!p.empty() && p.find('/') != std::string::npos)
		{
			if (stat(p.c_str(), &st) == 0)
				break;
			dirs_.push_back(p);
			p = parent(p);
		}
	}

	// el archivo se acaba de CREAR (open con O_EXCL tuvo exito): se anota sin consultar el disco
	void addFile(const std::string& path) { files_.push_back(path); }

	// llamar una vez por carpeta ANTES de crearla: anota las que todavia no existen
	void beforeDir(const std::string& dir)
	{
		struct stat st = {};
		std::string p = dir;
		while (!p.empty() && p.find('/') != std::string::npos)
		{
			if (stat(p.c_str(), &st) == 0)
				break;
			dirs_.push_back(p);
			p = parent(p);
		}
	}

	// borra lo que se creo, y las carpetas que quedaron vacias
	void rollback()
	{
		for (const auto& f : files_)
			std::remove(f.c_str());

		std::sort(dirs_.begin(), dirs_.end());
		dirs_.erase(std::unique(dirs_.begin(), dirs_.end()), dirs_.end());
		std::sort(dirs_.begin(), dirs_.end(),
				  [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
		for (const auto& d : dirs_)
			rmdir(d.c_str()); // solo funciona si esta vacia

		clear();
	}

private:
	static std::string parent(const std::string& p)
	{
		auto pos = p.find_last_of('/');
		return pos == std::string::npos ? std::string() : p.substr(0, pos);
	}

	std::vector<std::string> files_;
	std::vector<std::string> dirs_;
};

inline LibgetCreatedTracker g_libget_created;
