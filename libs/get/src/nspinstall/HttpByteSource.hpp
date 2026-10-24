#pragma once

// ---------------------------------------------------------------------------
// HttpByteSource: lee un .nsp/.nsz directamente de internet con HTTP Range,
// sin que el archivo exista nunca en la SD (asi no importa el limite de 4 GB
// de FAT32).
//
// - Open() pregunta al servidor el tamano y si acepta Range.
// - ReadRange() baja el rango en trozos de 4 MB con varias conexiones a la
//   vez (una sola conexion esta limitada por su ventana TCP, igual que en las
//   descargas de zips) y entrega los trozos EN ORDEN.
// - Si un trozo falla (se corta la conexion, etc.) se reintenta; si el archivo
//   cambia en el servidor a mitad de la instalacion (otro ETag) se aborta.
//
// Todo en este header (sin .cpp) para no tocar el Makefile.
// ---------------------------------------------------------------------------

#include "ByteSource.hpp"

#ifndef NETWORK_MOCK

#include <curl/curl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <unistd.h>

#include "../Utils.hpp" // setPlatformCurlFlags()
#include "../Cancel.hpp"

namespace nspinstall {

class HttpByteSource : public ByteSource {
public:
    static constexpr std::uint64_t kChunkSize     = 4ull * 1024 * 1024; // lo que pide cada peticion
    static constexpr int           kWindowChunks  = 8;                  // trozos en memoria a la vez
    static constexpr int           kMaxRetries    = 6;                  // por trozo
    static constexpr std::size_t   kDeliverSlice  = 1024 * 1024;        // tamano maximo por write_fn

    explicit HttpByteSource(std::string url, int connections = 4)
        : url_(std::move(url)), connections_(connections < 1 ? 1 : connections) {}

    ~HttpByteSource() override { CloseWorkers(); }

    HttpByteSource(const HttpByteSource &) = delete;
    HttpByteSource &operator=(const HttpByteSource &) = delete;

    // Consulta al servidor. false (con el motivo en error) si no se puede usar.
    bool Open(std::string &error) {
        Probe probe;
        long code = 0;
        curl_off_t length = 0;

        CURL *c = curl_easy_init();
        if(!c) { error = "curl_easy_init fallo"; return false; }
        setPlatformCurlFlags(c);
        curl_easy_setopt(c, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, &HttpByteSource::ProbeHeader);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &probe);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
        const CURLcode rc = curl_easy_perform(c);
        if(rc == CURLE_OK) {
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
            curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
        }
        curl_easy_cleanup(c);

        if(rc != CURLE_OK) {
            error = std::string("No se pudo conectar: ") + curl_easy_strerror(rc);
            return false;
        }
        if(code != 200) {
            error = "El servidor respondio HTTP " + std::to_string(code);
            return false;
        }
        if(length <= 0) {
            error = "El servidor no informo el tamano del archivo";
            return false;
        }
        if(!probe.accept_ranges) {
            error = "El servidor no admite descargas por rangos (Accept-Ranges)";
            return false;
        }

        total_ = static_cast<std::uint64_t>(length);
        etag_  = probe.etag;
        open_  = true;
        return true;
    }

    std::uint64_t Size() const { return total_; }

    bool Read(std::uint64_t offset, void *buf, std::size_t len) override {
        if(len == 0) return true;
        auto *dst = static_cast<std::uint8_t *>(buf);
        std::size_t got = 0;
        const bool ok = ReadRange(offset, len, [&](const void *d, std::size_t n) -> bool {
            if(got + n > len) return false;
            std::memcpy(dst + got, d, n);
            got += n;
            return true;
        });
        return ok && got == len;
    }

