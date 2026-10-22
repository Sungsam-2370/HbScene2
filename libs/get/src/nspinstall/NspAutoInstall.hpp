#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace nspinstall {

struct AutoInstallResult {
    // true si no había orden de instalación en el repo.json (no es un error,
    // simplemente no había nada que instalar)
    bool nothing_to_do = false;
    bool success       = false;
    std::string message; // texto listo para mostrar en el popup de resultado
};

// progreso opcional: (bytes_done, bytes_total, nombre del nca actual, preparando)
// "preparando" = true mientras se reserva espacio (CreatePlaceholder), fase
// sin progreso incremental real disponible.
using AutoInstallProgress = std::function<void(std::uint64_t, std::uint64_t, const std::string &, bool)>;

// root_path: ej. "sdmc:/"  (ROOT_PATH de HbScene)
// relative_nsp_path: valor tal cual viene de repo.json -> "instalacion"
//                    (ej. "forwarders/Emuladores/DrasticDS/DrasticDS.nsp")
// install_to_nand: si el usuario tiene activada la opción de instalar a NAND
//                  en vez de SD (misma idea que config->install_to_nand en BrowseTab)
// Borra SOLO el archivo indicado por "instalacion" (ruta relativa a root_path),
// se haya instalado bien o no. Devuelve true si el archivo ya no existe al
// terminar (borrado ahora, o no existia). Nunca toca otros archivos ni carpetas:
// rechaza rutas vacias o con "..".
bool DeleteInstallFile(const std::string &root_path, const std::string &relative_nsp_path);

AutoInstallResult InstallNspIfRequested(const std::string &root_path,
                                         const std::string &relative_nsp_path,
                                         bool install_to_nand,
                                         AutoInstallProgress progress = nullptr);

// Instalacion directa: lee el .nsp/.nsz de internet (HTTP Range) y lo instala
// mientras se descarga; el archivo nunca se escribe en la SD (sin limite de 4 GB).
// url: valor de "instalacion_directa" en repo.json.
AutoInstallResult InstallNspFromUrl(const std::string &url,
                                    bool install_to_nand,
                                    AutoInstallProgress progress = nullptr);

}
