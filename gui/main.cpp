#if defined(SWITCH)
#include <switch.h>
#endif

#if defined(WII)
#include <stdlib.h>
#include <unistd.h>
#include <fat.h>
// Handles basic HW Init,
// Including Wiimote as Mouse
// And starting the Fat FS
#include "SDL2/SDL_main.h"
#include "../libs/chesto/src/DrawUtils.hpp"
#include <ogc/system.h>
#endif

#if defined(__WIIU__)
#include <unistd.h>

#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>

#include <sys/iosupport.h>
#include <unistd.h>
#endif

#include <sys/stat.h>
#include <ctime>
#include "../libs/chesto/libs/resinfs/include/romfs-wiiu.h"

#include "../libs/get/src/Get.hpp"
#include "../libs/get/src/Utils.hpp"

#include "ThemeManager.hpp"
#include "../gui/MainDisplay.hpp"
#include "SupporterBenefit.hpp"

#include "../console/Menu.hpp"

#include "main.hpp"

// ---------------------------------------------------------------------------
// Switch Scene validation - DESACTIVADA
// Se mantiene la variable para no romper otras partes del codigo que la usan,
// pero siempre retorna true. La unica validacion activa es la del hash.
// ---------------------------------------------------------------------------
bool gSwitchSceneValid = false;

static bool checkSwitchScene()
{
	return true; // validacion de carpeta desactivada
}
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Atmosphere hash validation
// 1. Descarga SWITCH_REPO/valido.json  →  lista de hashes permitidos
// 2. Calcula el SHA-256 de sdmc:/atmosphere/package3
// 3. Valida si el hash calculado aparece en la lista (basta uno)
//
// Formato esperado de valido.json:
//   { "hashes": [ "aabb...", "ccdd...", ... ] }
//
// Usa SHA-256 implementado internamente y rapidjson (ya en libs/get/src/libs).
// ---------------------------------------------------------------------------
#include "../libs/get/src/libs/rapidjson/include/rapidjson/document.h"
#include <cstring>
#include <cstdint>

bool gAtmosphereValid = false;

// ---------------------------------------------------------------------------
// Implementación SHA-256 sin dependencias externas
// ---------------------------------------------------------------------------
static const uint32_t SHA256_K[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

struct Sha256Ctx {
	uint32_t state[8];
	uint64_t count;
	uint8_t  buf[64];
};

static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(Sha256Ctx& ctx, const uint8_t* data)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	for (int i = 0; i < 16; i++)
		w[i] = ((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)|((uint32_t)data[i*4+2]<<8)|data[i*4+3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3);
		uint32_t s1 = rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);
		w[i] = w[i-16]+s0+w[i-7]+s1;
	}
	a=ctx.state[0]; b=ctx.state[1]; c=ctx.state[2]; d=ctx.state[3];
	e=ctx.state[4]; f=ctx.state[5]; g=ctx.state[6]; h=ctx.state[7];
	for (int i = 0; i < 64; i++) {
		t1 = h + (rotr32(e,6)^rotr32(e,11)^rotr32(e,25)) + ((e&f)^(~e&g)) + SHA256_K[i] + w[i];
		t2 = (rotr32(a,2)^rotr32(a,13)^rotr32(a,22)) + ((a&b)^(a&c)^(b&c));
		h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
	}
	ctx.state[0]+=a; ctx.state[1]+=b; ctx.state[2]+=c; ctx.state[3]+=d;
	ctx.state[4]+=e; ctx.state[5]+=f; ctx.state[6]+=g; ctx.state[7]+=h;
}

static void sha256_init(Sha256Ctx& ctx)
{
	ctx.count = 0;
	ctx.state[0]=0x6a09e667; ctx.state[1]=0xbb67ae85; ctx.state[2]=0x3c6ef372; ctx.state[3]=0xa54ff53a;
	ctx.state[4]=0x510e527f; ctx.state[5]=0x9b05688c; ctx.state[6]=0x1f83d9ab; ctx.state[7]=0x5be0cd19;
}

static void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len)
{
	size_t idx = ctx.count & 63;
	ctx.count += len;
	for (size_t i = 0; i < len; i++) {
		ctx.buf[idx++] = data[i];
		if (idx == 64) { sha256_transform(ctx, ctx.buf); idx = 0; }
	}
}

static void sha256_final(Sha256Ctx& ctx, uint8_t digest[32])
{
	size_t idx = ctx.count & 63;
	ctx.buf[idx++] = 0x80;
	if (idx > 56) { while (idx < 64) ctx.buf[idx++]=0; sha256_transform(ctx, ctx.buf); idx=0; }
	while (idx < 56) ctx.buf[idx++] = 0;
	uint64_t bits = ctx.count * 8;
	for (int i = 0; i < 8; i++) ctx.buf[56+i] = (bits >> (56-8*i)) & 0xff;
	sha256_transform(ctx, ctx.buf);
	for (int i = 0; i < 8; i++) {
		digest[i*4]   = (ctx.state[i]>>24)&0xff; digest[i*4+1] = (ctx.state[i]>>16)&0xff;
		digest[i*4+2] = (ctx.state[i]>> 8)&0xff; digest[i*4+3] =  ctx.state[i]     &0xff;
	}
}
// ---------------------------------------------------------------------------

