#pragma once

// ---------------------------------------------------------------------------
// ByteSource: de donde salen los bytes de un NSP/NSZ.
//
// El instalador solo necesita "leer N bytes desde el offset X" y "recorrer un
// rango en trozos". No le importa si los bytes vienen de un archivo en la SD
// (FileByteSource, lo de siempre) o, mas adelante, directo de internet con
// HTTP Range (sin que el .nsp exista nunca como archivo en la SD).
//
// Todo esta en este header a proposito (sin .cpp) para no tener que tocar el
// Makefile del proyecto.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nspinstall {

class ByteSource {
public:
    using WriteFn = std::function<bool(const void *, std::size_t)>;

    virtual ~ByteSource() = default;

    // Lee EXACTAMENTE len bytes desde offset. false si hubo error o faltaron bytes.
    virtual bool Read(std::uint64_t offset, void *buf, std::size_t len) = 0;

    // Recorre [offset, offset + size) en trozos, llamando write_fn con cada uno
    // (en orden). Si write_fn devuelve false se detiene y devuelve false.
    // Devuelve true solo si se entrego el rango completo.
    virtual bool ReadRange(std::uint64_t offset, std::uint64_t size, const WriteFn &write_fn) = 0;

    // Ruta del archivo local, si esta fuente es un archivo (vacio si no).
    // Hay codigo (descompresion NCZ a un temporal) que solo sabe trabajar con
    // rutas; con una fuente que no es un archivo local ese camino no existe.
    virtual std::string LocalPath() const { return std::string(); }
};

// Archivo local (SD). Se comporta igual que el codigo anterior (fopen/fseek/fread).
class FileByteSource : public ByteSource {
public:
    explicit FileByteSource(const std::string &path) : path_(path) {
        file_ = std::fopen(path.c_str(), "rb");
    }

    ~FileByteSource() override {
        if(file_) std::fclose(file_);
    }

    FileByteSource(const FileByteSource &) = delete;
    FileByteSource &operator=(const FileByteSource &) = delete;

    bool IsOpen() const { return file_ != nullptr; }

    bool Read(std::uint64_t offset, void *buf, std::size_t len) override {
        if(!file_) return false;
        if(len == 0) return true;
        if(std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
        return std::fread(buf, 1, len, file_) == len;
    }

    bool ReadRange(std::uint64_t offset, std::uint64_t size, const WriteFn &write_fn) override {
        if(!file_) return false;
        if(size == 0) return true;
        if(std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;

        std::vector<std::uint8_t> buf(kChunkSize);
        std::uint64_t left = size;
        while(left > 0) {
            const std::size_t want = static_cast<std::size_t>(left < kChunkSize ? left : kChunkSize);
            const std::size_t got  = std::fread(buf.data(), 1, want, file_);
            if(got == 0) return false;
            if(!write_fn(buf.data(), got)) return false;
            left -= got;
        }
        return true;
    }

    std::string LocalPath() const override { return path_; }

private:
    static constexpr std::size_t kChunkSize = 512 * 1024;

    std::string path_;
    FILE *file_ = nullptr;
};

}
