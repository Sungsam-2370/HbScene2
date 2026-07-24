#include "NspAutoInstall.hpp"
#include "InstallEngine.hpp"

#include <sys/stat.h>

namespace nspinstall {

AutoInstallResult InstallNspIfRequested(const std::string &root_path,
                                         const std::string &relative_nsp_path,
                                         bool install_to_nand,
                                         AutoInstallProgress progress) {
    AutoInstallResult result;

    // Paso 3 del flujo pedido: si el repo.json no trae la orden de
    // instalación, no hacemos absolutamente nada.
    if (relative_nsp_path.empty()) {
        result.nothing_to_do = true;
        result.success = true; // no es un fallo, simplemente no aplica
        return result;
    }

    // Normaliza la ruta: acepta tanto "carpeta/archivo.nsp" como "/carpeta/archivo.nsp"
    std::string clean = relative_nsp_path;
    if (!clean.empty() && clean.front() == '/') {
        clean.erase(0, 1);
    }

    std::string root = root_path;
    if (!root.empty() && root.back() != '/') {
        root += '/';
    }

    const std::string full_path = root + clean;

    struct stat st{};
    if (stat(full_path.c_str(), &st) != 0) {
        result.success = false;
        result.message = "No se encontró el NSP a instalar tras la extracción:\n" + full_path;
        return result;
    }

    ::nspinstall::InstallConfig config;
    config.dest_storage_id = install_to_nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
    config.ignore_req_fw   = true;
    config.reinstall_ncas  = false;

    ::nspinstall::InstallResult install_res = ::nspinstall::InstallFromLocalFile(
        full_path, config,
        [&progress](const ::nspinstall::InstallProgress &p) {
            if (progress) {
                progress(p.bytes_done, p.bytes_total, p.current_nca);
            }
        });

    result.success = install_res.success;
    if (install_res.success) {
        result.message = "INSTALACION EXITOSA";
    } else {
        result.message = "No se pudo instalar el NSP automáticamente:\n" + install_res.error_message;
    }
    return result;
}

}