    bool ReadRange(std::uint64_t offset, std::uint64_t size, const WriteFn &write_fn) override {
        if(!open_) return false;
        if(size == 0) return true;
        if(offset > total_ || size > total_ - offset) return false;
        if(!EnsureWorkers()) return false;

        const long nchunks = static_cast<long>((size + kChunkSize - 1) / kChunkSize);
        std::vector<Chunk> chunks(static_cast<std::size_t>(nchunks));
        for(long i = 0; i < nchunks; i++) {
            const std::uint64_t b = offset + static_cast<std::uint64_t>(i) * kChunkSize;
            const std::uint64_t e = std::min<std::uint64_t>(b + kChunkSize, offset + size);
            chunks[i].begin    = b;
            chunks[i].expected = static_cast<std::size_t>(e - b);
            chunks[i].total    = total_;
        }

        long next_assign = 0;  // proximo trozo que nunca se ha pedido
        long next_deliver = 0; // proximo trozo a entregar (en orden)
        std::deque<long> retry_queue;
        bool failed = false;

        auto start_chunk = [&](Worker &w, long idx) {
            Chunk &c = chunks[idx];
            c.data.clear();
            c.data.reserve(c.expected);
            char range[64];
            std::snprintf(range, sizeof(range), "%llu-%llu",
                          static_cast<unsigned long long>(c.begin),
                          static_cast<unsigned long long>(c.begin + c.expected - 1));
            curl_easy_setopt(w.easy, CURLOPT_RANGE, range);
            curl_easy_setopt(w.easy, CURLOPT_WRITEDATA, &c);
            curl_easy_setopt(w.easy, CURLOPT_HEADERDATA, &c);
            c.range_ok = false;
            curl_multi_add_handle(multi_, w.easy);
            w.chunk = idx;
        };

        auto assign_work = [&]() -> bool {
            bool any = false;
            for(auto &w : workers_) {
                if(w.chunk >= 0) continue;
                if(!retry_queue.empty()) {
                    const long idx = retry_queue.front();
                    retry_queue.pop_front();
                    start_chunk(w, idx);
                    any = true;
                } else if(next_assign < nchunks && next_assign < next_deliver + kWindowChunks) {
                    start_chunk(w, next_assign++);
                    any = true;
                }
            }
            return any;
        };

        assign_work();

        while(!failed && next_deliver < nchunks) {
            if(libget_cancelled()) { failed = true; break; } // el usuario cancelo
            int running = 0;
            curl_multi_perform(multi_, &running);

            int msgs_left = 0;
            while(CURLMsg *msg = curl_multi_info_read(multi_, &msgs_left)) {
                if(msg->msg != CURLMSG_DONE) continue;
                Worker *w = nullptr;
                for(auto &cand : workers_)
                    if(cand.easy == msg->easy_handle) w = &cand;
                if(!w || w->chunk < 0) continue;

                long http = 0;
                curl_easy_getinfo(w->easy, CURLINFO_RESPONSE_CODE, &http);
                curl_multi_remove_handle(multi_, w->easy);

                Chunk &c = chunks[w->chunk];
                const bool ok = msg->data.result == CURLE_OK && http == 206 && c.range_ok
                                && c.data.size() == c.expected;
                if(ok) {
                    c.done = true;
                } else {
                    std::printf("[http] trozo %ld fallo (curl=%d http=%ld rango_ok=%d recibido=%zu/%zu), intento %d\n",
                                w->chunk, static_cast<int>(msg->data.result), http, (int)c.range_ok,
                                c.data.size(), c.expected, c.retries + 1);
                    // el archivo cambio en el servidor: reintentar no sirve de nada
                    const bool changed = (http == 200);
                    if(changed || ++c.retries > kMaxRetries) {
                        failed = true;
                    } else {
                        usleep(100000u * static_cast<unsigned>(c.retries)); // un respiro antes de reintentar
                        retry_queue.push_back(w->chunk);
                    }
                }
                w->chunk = -1;
            }

            // entregar en orden, en rebanadas de como maximo kDeliverSlice
            while(!failed && next_deliver < nchunks && chunks[next_deliver].done) {
                Chunk &c = chunks[next_deliver];
                std::size_t pos = 0;
                while(pos < c.data.size()) {
                    const std::size_t n = std::min<std::size_t>(kDeliverSlice, c.data.size() - pos);
                    if(!write_fn(c.data.data() + pos, n)) { failed = true; break; }
                    pos += n;
                }
                if(failed) break;
                std::string().swap(c.data); // liberar memoria
                next_deliver++;
            }

            if(failed) break;
            if(assign_work()) continue;

            int numfds = 0;
            curl_multi_wait(multi_, nullptr, 0, 100, &numfds);
            if(numfds == 0) usleep(2000);
        }

        // dejar los workers limpios para la siguiente llamada
        for(auto &w : workers_) {
            if(w.chunk >= 0) {
                curl_multi_remove_handle(multi_, w.easy);
                w.chunk = -1;
            }
        }
        return !failed;
    }

private:
    struct Probe {
        std::string etag;
        bool accept_ranges = false;
    };

    struct Chunk {
        std::string data;
        std::uint64_t begin = 0;
        std::size_t expected = 0;
        bool done = false;
        int retries = 0;
        bool range_ok = false; // el Content-Range de la respuesta es justo el rango pedido
        std::uint64_t total = 0; // tamano total del archivo (para validar Content-Range)
    };

    struct Worker {
        CURL *easy = nullptr;
        long chunk = -1;
    };

