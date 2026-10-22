#ifndef PACKAGE_H
#define PACKAGE_H
#include "Manifest.hpp"
#include "Utils.hpp"
#include "rapidjson/document.h"
#include "rapidjson/rapidjson.h"
#include <optional>
#include <string>
#if defined(WII)
#define ROOT_PATH "/"
#elif defined(SWITCH)
#define ROOT_PATH "sdmc:/"
#elif defined(__WIIU__)
#define ROOT_PATH "fs:/vol/external01/"
#elif defined(_3DS)
#define ROOT_PATH "sdmc:/"
#else
#define ROOT_PATH "sdroot/"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

#define APP_SHORTNAME "appstore"

class Get;
class Repo;
class GetRepo;
class LocalRepo;
class OSCRepo;
class UniStoreRepo;

/**
 * A package is a single application that can be installed. It contains the URL to the zip file and any instructions to install it (like a GET manifest).
 *
 * The download and install process is handled here, but they will use logic in the parentRepo's class to get the zip URL and installation logic.
 */
class Package
{
public:
	explicit Package(int state);
	~Package();

	Package(const Package& other) = default;

	// Como Package tiene destructor propio, el compilador NO genera las
	// operaciones de "mover" por su cuenta: std::sort y los vectores copiaban
	// cada Package completo en cada intercambio (muy lento con cientos de
	// componentes). Se piden explicitamente.
	Package(Package&& other) noexcept = default;
	Package& operator=(const Package& other) = default;
	Package& operator=(Package&& other) noexcept = default;

	[[nodiscard]] std::string toString() const;
	bool downloadZip(std::string_view tmp_path, float* progress = nullptr, bool resume = false, int zipIndex = 1, int zipTotal = 1) const;
	bool downloadZip2(std::string_view tmp_path, float* progress = nullptr, bool resume = false, int zipIndex = 1, int zipTotal = 1) const;
	bool downloadZip3(std::string_view tmp_path, float* progress = nullptr, bool resume = false, int zipIndex = 1, int zipTotal = 1) const;
	bool downloadZip4(std::string_view tmp_path, float* progress = nullptr, bool resume = false, int zipIndex = 1, int zipTotal = 1) const;
	bool install(const std::string& pkg_path, const std::string& tmp_path, int zipIndex = 1, int zipTotal = 1);
	bool install2(const std::string& pkg_path, const std::string& tmp_path, int zipIndex = 2, int zipTotal = 2);
	bool install3(const std::string& pkg_path, const std::string& tmp_path, int zipIndex = 3, int zipTotal = 3);
	bool install4(const std::string& pkg_path, const std::string& tmp_path, int zipIndex = 4, int zipTotal = 4);
	// --- actualizacion incremental ("actualizacion" en repo.json) ---
	static constexpr int UPDATE_OK = 0;       // actualizado
	static constexpr int UPDATE_FALLBACK = 1; // el zip incremental no sirve: usar la descarga completa
	static constexpr int UPDATE_FAILED = 2;   // error real (disco, escritura...)
	bool downloadUpdateZip(std::string_view tmp_path) const;
	int installUpdate(const std::string& pkg_path, const std::string& tmp_path);

	bool remove(std::string_view pkg_path);
	[[nodiscard]] const char* statusString() const;
	[[nodiscard]] const char* statusStringDisplay() const;
	void updateStatus(const std::string& pkg_path);
	
	// check if a partial download exists for this package, return percentage
	[[nodiscard]] int hasPartialDownload(std::string_view tmp_path) const;
	[[nodiscard]] long getPartialDownloadSize(std::string_view tmp_path) const;

	[[nodiscard]] std::string getIconUrl() const;
	[[nodiscard]] std::string getBannerUrl() const;
	[[nodiscard]] std::string getManifestUrl() const;
	[[nodiscard]] std::string getScreenShotUrl(int count) const;

	int isPreviouslyInstalled();
	[[nodiscard]] std::string getUrlFileExt() const;

	Manifest manifest;

	[[nodiscard]] const std::string& getPackageName() const
	{
		return pkg_name;
	}

	[[nodiscard]] const std::string& getTitle() const
	{
		return title;
	}

	[[nodiscard]] const std::string& getAuthor() const
	{
		return author;
	}

	[[nodiscard]] const std::string& getShortDescription() const
	{
		return short_desc;
	}

	[[nodiscard]] const std::string& getLongDescription() const
	{
		return long_desc;
	}

	// ruta (relativa a ROOT_PATH) del .nsp/.nsz/.xci a instalar
	// automaticamente tras la extraccion, si el repo.json trae la clave
	// "instalacion". Vacio si el paquete no la define -> no se instala nada.
	[[nodiscard]] const std::string& getInstallNsp() const
	{
		return install_nsp;
	}

	// URL del .nsp/.nsz a instalar directo desde internet ("instalacion_directa").
	[[nodiscard]] const std::string& getDirectInstallUrl() const
	{
		return direct_install_url;
	}

	// true si el paquete se instala SOLO por "instalacion_directa": trae esa
	// linea y ningun zip_url (ni _2/_3/_4). No hay zips que bajar.
	[[nodiscard]] bool isDirectOnly() const
	{
		return !direct_install_url.empty() && zipUrl.empty() && zipUrl2.empty()
		       && zipUrl3.empty() && zipUrl4.empty();
	}