// Calcula el SHA-256 de un archivo local y devuelve el hex string (64 chars).
// Devuelve cadena vacía si el archivo no existe o no se puede leer.
static std::string computeFileHash(const char* filePath)
{
	FILE* f = fopen(filePath, "rb");
	if (!f)
		return "";

	Sha256Ctx ctx;
	sha256_init(ctx);

	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		sha256_update(ctx, buf, n);

	fclose(f);

	uint8_t digest[32];
	sha256_final(ctx, digest);

	char hex[65];
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", digest[i]);
	hex[64] = '\0';

	return std::string(hex);
}

// Mensaje de depuracion visible en pantalla, indica exactamente en que paso
// fallo la validacion del hash, para diagnosticar sin herramientas externas
std::string gAtmosphereDebugMsg = "";

static bool checkAtmosphereHash()
{
	// 1. Calcular el hash del archivo local
	const std::string localHash = computeFileHash("sdmc:/atmosphere/package3");
	if (localHash.empty())
	{
		gAtmosphereDebugMsg = "package3 no encontrado en sdmc:/atmosphere/";
		std::cout << "[AtmHash] sdmc:/atmosphere/package3 no encontrado" << std::endl;
		return false;
	}
	std::cout << "[AtmHash] Hash local: " << localHash << std::endl;

	// 2. Descargar la lista de hashes válidos desde el repositorio
	// Se reintenta varias veces con espera entre intentos, porque al
	// arrancar la consola la conexion WiFi puede tardar unos segundos
	// en quedar lista despues de init_networking()
	//
	// raw.githubusercontent.com esta detras de un CDN (Fastly) que cachea
	// cada URL por varios minutos. Sin esto, tras editar valido.json en
	// GitHub la app podria seguir viendo la version vieja durante ese
	// tiempo. Se agrega un parametro con la hora actual para que cada
	// consulta sea una URL distinta y el CDN no devuelva algo cacheado.
	std::string jsonData;
	const std::string url = std::string(SWITCH_REPO) + "/valido.json?nocache=" + std::to_string((long long)time(nullptr));

	// Diagnostico extra: verificar si el certificado SSL realmente es
	// accesible desde el RomFS en este momento, usando fopen directo
	std::string cacertPath = std::string(RAMFS) + "res/cacert.pem";
	FILE* cacertTest = fopen(cacertPath.c_str(), "rb");
	std::string cacertStatus;
	if (cacertTest)
	{
		fseek(cacertTest, 0, SEEK_END);
		long cacertSize = ftell(cacertTest);
		fclose(cacertTest);
		cacertStatus = "OK (" + std::to_string(cacertSize) + " bytes) en " + cacertPath;
	}
	else
	{
		cacertStatus = "NO ACCESIBLE en " + cacertPath;
	}
	std::cout << "[AtmHash] Certificado SSL: " << cacertStatus << std::endl;

	bool downloaded = false;
	const int maxAttempts = 10;
	for (int attempt = 1; attempt <= maxAttempts; attempt++)
	{
		jsonData.clear();
		if (downloadFileToMemory(url, &jsonData))
		{
			downloaded = true;
			break;
		}
		std::cout << "[AtmHash] Intento " << attempt << "/" << maxAttempts
		          << " fallo (" << gLastCurlErrorMsg << "), reintentando..." << std::endl;

		// esperar antes de reintentar (1 segundo), dando tiempo a que la red
		// termine de inicializarse en segundo plano
#if defined(SWITCH)
		svcSleepThread(1'000'000'000ULL); // 1 segundo, en nanosegundos
#else
		usleep(1000 * 1000); // 1 segundo
#endif
	}

	if (!downloaded)
	{
		std::string curlDetail = gLastCurlErrorMsg.empty() ? "sin detalle" : gLastCurlErrorMsg;
		gAtmosphereDebugMsg = "No se pudo descargar valido.json tras " + std::to_string(maxAttempts) + " intentos. Error: " + curlDetail + ". Cert: " + cacertStatus;
		std::cout << "[AtmHash] No se pudo descargar valido.json. Ultimo error: " << curlDetail << std::endl;
		return false;
	}

	// 3. Parsear el JSON
	rapidjson::Document doc;
	doc.Parse(jsonData.c_str());

	if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("hashes") || !doc["hashes"].IsArray())
	{
		gAtmosphereDebugMsg = "valido.json descargado pero con formato invalido (" + std::to_string(jsonData.size()) + " bytes)";
		std::cout << "[AtmHash] valido.json con formato incorrecto" << std::endl;
		return false;
	}

	// 4. Comparar el hash local contra cada entrada de la lista
	const rapidjson::Value& hashes = doc["hashes"];
	for (rapidjson::SizeType i = 0; i < hashes.Size(); i++)
	{
		if (!hashes[i].IsString())
			continue;

		if (localHash == hashes[i].GetString())
		{
			gAtmosphereDebugMsg = "Hash valido (coincide con entrada " + std::to_string(i) + ")";
			std::cout << "[AtmHash] Hash válido encontrado (índice " << i << ")" << std::endl;
			return true;
		}
	}

	gAtmosphereDebugMsg = "Hash local (" + localHash.substr(0, 16) + "...) no coincide con " + std::to_string(hashes.Size()) + " hash(es) en valido.json";
	std::cout << "[AtmHash] Hash no encontrado en la lista de válidos" << std::endl;
	return false;
}
// ---------------------------------------------------------------------------

