#pragma once

// ---------------------------------------------------------------------------
// E/S con cache de lectura para minizip (solo lectura del .zip).
//
// minizip, por cada archivo extraido, hace varias lecturas pequeñas y saltos:
//   - la entrada del directorio central (que esta al FINAL del zip)
//   - la cabecera local del archivo
//   - los datos
// Con fopen/fread cada salto lejano descarta el buffer de stdio y obliga a otra
// lectura real de la SD. En la Switch cada lectura es una llamada IPC al servicio
// de archivos, asi que con miles de archivos pequeños eso pesa mucho.
//
// Aqui se mantienen DOS ventanas de lectura (256 KB cada una): una suele quedar
// sobre la zona de datos y la otra sobre el directorio central, asi que alternar
// entre ellas ya no cuesta lecturas extra, y los archivos pequeños consecutivos
// salen todos de una sola lectura. Las lecturas grandes (>= 256 KB, como los
// bloques de un archivo enorme) van directo al destino, sin copia intermedia.
//
// Todo en este header (sin .cpp) para no tocar el Makefile.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unzip.h"

namespace zipio {

constexpr uint64_t kBlock = 256 * 1024;

struct Window
{
	std::vector<uint8_t> buf;
	uint64_t start = 0;
	uint64_t len = 0;
};

struct File
{
	int fd = -1;
	uint64_t size = 0;
	uint64_t pos = 0;   // posicion logica (la que ve minizip)
	uint64_t fdpos = 0; // posicion real del descriptor
	Window win[2];
	int last = 0; // ultima ventana usada
	bool err = false;
};

// lee exactamente n bytes (o hasta el fin del archivo) desde offset, directo del descriptor
inline uint64_t rawRead(File* f, uint64_t offset, uint8_t* dst, uint64_t n)
{
	if (f->fdpos != offset)
	{
		if (lseek(f->fd, (off_t)offset, SEEK_SET) < 0)
		{
			f->err = true;
			return 0;
		}
		f->fdpos = offset;
	}
	uint64_t got = 0;
	while (got < n)
	{
		ssize_t r = read(f->fd, dst + got, (size_t)(n - got));
		if (r < 0)
		{
			f->err = true;
			break;
		}
		if (r == 0)
			break;
		got += (uint64_t)r;
		f->fdpos += (uint64_t)r;
	}
	return got;
}

inline voidpf ZCALLBACK openFn(voidpf, const void* filename, int mode)
{
	if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ || filename == nullptr)
		return nullptr;
	int fd = open((const char*)filename, O_RDONLY);
	if (fd < 0)
		return nullptr;
	struct stat st = {};
	if (fstat(fd, &st) != 0)
	{
		close(fd);
		return nullptr;
	}
	File* f = new File();
	f->fd = fd;
	f->size = (uint64_t)st.st_size;
	return f;
}

inline uLong ZCALLBACK readFn(voidpf, voidpf stream, void* buf, uLong size)
{
	File* f = (File*)stream;
	uint8_t* dst = (uint8_t*)buf;
	uint64_t want = size;
	uint64_t total = 0;

	while (want > 0 && f->pos < f->size)
	{
		uint64_t n = std::min<uint64_t>(want, f->size - f->pos);

		// ya esta en alguna ventana?
		Window* w = nullptr;
		for (int i = 0; i < 2; i++)
		{
			Window& c = f->win[i];
			if (c.len > 0 && f->pos >= c.start && f->pos < c.start + c.len)
			{
				w = &c;
				f->last = i;
				break;
			}
		}

		uint64_t take = 0;
		if (w != nullptr)
		{
			take = std::min<uint64_t>(n, w->start + w->len - f->pos);
			memcpy(dst, w->buf.data() + (f->pos - w->start), (size_t)take);
		}
		else if (n >= kBlock)
		{
			// lectura grande: directo al destino
			take = rawRead(f, f->pos, dst, n);
			if (take == 0)
				break;
		}
		else
		{
			// llenar la ventana que no se uso la ultima vez
			int idx = 1 - f->last;
			Window& c = f->win[idx];
			if (c.buf.empty())
				c.buf.resize((size_t)kBlock);
			uint64_t fill = std::min<uint64_t>(kBlock, f->size - f->pos);
			uint64_t got = rawRead(f, f->pos, c.buf.data(), fill);
			if (got == 0)
				break;
			c.start = f->pos;
			c.len = got;
			f->last = idx;
			continue; // ahora si esta en la ventana
		}

		f->pos += take;
		dst += take;
		want -= take;
		total += take;
	}
	return (uLong)total;
}

inline ZPOS64_T ZCALLBACK tellFn(voidpf, voidpf stream)
{
	return (ZPOS64_T)((File*)stream)->pos;
}

inline long ZCALLBACK seekFn(voidpf, voidpf stream, ZPOS64_T offset, int origin)
{
	File* f = (File*)stream;
	uint64_t np;
	switch (origin)
	{
	case ZLIB_FILEFUNC_SEEK_CUR:
		np = f->pos + offset;
		break;
	case ZLIB_FILEFUNC_SEEK_END:
		np = (uint64_t)((int64_t)f->size + (int64_t)offset); // minizip solo lo usa con offset 0
		break;
	case ZLIB_FILEFUNC_SEEK_SET:
		np = offset;
		break;
	default:
		return -1;
	}
	f->pos = np; // el salto es solo logico: no toca el disco
	return 0;
}

inline int ZCALLBACK closeFn(voidpf, voidpf stream)
{
	File* f = (File*)stream;
	if (f->fd >= 0)
		close(f->fd);
	delete f;
	return 0;
}

inline int ZCALLBACK errorFn(voidpf, voidpf stream)
{
	return ((File*)stream)->err ? 1 : 0;
}

inline unzFile openZip(const std::string& path)
{
	zlib_filefunc64_def ff;
	memset(&ff, 0, sizeof(ff));
	ff.zopen64_file = openFn;
	ff.zread_file = readFn;
	ff.zwrite_file = nullptr;
	ff.ztell64_file = tellFn;
	ff.zseek64_file = seekFn;
	ff.zclose_file = closeFn;
	ff.zerror_file = errorFn;
	ff.opaque = nullptr;
	return unzOpen2_64(path.c_str(), &ff);
}

} // namespace zipio