    static std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return s;
    }

    static std::size_t ProbeHeader(char *data, std::size_t size, std::size_t nmemb, void *userp) {
        auto *probe = static_cast<Probe *>(userp);
        const std::size_t n = size * nmemb;
        const std::string raw(data, n);
        const std::string low = Lower(raw);

        if(low.compare(0, 5, "http/") == 0) {
            // respuesta nueva (p. ej. despues de una redireccion): olvidar lo anterior
            probe->etag.clear();
            probe->accept_ranges = false;
        } else if(low.compare(0, 5, "etag:") == 0) {
            const std::size_t b = raw.find_first_not_of(" \t", 5);
            const std::size_t e = raw.find_last_not_of(" \t\r\n");
            if(b != std::string::npos && e != std::string::npos && e >= b)
                probe->etag = raw.substr(b, e - b + 1);
        } else if(low.compare(0, 14, "accept-ranges:") == 0) {
            probe->accept_ranges = low.find("bytes") != std::string::npos;
        }
        return n;
    }

    // Verifica que el servidor devolvio EXACTAMENTE el rango pedido
    // (Content-Range: bytes inicio-fin/total); asi nunca se acepta un trozo
    // de otro lugar del archivo.
    static std::size_t ChunkHeader(char *data, std::size_t size, std::size_t nmemb, void *userp) {
        auto *c = static_cast<Chunk *>(userp);
        const std::size_t n = size * nmemb;
        const std::string low = Lower(std::string(data, n));
        if(low.compare(0, 5, "http/") == 0) {
            c->range_ok = false;
        } else if(low.compare(0, 14, "content-range:") == 0) {
            unsigned long long a = 0, b = 0, t = 0;
            const std::size_t p = low.find("bytes");
            if(p != std::string::npos &&
               std::sscanf(low.c_str() + p, "bytes %llu-%llu/%llu", &a, &b, &t) == 3) {
                c->range_ok = (a == c->begin && b == c->begin + c->expected - 1 && t == c->total);
            }
        }
        return n;
    }

    // Un trozo no puede crecer mas de lo esperado: si el servidor ignora Range
    // y manda el archivo completo, se corta en vez de llenar la memoria.
    static std::size_t ChunkWrite(void *contents, std::size_t size, std::size_t nmemb, void *userp) {
        auto *c = static_cast<Chunk *>(userp);
        const std::size_t n = size * nmemb;
        if(c->data.size() + n > c->expected) return 0;
        c->data.append(static_cast<char *>(contents), n);
        return n;
    }

    bool EnsureWorkers() {
        if(multi_) return true;
        multi_ = curl_multi_init();
        if(!multi_) return false;
        curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, static_cast<long>(connections_));
#ifdef CURLPIPE_NOTHING
        // conexiones TCP separadas, no una sola multiplexada
        curl_multi_setopt(multi_, CURLMOPT_PIPELINING, static_cast<long>(CURLPIPE_NOTHING));
#endif
        if(!etag_.empty() && etag_.compare(0, 2, "W/") != 0) {
            const std::string h = "If-Range: " + etag_;
            headers_ = curl_slist_append(headers_, h.c_str());
        }

        workers_.resize(static_cast<std::size_t>(connections_));
        for(auto &w : workers_) {
            w.easy = curl_easy_init();
            if(!w.easy) return false;
            setPlatformCurlFlags(w.easy);
            curl_easy_setopt(w.easy, CURLOPT_URL, url_.c_str());
            curl_easy_setopt(w.easy, CURLOPT_WRITEFUNCTION, &HttpByteSource::ChunkWrite);
            curl_easy_setopt(w.easy, CURLOPT_HEADERFUNCTION, &HttpByteSource::ChunkHeader);
            curl_easy_setopt(w.easy, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(w.easy, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
            curl_easy_setopt(w.easy, CURLOPT_CONNECTTIMEOUT, 20L);
            // conexion practicamente muerta (<1 KB/s durante 30 s): abandonarla
            curl_easy_setopt(w.easy, CURLOPT_LOW_SPEED_LIMIT, 1024L);
            curl_easy_setopt(w.easy, CURLOPT_LOW_SPEED_TIME, 30L);
            if(headers_) curl_easy_setopt(w.easy, CURLOPT_HTTPHEADER, headers_);
        }
        return true;
    }

    void CloseWorkers() {
        for(auto &w : workers_) {
            if(w.easy) {
                if(w.chunk >= 0 && multi_) curl_multi_remove_handle(multi_, w.easy);
                curl_easy_cleanup(w.easy);
            }
        }
        workers_.clear();
        if(multi_) { curl_multi_cleanup(multi_); multi_ = nullptr; }
        if(headers_) { curl_slist_free_all(headers_); headers_ = nullptr; }
    }

    std::string url_;
    int connections_;
    bool open_ = false;
    std::uint64_t total_ = 0;
    std::string etag_;

    CURLM *multi_ = nullptr;
    curl_slist *headers_ = nullptr;
    std::vector<Worker> workers_;
};

}

#endif // NETWORK_MOCK
