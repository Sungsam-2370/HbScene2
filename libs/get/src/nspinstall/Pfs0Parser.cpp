#include "Pfs0Parser.hpp"
#include "NcaStructs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace nspinstall {
namespace {

bool EndsWith(const std::string &value, const std::string &suffix) {
    if(suffix.size() > value.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

}

bool ParsePfs0(const std::string &path,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset) {
    entries.clear();
    data_offset = 0;

    FileByteSource source(path);
    if(!source.IsOpen()) return false;
    return ParsePfs0(source, entries, data_offset);
}

bool ParsePfs0(ByteSource &source,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset) {
    entries.clear();
    data_offset = 0;

    Pfs0Header header{};
    if(!source.Read(0, &header, sizeof(header))) return false;
    if(header.magic != kMagicPfs0) return false;
    if(header.file_count == 0) return true;

    // los datos pueden venir de internet: no confiar en tamanos absurdos
    constexpr std::uint32_t kMaxFiles       = 100000;
    constexpr std::uint32_t kMaxStringTable = 64u * 1024u * 1024u;
    if(header.file_count > kMaxFiles || header.string_table_size > kMaxStringTable) return false;

    const std::size_t entry_table_size = header.file_count * sizeof(Pfs0FileEntry);
    auto entry_buf = std::make_unique<std::uint8_t[]>(entry_table_size);
    if(!source.Read(sizeof(Pfs0Header), entry_buf.get(), entry_table_size)) return false;

    // +1 y terminador: los nombres se leen como cadenas C
    auto string_buf = std::make_unique<char[]>(static_cast<std::size_t>(header.string_table_size) + 1);
    if(!source.Read(sizeof(Pfs0Header) + entry_table_size, string_buf.get(), header.string_table_size)) {
        return false;
    }
    string_buf[header.string_table_size] = '\0';

    const std::uint64_t data_start = sizeof(Pfs0Header) +
                                     entry_table_size +
                                     header.string_table_size;
    data_offset = data_start;

    const auto *raw_entries = reinterpret_cast<const Pfs0FileEntry *>(entry_buf.get());
    entries.reserve(header.file_count);

    for(std::uint32_t i = 0; i < header.file_count; i++) {
        Pfs0Entry entry;
        if(raw_entries[i].string_table_offset < header.string_table_size) {
            entry.name = &string_buf[raw_entries[i].string_table_offset];
        }
        entry.offset = data_start + raw_entries[i].data_offset;
        entry.size   = raw_entries[i].file_size;
        entries.push_back(std::move(entry));
    }

    return true;
}

const Pfs0Entry *FindEntryByExtension(const std::vector<Pfs0Entry> &entries,
                                       const std::string &extension) {
    for(const auto &entry : entries) {
        if(EndsWith(entry.name, extension)) return &entry;
    }
    return nullptr;
}

std::vector<const Pfs0Entry *> FindEntriesByExtension(
    const std::vector<Pfs0Entry> &entries,
    const std::string &extension) {
    std::vector<const Pfs0Entry *> result;
    for(const auto &entry : entries) {
        if(EndsWith(entry.name, extension)) result.push_back(&entry);
    }
    return result;
}

const Pfs0Entry *FindEntryByNcaId(const std::vector<Pfs0Entry> &entries,
                                   const std::string &nca_id_hex) {
    for(const auto &entry : entries) {
        if(entry.name.size() >= 32) {
            bool match = std::equal(
                nca_id_hex.begin(), nca_id_hex.end(), entry.name.begin(),
                [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });
            if(match) return &entry;
        }
    }
    return nullptr;
}

}
