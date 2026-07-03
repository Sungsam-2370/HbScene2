#if defined(SWITCH)
#include <switch.h>
#else
#include <unistd.h>
#endif

#include <dirent.h>
#include <cctype>
#include <cstring>
#include <iostream>
#include <vector>

#include "SupporterBenefit.hpp"
#include "main.hpp"

#include "../libs/get/src/Utils.hpp"
#include "../libs/get/src/libs/rapidjson/include/rapidjson/document.h"

bool gIsSupporter = false;

// carpeta donde Atmosphere guarda los respaldos automaticos de PRODINFO
static const char* kBackupsDir = "sdmc:/atmosphere/automatic_backups";

// sufijo esperado y longitud del identificador, segun el patron
// "**************_PRODINFO.bin" (14 caracteres alfanumericos + sufijo)
static const char* kBackupSuffix = "_PRODINFO.bin";
static const size_t kBackupIdLength = 14;

// Por privacidad, NO se compara ni se conserva el identificador completo
// de 14 caracteres: solo se usan los ULTIMOS 7 caracteres (informacion
// parcial) tanto para comparar contra apoyo.json como para todo lo demas
// (por ejemplo el "gracias por tu apoyo" en el sidebar).
static const size_t kCompareIdLength = 7;

// ---------------------------------------------------------------------------
// Revisa si un nombre de archivo cumple con el patron de backup de PRODINFO
// (14 caracteres alfanumericos + sufijo). El patron completo se usa SOLO
// para validar el nombre del archivo; lo que se devuelve en outId son
// unicamente los ULTIMOS 7 caracteres (informacion parcial), que es lo
// unico que se conserva en memoria y se compara contra apoyo.json.
// ---------------------------------------------------------------------------
static bool extractBackupId(const std::string& filename, std::string& outId)
{
	size_t suffixLen = strlen(kBackupSuffix);

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
// Recorre sdmc:/atmosphere/automatic_backups/ y junta los identificadores
// PARCIALES (ultimos 7 caracteres) de cada archivo *_PRODINFO.bin valido
// (normalmente solo deberia existir uno, pero se juntan todos por si acaso)
// ---------------------------------------------------------------------------
static std::vector<std::string> findLocalBackupIds()
{
	std::vector<std::string> ids;

	DIR* dir = opendir(kBackupsDir);
	if (!dir)
	{
		std::cout << "[Supporter] No se pudo abrir " << kBackupsDir << std::endl;
		return ids;
	}

	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		std::string name(entry->d_name);
		std::string id;
		if (extractBackupId(name, id))
			ids.push_back(id);
	}

	closedir(dir);
	return ids;
}

bool checkSupporterStatus()
{
	// 1. buscar identificadores locales de PRODINFO
	std::vector<std::string> localIds = findLocalBackupIds();
	if (localIds.empty())
	{
		std::cout << "[Supporter] No se encontro ningun backup de PRODINFO en " << kBackupsDir << std::endl;
		return false;
	}

	// 2. descargar apoyo.json del mismo repositorio que valido.json
	std::string jsonData;
	const std::string url = std::string(SWITCH_REPO) + "/apoyo.json";

	bool downloaded = false;
	const int maxAttempts = 5;
	for (int attempt = 1; attempt <= maxAttempts; attempt++)
	{
		jsonData.clear();
		if (downloadFileToMemory(url, &jsonData))
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
		return false;
	}

	// 3. parsear el JSON
	rapidjson::Document doc;
	doc.Parse(jsonData.c_str());

	if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("ids") || !doc["ids"].IsArray())
	{
		std::cout << "[Supporter] apoyo.json descargado pero con formato invalido" << std::endl;
		return false;
	}

	// 4. comparar cada id local (parcial, 7 caracteres) contra la lista de
	//    apoyo.json. Por compatibilidad, si en apoyo.json quedara guardado
	//    un id mas largo (ej. el formato viejo de 14 caracteres), solo se
	//    comparan sus ULTIMOS 7 caracteres — nunca se usan mas de 7.
	const rapidjson::Value& supporterIds = doc["ids"];
	for (rapidjson::SizeType i = 0; i < supporterIds.Size(); i++)
	{
		if (!supporterIds[i].IsString())
			continue;

		std::string supporterId = supporterIds[i].GetString();
		if (supporterId.size() > kCompareIdLength)
			supporterId = supporterId.substr(supporterId.size() - kCompareIdLength);

		for (const std::string& localId : localIds)
		{
			if (localId == supporterId)
			{
				std::cout << "[Supporter] Usuario beneficiario (id " << localId << ")" << std::endl;
				return true;
			}
		}
	}

	std::cout << "[Supporter] Ninguno de los " << localIds.size()
	          << " id(s) locales coincide con apoyo.json" << std::endl;
	return false;
}
