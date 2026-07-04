#if defined(SWITCH)
#include <switch.h>
#else
#include <unistd.h>
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iostream>

#include "SupporterBenefit.hpp"
#include "SimpleCipher.hpp"
#include "main.hpp"

#include "../libs/get/src/Utils.hpp"
#include "../libs/get/src/libs/rapidjson/include/rapidjson/document.h"

bool gIsSupporter = false;

// carpeta donde Atmosphere guarda los respaldos automaticos de PRODINFO
// (ofuscada en compilacion, ver BACKUPS_DIR en main.hpp)
static const std::string kBackupsDir = BACKUPS_DIR;

// sufijo esperado y longitud del identificador, segun el patron
// "**************_PRODINFO.bin" (14 caracteres alfanumericos + sufijo)
// (ofuscado en compilacion, ver BACKUP_SUFFIX en main.hpp)
static const std::string kBackupSuffix = BACKUP_SUFFIX;
static const size_t kBackupIdLength = 14;

// Por privacidad, NO se compara ni se conserva el identificador completo
// de 14 caracteres: solo se usan los ULTIMOS 7 caracteres (informacion
// parcial) tanto para comparar contra apoyo.json como para todo lo demas
// (por ejemplo el "gracias por tu apoyo" en el sidebar).
static const size_t kCompareIdLength = 7;

// ---------------------------------------------------------------------------
// Acceso alterno para quien no quiera compartir su numero de serie (o ya
// lo haya borrado). Si este archivo existe en la misma carpeta que los
// backups de PRODINFO, se le da el mismo acceso que a un beneficiario
// validado por apoyo.json. Se comparte manualmente (ej. por el staff de
// Switch Scene) a quien corresponda; no requiere conexion a internet.
// ---------------------------------------------------------------------------
// (ofuscado en compilacion, ver MANUAL_ACCESS_FILE en main.hpp)
static const std::string kManualAccessFileName = MANUAL_ACCESS_FILE;

// ---------------------------------------------------------------------------
// Revisa si un nombre de archivo cumple con el patron de backup de PRODINFO
// (14 caracteres alfanumericos + sufijo). El patron completo se usa SOLO
// para validar el nombre del archivo; lo que se devuelve en outId son
// unicamente los ULTIMOS 7 caracteres (informacion parcial), que es lo
// unico que se conserva en memoria y se compara contra apoyo.json.
// ---------------------------------------------------------------------------
static bool extractBackupId(const std::string& filename, std::string& outId)
{
	size_t suffixLen = kBackupSuffix.size();

	if (filename.size() != kBackupIdLength + suffixLen)
		return false;

	if (filename.compare(kBackupIdLength, suffixLen, kBackupSuffix) != 0)
		return false;

	std::string fullId = filename.substr(0, kBackupIdLength);
	for (char c : fullId)
	{
		if (!std::isalnum(static_cast<unsigned char>(c)))
			return false;
	}

	// solo nos quedamos con los ultimos 7 caracteres (informacion parcial)
	outId = fullId.substr(kBackupIdLength - kCompareIdLength, kCompareIdLength);
	return true;
}

// ---------------------------------------------------------------------------
// Recorre sdmc:/atmosphere/automatic_backups/ y devuelve el identificador
// PARCIAL (ultimos 7 caracteres) del backup MAS RECIENTE (por fecha de
// modificacion) entre los archivos *_PRODINFO.bin validos.
//
// Solo se usa el mas reciente (en vez de comparar contra todos) para que
// un backup viejo que haya quedado en la SD no pueda producir un falso
// positivo/negativo: siempre se evalua el backup vigente de la consola.
// ---------------------------------------------------------------------------
static bool findLatestLocalBackupId(std::string& outId)
{
	DIR* dir = opendir(kBackupsDir.c_str());
	if (!dir)
	{
		std::cout << "[Supporter] No se pudo abrir " << kBackupsDir << std::endl;
		return false;
	}

	bool found = false;
	time_t latestMtime = 0;
	std::string latestId;

	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		std::string name(entry->d_name);
		std::string id;
		if (!extractBackupId(name, id))
			continue;

		// intentar usar la fecha de modificacion para preferir el backup
		// mas reciente cuando hay varios. Si stat() falla por cualquier
		// razon, el archivo NO se descarta — sigue siendo un candidato
		// valido (solo no participa en la comparacion de fecha). Antes,
		// un fallo de stat() aqui hacia "continue" y el archivo se
		// perdia por completo, aunque su nombre fuera valido.
		std::string fullPath = kBackupsDir + "/" + name;
		struct stat fileInfo{};
		bool gotStat = (stat(fullPath.c_str(), &fileInfo) == 0);
		time_t mtime = gotStat ? fileInfo.st_mtime : 0;

		if (!found || (gotStat && mtime > latestMtime))
		{
			found = true;
			latestMtime = mtime;
			latestId = id;
		}
	}

	closedir(dir);

	if (found)
		outId = latestId;

	return found;
}