#if defined(__WIIU__)
void setPlatformPwd()
{
#define HBAS_PATH ROOT_PATH "wiiu/apps/appstore"

	// create and cd into the appstore directory
	mkpath(HBAS_PATH);
	chdir(HBAS_PATH);
}
#endif

#if defined(WII)
void setPlatformPwd()
{
#define HBAS_PATH ROOT_PATH "apps/appstore"

	// create and cd into the appstore directory
	mkpath(HBAS_PATH);
	chdir(HBAS_PATH);
}
#endif

int main(int argc, char* argv[])
{
	#ifdef WII
	SYS_STDIO_Report(true);
	#endif
#if defined(__WIIU__) || defined(WII)
	setPlatformPwd();
#endif
#if defined(SWITCH)
    chdir("sdmc:/switch/scene_eshop");

	// Servicios necesarios para la auto-instalacion de NSPs indicada por
	// repo.json ("instalacion"). Si alguno falla igual seguimos: simplemente
	// InstallNspIfRequested() fallara mas adelante y se mostrara el error
	// en el popup, sin tumbar el resto de la app.
	splInitialize();
	splCryptoInitialize();
	ncmInitialize();
	esInitialize();
	nsInitialize();
	nsextInitialize();
#endif

	// Inicializar el RomFS (resin:/) ANTES de cualquier cosa que necesite
	// leer archivos empaquetados (como res/cacert.pem para validar SSL).
	// Normalmente esto ocurre dentro del constructor de RootDisplay/MainDisplay,
	// pero checkAtmosphereHash() se ejecuta antes de crear MainDisplay,
	// asi que sin esto el filesystem "resin:/" todavia no existe.
#if defined(USE_RAMFS)
	ramfsInit();
#endif

	init_networking();
	setUserAgent("HBAS/" APP_VERSION " (" PLATFORM "; Chesto)");
	HBAS::ThemeManager::themeManagerInit();

	// Verificar si el PkUnico de Switch Scene está instalado
	gSwitchSceneValid = checkSwitchScene();

	// Verificar el hash de sd:atmosphere/package3
	gAtmosphereValid = checkAtmosphereHash();

	// Verificar si la consola es beneficiaria (apoyo/donacion), independiente
	// de las validaciones anteriores. Ver SupporterBenefit.hpp/.cpp
	gIsSupporter = checkSupporterStatus();

	bool cliMode = false;

#ifdef NOGUI
	cliMode = true;
#endif
	for (int x = 0; x < argc; x++)
		if (std::string("--recovery") == argv[x])
			cliMode = true;

	// initialize main title screen
	MainDisplay* display = new MainDisplay();
	display->canUseSelectToExit = true;

	auto events = display->events;

	for (int x = 0; x < 10; x++)
	{
		while (events->update())
		{
			// check if L or R is pushed during startup
			cliMode |= (events->held(L_BUTTON) || events->held(R_BUTTON));
		}

		// small delay for recovery mode input
		CST_Delay(16);
	}

	if (cliMode)
	{
		// if NOGUI variable defined, use the console's main method
		// TODO: process InputEvents outside of MainDisplay, which might have more requirements
		int console_main(RootDisplay*, InputEvents*);
		console_main(display, events);
	}
	else
	{
		display->setupMusic();

		#if defined(WII)
		// Wii uses a Hand Cursor by default, so force Arrow
		CST_SetCursor(CST_CURSOR_ARROW);
		#endif

		// start primary app
		display->mainLoop();
	}

	deinit_networking();

#if defined(SWITCH)
	nsextExit();
	nsExit();
	esExit();
	ncmExit();
	splCryptoExit();
	splExit();
#endif

	return 0;
}
