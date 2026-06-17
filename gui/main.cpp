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

#include "../libs/get/src/Get.hpp"
#include "../libs/get/src/Utils.hpp"

#include "ThemeManager.hpp"
#include "../gui/MainDisplay.hpp"

#include "../console/Menu.hpp"

#include "main.hpp"

// ---------------------------------------------------------------------------
// Switch Scene validation
// Verifica si el usuario tiene instalado el PkUnico de Switch Scene
// comprobando la existencia de: sdmc:/config/switch-scene/Overlays
// ---------------------------------------------------------------------------
bool gSwitchSceneValid = false;

static bool checkSwitchScene()
{
	struct stat st;
	return (stat("sdmc:/config/switch-scene/Overlays", &st) == 0
	        && S_ISDIR(st.st_mode));
}
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Atmosphere hash validation
// Verifica que sd:atmosphere/package3 exista y que su SHA-256 coincida
// con el hash permitido. Usa mbedtls, disponible en el SDK de libnx.
// ---------------------------------------------------------------------------
#include <mbedtls/sha256.h>

bool gAtmosphereValid = false;

static bool checkAtmosphereHash()
{
	// Hash SHA-256 permitido (en minúsculas, sin espacios)
	static const char ALLOWED_HASH[] =
	    "0151f0e4d0a01077d7f7e08079a854b2ee516bff897d8eb0a8fed6bf1e645c73";

	const char* FILE_PATH = "sdmc:/atmosphere/package3";

	// Abrir el archivo
	FILE* f = fopen(FILE_PATH, "rb");
	if (!f)
		return false;

	// Calcular SHA-256 en bloques para no cargar todo en RAM
	mbedtls_sha256_context ctx;
	mbedtls_sha256_init(&ctx);
	mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (no SHA-224)

	unsigned char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		mbedtls_sha256_update(&ctx, buf, n);

	fclose(f);

	unsigned char digest[32];
	mbedtls_sha256_finish(&ctx, digest);
	mbedtls_sha256_free(&ctx);

	// Convertir digest a hex string y comparar
	char hexDigest[65];
	for (int i = 0; i < 32; i++)
		snprintf(hexDigest + i * 2, 3, "%02x", digest[i]);
	hexDigest[64] = '\0';

	return (strcmp(hexDigest, ALLOWED_HASH) == 0);
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
#endif
	init_networking();
	setUserAgent("HBAS/" APP_VERSION " (" PLATFORM "; Chesto)");
	HBAS::ThemeManager::themeManagerInit();

	// Verificar si el PkUnico de Switch Scene está instalado
	gSwitchSceneValid = checkSwitchScene();

	// Verificar el hash de sd:atmosphere/package3
	gAtmosphereValid = checkAtmosphereHash();

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

	return 0;
}