	// Escribe packages/<nombre>/manifest.install + info.json para que el paquete
	// figure como instalado (version incluida). Se usa tras una instalacion
	// directa exitosa, donde no hay zip que traiga esos archivos.
	bool writeDirectInstallRecord(const std::string& pkg_path);

	// instruccion final que se muestra en el mensaje de resultado (viene
	// del campo "mensaje"/"message" de repo.json). Vacio si el paquete no
	// define ninguna.
	[[nodiscard]] const std::string& getInstallMessage() const
	{
		return install_message;
	}

	// aviso a mostrar antes de descargar ("aviso" en repo.json). Vacio = sin aviso.
	[[nodiscard]] const std::string& getNotice() const
	{
		return notice;
	}

	// URL del zip de actualizacion incremental ("actualizacion"). Vacio = no hay.
	[[nodiscard]] const std::string& getUpdateUrl() const
	{
		return update_url;
	}

	// true si, al actualizar, se puede usar el zip incremental en vez de los zips completos
	[[nodiscard]] bool canUseUpdateZip() const
	{
		if (update_url.empty())
			return false;
		// si el repo indica desde que version aplica, la version instalada debe coincidir
		return update_base.empty() || update_base == installed_version;
	}

	// Resultado (en tiempo de ejecucion) del intento de instalar el NSP.
	// Lo llena AppDetails::proceed() despues de la instalacion; vacio si
	// no habia orden de instalacion para este paquete. No viene de
	// repo.json, por eso vive fuera del bloque "private + friend".
	std::string runtime_install_status;

	// true si la ultima instalacion de este paquete se hizo con el zip de
	// actualizacion incremental ("actualizacion"). En ese caso el zip solo trae
	// lo que cambio, asi que el archivo de "instalacion" puede no venir en el zip
	// (ya se instalo y se borro en la version anterior): no hay nada que instalar.
	bool runtime_used_delta = false;

	[[nodiscard]] const std::string& getVersion() const
	{
		return version;
	}

	[[nodiscard]] const std::string& getLicense() const
	{
		return license;
	}

	[[nodiscard]] const std::string& getChangelog() const
	{
		return changelog;
	}

	[[nodiscard]] int getStatus() const
	{
		return status;
	}

	[[nodiscard]] int getDownloadCount() const {
		return downloads;
	}

	[[nodiscard]] uint64_t getDownloadSize() const {
		return download_size;
	}

	[[nodiscard]] uint64_t getExtractedSize() const {
		return extracted_size;
	}

	[[nodiscard]] std::string getHumanDownloadSize() const {
		return getHumanReadableBytes(download_size);
	}

	[[nodiscard]] std::string getHumanExtractedSize() const {
		return getHumanReadableBytes(extracted_size);
	}

	[[nodiscard]] int getScreenshotCount() const {
		return screens;
	}

	[[nodiscard]] int getUpdatedAtTimestamp() const {
		return updated_timestamp;
	}

	[[nodiscard]] const std::string& getUpdatedAt() const {
		return updated;
	}

	[[nodiscard]] const std::string& getCategory() const {
		return category;
	}

	[[nodiscard]] const std::string& getBinary() const {
		return binary;
	}

	// these are exposed, since they can be overridden in some contexts
	std::string title;
	std::string author;
	std::string short_desc;
	std::string long_desc;


	std::string url;
private:
	// Package attributes
	std::string pkg_name;
	std::string version;

	std::string license;
	std::string changelog;

	std::string sourceUrl;
	std::string zipUrl;   // optional direct zip URL (overrides repo default)
	std::string zipUrl2;  // optional second zip URL for split packages
	std::string zipUrl3;  // optional third zip URL for split packages
	std::string zipUrl4;  // optional fourth zip URL for split packages
	std::string install_nsp; // "instalacion" en repo.json: ruta (relativa a
	                          // ROOT_PATH) del .nsp/.nsz/.xci a instalar
	                          // automaticamente. Vacio por defecto -> no se
	                          // instala nada.
	std::string direct_install_url; // "instalacion_directa" en repo.json
	std::string install_message; // "mensaje"/"message" en repo.json:
	                              // instruccion final post-instalacion.
	std::string notice;       // "aviso" en repo.json
	std::string update_url;   // "actualizacion" en repo.json
	std::string update_base;  // "actualizacion_desde" en repo.json (opcional)
	std::string installed_version; // version leida del info.json instalado (lo llena updateStatus)
	std::string iconUrl;

	std::string updated;
	std::string binary;

	int updated_timestamp = 0;

	int downloads = 0;
	uint64_t extracted_size = 0;
	uint64_t download_size = 0;
	int screens = 0;

	std::string category;

	// Sorting attributes
	std::shared_ptr<Repo> mRepo;

	int status; // local, update, installed, get

	// bitmask for permissions, from left to right:
	// unused, iosu, kernel, nand, usb, sd, wifi, sound
	char permissions{};

	friend Get;
	friend Repo;
	friend GetRepo;
	friend LocalRepo;
	friend OSCRepo;
	friend UniStoreRepo;
};

#endif