// ---------------------------------------------------------------------------
// Revisa si existe sdmc:/atmosphere/automatic_backups/ApoyoGrupo0042.ini
// No importa su contenido, solo que exista.
// ---------------------------------------------------------------------------
static bool checkManualAccessFile()
{
	std::string path = kBackupsDir + "/" + kManualAccessFileName;
	struct stat fileInfo{};
	bool exists = (stat(path.c_str(), &fileInfo) == 0);

	if (exists)
		std::cout << "[Supporter] Acceso alterno detectado (" << kManualAccessFileName << ")" << std::endl;

	return exists;
}

bool checkSupporterStatus()
{
	// 1. buscar el backup local de PRODINFO mas reciente
	std::string localId;
	if (!findLatestLocalBackupId(localId))
	{
		std::cout << "[Supporter] No se encontro ningun backup de PRODINFO en " << kBackupsDir << std::endl;
		// sin backup no se puede comparar contra apoyo.json, pero todavia
		// puede tener acceso por el archivo alterno
		return checkManualAccessFile();
	}

	// 2. descargar apoyo.json del mismo repositorio que valido.json
	//
	// raw.githubusercontent.com esta detras de un CDN (Fastly) que cachea
	// cada URL por varios minutos. Sin esto, tras editar apoyo.json en
	// GitHub la app podria seguir viendo la version vieja durante ese
	// tiempo. Se agrega un parametro con la hora actual para que cada
	// consulta sea una URL distinta y el CDN no devuelva algo cacheado.
	std::string fileData;
	const std::string url = std::string(SWITCH_REPO) + "/apoyo.json?nocache=" + std::to_string((long long)time(nullptr));

	bool downloaded = false;
	const int maxAttempts = 5;
	for (int attempt = 1; attempt <= maxAttempts; attempt++)
	{
		fileData.clear();
		if (downloadFileToMemory(url, &fileData))
		{
			downloaded = true;
			break;
		}

		std::cout << "[Supporter] Intento " << attempt << "/" << maxAttempts
		          << " fallo (" << gLastCurlErrorMsg << "), reintentando..." << std::endl;

#if defined(SWITCH)
		svcSleepThread(1'000'000'000ULL); // 1 segundo
#else
		usleep(1000 * 1000); // 1 segundo
#endif
	}

	if (!downloaded)
	{
		std::cout << "[Supporter] No se pudo descargar apoyo.json" << std::endl;
		return checkManualAccessFile();
	}

	// 3. intentar leer el contenido descargado TAL CUAL como JSON primero.
	// Esto hace la transicion a apoyo.json encriptado segura: si por
	// cualquier razon el archivo que esta publicado en el repositorio
	// todavia es JSON en texto plano (ej. no se subio la version
	// encriptada con encrypt_apoyo.py, o se subio sin querer), el
	// programa lo sigue reconociendo en vez de fallar silenciosamente.
	rapidjson::Document doc;
	doc.Parse(fileData.data(), fileData.size());
	bool esTextoPlanoValido = !doc.HasParseError() && doc.IsObject() && doc.HasMember("ids") && doc["ids"].IsArray();

	if (!esTextoPlanoValido)
	{
		// 3b. no era JSON plano valido: intentar desencriptar.
		// apoyo.json se publica como base64(RC4(json, APOYO_KEY)) para
		// que alguien que entre al repositorio no vea la lista de
		// seriales parciales en texto plano (ver SimpleCipher.hpp/.cpp).
		std::string cipherBytes = base64Decode(fileData);
		std::string jsonData = rc4(cipherBytes, APOYO_KEY);

		// se usa Parse(ptr, len) en vez de Parse(c_str()) porque el
		// resultado desencriptado se maneja como bytes con tamaño
		// explicito, sin asumir que este termina en un caracter nulo
		doc.Parse(jsonData.data(), jsonData.size());

		if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("ids") || !doc["ids"].IsArray())
		{
			std::cout << "[Supporter] apoyo.json invalido (ni texto plano ni desencriptado con la clave actual dieron JSON valido)" << std::endl;
			return checkManualAccessFile();
		}

		std::cout << "[Supporter] apoyo.json leido desencriptado" << std::endl;
	}
	else
	{
		std::cout << "[Supporter] apoyo.json leido en texto plano (sin encriptar)" << std::endl;
	}

	// 4. comparar el id local (parcial, 7 caracteres, del backup mas reciente)
	//    contra la lista de apoyo.json. Por compatibilidad, si en apoyo.json
	//    quedara guardado un id mas largo (ej. el formato viejo de 14
	//    caracteres), solo se comparan sus ULTIMOS 7 caracteres.
	const rapidjson::Value& supporterIds = doc["ids"];
	for (rapidjson::SizeType i = 0; i < supporterIds.Size(); i++)
	{
		if (!supporterIds[i].IsString())
			continue;

		std::string supporterId = supporterIds[i].GetString();
		if (supporterId.size() > kCompareIdLength)
			supporterId = supporterId.substr(supporterId.size() - kCompareIdLength);

		if (localId == supporterId)
		{
			std::cout << "[Supporter] Usuario beneficiario (id " << localId << ")" << std::endl;
			return true;
		}
	}

	std::cout << "[Supporter] El id local (" << localId << ") no coincide con apoyo.json" << std::endl;

	// 5. metodo alterno: no coincidio por id, pero puede tener el archivo
	//    de acceso manual compartido para casos especiales
	return checkManualAccessFile();
}
