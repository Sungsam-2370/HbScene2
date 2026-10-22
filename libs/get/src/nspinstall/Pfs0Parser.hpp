#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ByteSource.hpp"

namespace nspinstall {

struct Pfs0Entry {
    std::string   name;
    std::uint64_t offset;
    std::uint64_t size;
};

bool ParsePfs0(const std::string &path,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset);

// Igual, pero leyendo de cualquier fuente de bytes (archivo local o HTTP).
// Solo lee el encabezado (tabla de archivos + tabla de nombres), no el contenido.
bool ParsePfs0(ByteSource &source,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset);

const Pfs0Entry *FindEntryByExtension(const std::vector<Pfs0Entry> &entries,
                                       const std::string &extension);

std::vector<const Pfs0Entry *> FindEntriesByExtension(
    const std::vector<Pfs0Entry> &entries,
    const std::string &extension);

const Pfs0Entry *FindEntryByNcaId(const std::vector<Pfs0Entry> &entries,
                                   const std::string &nca_id_hex);

}
