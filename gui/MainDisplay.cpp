#if defined(SWITCH)
#include <switch.h>
#endif
#if defined(WII)
#include <ogc/conf.h>
#endif
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include "../libs/get/src/Get.hpp"
#include "../libs/get/src/Utils.hpp"
#include "../libs/chesto/src/Constraint.hpp"

#include "MainDisplay.hpp"
#include "ThemeManager.hpp"
#include "ToolsScreen.hpp"
#include "ClockSync.hpp"
#include "main.hpp"
// gAtmosphereValid definido en main.cpp, declarado en main.hpp

using namespace std::string_literals; // for ""s

MainDisplay::MainDisplay()
	: RootDisplay(), appList(NULL, &sidebar)
{
	// add in the sidebar, footer, and main app listing
	sidebar.appList = &appList;

	super::append(&sidebar);
	super::append(&appList);

	// splash a pantalla completa mientras carga el repo.json y la lista de
	// componentes (se agrega al final del arbol, asi queda encima de todo;
	// el spinner se agrega despues y queda por delante). Si la imagen no
	// existe simplemente no hay splash.
#if !defined(_3DS) && !defined(_3DS_MOCK)
	// (fopen en vez de std::filesystem: "resin:/" es un sistema de archivos
	//  embebido y std::filesystem::exists puede devolver false ahi)
	FILE* splashProbe = fopen(RAMFS "res/splash.png", "rb");
	if (splashProbe)
	{
		fclose(splashProbe);
		splashImage = new ImageElement(RAMFS "res/splash.png");
		splashImage->resize(SCREEN_WIDTH, SCREEN_HEIGHT);
		splashImage->position(0, 0);
		super::append(splashImage);
	}
#endif

	needsRedraw = true;

	updateSidebarColor();

	#if defined(WII)
		if(CONF_GetAspectRatio() == CONF_ASPECT_16_9)
			setScreenResolution(854, 480);
	#endif
	// use HD resolution for hb-appstore
	// setScreenResolution(1920, 1080);
	// setScreenResolution(3840, 2160); // 4k
}

void MainDisplay::updateSidebarColor() {
	// set the background color (used as sidebar color)
	auto color = HBAS::ThemeManager::sidebarColor;
	backgroundColor = fromRGB(color.r, color.g, color.b);
}

void MainDisplay::setupMusic() {
	// initialize music (only if MUSIC defined)
	this->initMusic();

#ifdef MUSIC
	// load the music state from a config file
	this->startMusic();

	bool allowSound = getDefaultAudioStateForPlatform();

	if (std::filesystem::exists(SOUND_PATH)) {
		// invert our sound allowing setting, due to the existence of this file
		allowSound = !allowSound;
	}

	if (!allowSound) {
		// muted, so pause the music that we started earlier
		Mix_PauseMusic();
	}

	// load the sfx noise
	click_sfx = Mix_LoadWAV(RAMFS "res/click.wav");

#endif
}

bool MainDisplay::getDefaultAudioStateForPlatform() {
#ifdef __WIIU__
// default to true, only for wiiu
	return true;
#endif
	return false;
}

// plays an sfx interface-moving-noise, if sound isn't muted
void MainDisplay::playSFX()
{
#ifdef MUSIC
	if (this->music && !Mix_PausedMusic()) {
		Mix_PlayChannel( -1, this->click_sfx, 0 );
	}
#endif
}

MainDisplay::~MainDisplay()
{
	delete get;
	delete spinner;
	delete splashImage;
}

void MainDisplay::hideSplash() {
	if (spinner) {
		super::remove(spinner);
		delete spinner;
		spinner = nullptr;
	}
	if (splashImage) {
		super::remove(splashImage);
		delete splashImage;
		splashImage = nullptr;
	}
}

// splash a pantalla completa + spinner (mismo aspecto que la carga inicial)
void MainDisplay::showLoadingOverlay() {
#if !defined(_3DS) && !defined(_3DS_MOCK)
	if (!splashImage)
	{
		FILE* splashProbe = fopen(RAMFS "res/splash.png", "rb");
		if (splashProbe)
		{
			fclose(splashProbe);
			splashImage = new ImageElement(RAMFS "res/splash.png");
			splashImage->resize(SCREEN_WIDTH, SCREEN_HEIGHT);
			splashImage->position(0, 0);
			super::append(splashImage);
		}
	}
#endif
	if (!spinner)
	{
		auto spinnerPath = RAMFS "res/spinner.png";
#ifdef SWITCH
		spinnerPath = RAMFS "res/spinner_red.png";
#endif
		spinner = new ImageElement(spinnerPath);
		spinner->resize(90, 90);
		if (splashImage)
			spinner->constrain(ALIGN_TOP, SCREEN_HEIGHT * 26 / 100)->constrain(ALIGN_CENTER_HORIZONTAL, 0);
		else
			spinner->constrain(ALIGN_TOP, 90)->constrain(ALIGN_CENTER_HORIZONTAL, 0)->constrain(OFFSET_LEFT, 45);
		super::append(spinner);
	}
}

void MainDisplay::reloadList() {
	// solo desde la lista (no mientras hay otra pantalla o la carga inicial)
	if (reloading || RootDisplay::subscreen || showingSplash || get == nullptr)
		return;
	reloading = true;

	showLoadingOverlay();
	networking_callback = MainDisplay::updateLoader;
	updateLoader(nullptr, 0.0); // dibuja ya el primer cuadro

#if defined(SWITCH)
	// si la hora de la consola esta mal, la descarga fallaria
	ClockSync::syncSystemClock();
#endif

	// la lista actual solo se reemplaza si TODO llego bien; las tarjetas viejas se
	// sueltan justo antes del cambio (apuntan a los paquetes anteriores)
	bool ok = get->reload([this]() { appList.clearCards(); });

	networking_callback = nullptr;
	if (ok)
		appList.update(); // se arma mientras el splash sigue encima

	hideSplash();

	if (!ok)
	{
		showFullscreenPrompt(
			"No se pudo recargar",
			"No se pudo descargar la lista de componentes.\n"
			"Revisa tu conexion a internet e intentalo de nuevo.\n\n"
			"Se mantiene la lista actual.",
			false);
	}

	// descartar pulsaciones acumuladas durante la recarga (si no, un + repetido la lanzaria otra vez)
	{
		InputEvents flush;
		while (flush.update()) { /* descartar */ }
	}

	reloading = false;
}

void MainDisplay::beginInitialLoad() {
	networking_callback = nullptr;
	
	// set get instance to our applist; la lista completa se construye y se
	// dibuja una vez MIENTRAS el splash sigue encima, asi al quitarlo ya
	// esta todo listo (texturas de texto/iconos precargados)
	appList.get = get;

	// Precalentamiento: mientras el splash tapa todo, se construye una vez la
	// lista MAS PESADA ("Todos los aportes"). Asi los textos (nombre, version,
	// etc.) de todas las tarjetas quedan ya en la cache de texturas y, al
	// cambiar de seccion mas tarde, no hay tirón la primera vez.
	if (splashImage)
	{
		Sidebar* sb = appList.sidebar;
		const int startCat = sb->curCategory;
		for (int i = 0; i < TOTAL_CATS; i++)
		{
			if (std::string(sb->cat_value[i]) == "_all")
			{
				sb->curCategory = i;
				appList.update();
				RootDisplay::render(NULL);
				break;
			}
		}
		sb->curCategory = startCat;
	}

	appList.update();
	appList.sidebar->addHints();
	if (splashImage)
		RootDisplay::render(NULL);

	hideSplash();
}

bool MainDisplay::checkMetaRepoForUpdates(Get* get) {
	// download the metarepo (+1 network call)
	std::string data("");
	bool success = downloadFileToMemoryNoCache(META_REPO + "/index.json?nocache=" + std::to_string((long long)time(nullptr)), &data);

	if (!success) {
		// couldn't download the metarepo, so just return
		// TODO: surface some error notification to the user
		std::cout << "couldn't download metarepo" << std::endl;
		return false;
	}

	// parse the metarepo
	rapidjson::Document d;
	d.Parse(data.c_str());

	// check for parse success
	if (d.HasParseError()) {
		// couldn't parse metarepo
		std::cout << "couldn't parse metarepo" << std::endl;
		return false;
	}

	// the repos that we're interested in, which is based on our platform
	std::vector<std::string> platformsToCheck;
	// TODO: Use a RepoManager to get which platform types are enabled
#if defined(__WIIU__) || defined(PC)
	platformsToCheck.push_back("wiiu"); // TOOD: also use vwii, if enabled
#endif
#if defined(SWITCH) || defined(PC)
	platformsToCheck.push_back("switch");
#endif
#if defined(WII) || defined(WII_MOCK)
	platformsToCheck.push_back("wii"); // osc
#endif
#if defined(_3DS) || defined(_3DS_MOCK)
	platformsToCheck.push_back("3ds"); // uu
#endif

	// set of repos to remove (exclude)
	std::unordered_set<std::string> reposToRemove;

	// set of repos to add (include)
	std::unordered_map<std::string, std::string> reposToAdd;

	// grab the "suggestions" key
	if (d.HasMember("suggestions")) {
		// check the repo platforms that we're interested in
		for (auto& platform : platformsToCheck) {
			if (d["suggestions"].HasMember(platform.c_str())) {
				// operations for this platform
				auto& ops = d["suggestions"][platform.c_str()];

				// iterate through the operations
				for (auto& op : ops.GetArray()) {
					if (!op.HasMember("op")) continue;
					if (!op.HasMember("url")) continue;

					std::string opName = op["op"].GetString();
					std::string repoUrl = op["url"].GetString();

					if ("remove" == opName) {
						// remove this repo
						reposToRemove.insert(repoUrl);
					} else if ("add" == opName) {
						// check/get the type
						auto repoType = "get"; // default to get
						if (op.HasMember("type")) {
							repoType = op["type"].GetString();
						}
						// add this repo
						reposToAdd[repoUrl] = repoType;
					}
				}
			}
		}
	}

	get->addAndRemoveReposByURL(reposToAdd, reposToRemove);
	return true;
}

// ---------------------------------------------------------------------------
// checkSelfUpdate()
// Descarga META_REPO/version.json, compara la version con APP_VERSION y,
// si hay una version mas nueva, ofrece al usuario descargar la actualizacion.
//
// Formato esperado de version.json:
//   {
//     "version": "2.1.0",
//     "download_url": "https://...scene_eshop.nro"
//   }
//
// Retorna true si el usuario actualizo (para que el llamador pueda detener
// la carga normal si lo desea), false en cualquier otro caso.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// isVersionNewer()
// Compara dos versiones con formato "X.Y.Z" (numero de partes libre) de
// forma NUMERICA, componente por componente — NO como texto. Con una
// comparacion de texto simple, "10.0.0" se veria como "menor" que "9.0.0",
// y ademas cualquier version DISTINTA (mayor o menor) disparaba el aviso
// de actualizacion. Con esto, solo se considera "mas nueva" si realmente
// lo es numericamente.
//
// Devuelve true si `remote` es mayor que `local`.
// ---------------------------------------------------------------------------
static std::vector<int> parseVersionParts(const std::string& version)
{
	std::vector<int> parts;
	std::stringstream ss(version);
	std::string part;

	while (std::getline(ss, part, '.'))
	{
		// ignora un prefijo no numerico ("v2" -> 2); lo que sigue al numero ("0-beta") se ignora
		size_t start = 0;
		while (start < part.size() && !std::isdigit((unsigned char)part[start]))
			start++;
		try {
			parts.push_back(std::stoi(part.substr(start)));
		} catch (...) {
			parts.push_back(0); // parte no numerica, tratar como 0
		}
	}

	return parts;
}

static bool isVersionNewer(const std::string& remote, const std::string& local)
{
	std::vector<int> remoteParts = parseVersionParts(remote);
	std::vector<int> localParts = parseVersionParts(local);

	size_t maxParts = std::max(remoteParts.size(), localParts.size());
	for (size_t i = 0; i < maxParts; i++)
	{
		int r = (i < remoteParts.size()) ? remoteParts[i] : 0;
		int l = (i < localParts.size()) ? localParts[i] : 0;

		if (r != l)
			return r > l;
	}

	return false; // son iguales
}

bool MainDisplay::showFullscreenPrompt(const std::string& title, const std::string& message, bool withCancel)
{
	CST_Color white  = { 0xFF, 0xFF, 0xFF, 0xFF };
	CST_Color accent = { 0xFF, 0xD5, 0x4F, 0xFF }; // dorado, para que el titulo llame la atencion

	bool confirmed = false;
	bool responded = false;

	DimOverlay dim;
	// titulo grande y llamativo (bastante mas grande que el texto normal)
	TextElement titleText(title, 42 / SCALER, &accent);
	TextElement msgText(message, 20 / SCALER, &white, NORMAL, 700);

	titleText.update();
	msgText.update();

	titleText.position(0, SCREEN_HEIGHT / 2 - 140 / SCALER);
	msgText.position(0, SCREEN_HEIGHT / 2 - 50 / SCALER);

	titleText.constrain(ALIGN_CENTER_HORIZONTAL, 0);
	msgText.constrain(ALIGN_CENTER_HORIZONTAL, 0);

	super::append(&dim);
	super::append(&titleText);
	super::append(&msgText);

	// Botones con icono real del boton fisico (A/B), en vez de texto
	// "[A]"/"[B]" suelto -- Button ya sabe dibujar el icono correcto segun
	// la plataforma (ver Button::getControllerButtonImageForPlatform).
	Button acceptBtn("Aceptar", A_BUTTON, true, 24 / SCALER, 0);
	Button cancelBtn("Cancelar", B_BUTTON, true, 24 / SCALER, 0);

	// centrar horizontalmente: si hay dos botones, uno a cada lado del centro;
	// si es solo aviso, un unico boton centrado
	if (withCancel)
	{
		int gap = 20;
		int totalWidth = acceptBtn.width + cancelBtn.width + gap;
		int startX = SCREEN_WIDTH / 2 - totalWidth / 2;
		acceptBtn.position(startX, SCREEN_HEIGHT / 2 + 110 / SCALER);
		cancelBtn.position(startX + acceptBtn.width + gap, SCREEN_HEIGHT / 2 + 110 / SCALER);
		super::append(&cancelBtn);
	}
	else
	{
		acceptBtn.position(0, SCREEN_HEIGHT / 2 + 110 / SCALER);
		acceptBtn.constrain(ALIGN_CENTER_HORIZONTAL, 0);
	}
	super::append(&acceptBtn);

	// Conectamos los botones a su "action" -- Button::process() ya sabe
	// dispararla tanto por boton fisico (A/B, via event->held(physical))
	// como por touch/click (via Element::process() -> onTouchUp(), el
	// mismo mecanismo que usa el resto de la app, ver Sidebar/ThemeScreen).
	// Asi nos ahorramos reimplementar a mano la deteccion de touch, y los
	// dos modos de entrada quedan cubiertos por el mismo camino ya probado.
	bool wantAccept = false;
	bool wantCancel = false;
	acceptBtn.action = [&wantAccept]() { wantAccept = true; };
	if (withCancel)
		cancelBtn.action = [&wantCancel]() { wantCancel = true; };

	// Dibuja un frame completo del dialogo. Separado en un lambda porque
	// se usa tanto para el render inicial (antes del loop) como en cada
	// vuelta del loop de espera de input.
	auto renderPromptFrame = [&]() {
		if (RootDisplay::subscreen)
		{
			// El dialogo (dim/titleText/msgText/botones) es hijo directo
			// de MainDisplay, NO del subscreen -- por eso hay que
			// dibujarlo aparte, encima. Ademas no podemos usar
			// RootDisplay::mainDisplay->render(NULL) aca: esa funcion, si
			// hay un subscreen activo (como AppDetails, que todavia lo es
			// en este punto), dibuja SOLO el subscreen y hace return antes
			// de llegar a la parte que pinta estos hijos. Por eso lo
			// dibujamos a mano.
			RootDisplay::subscreen->render(RootDisplay::mainDisplay);
			dim.render(RootDisplay::mainDisplay);
			titleText.render(RootDisplay::mainDisplay);
			msgText.render(RootDisplay::mainDisplay);
			acceptBtn.render(RootDisplay::mainDisplay);
			if (withCancel)
				cancelBtn.render(RootDisplay::mainDisplay);
		}
		else
		{
			// Sin subscreen activo (ej. el aviso de actualizacion al
			// inicio, antes de que haya subscreen): el dialogo YA es
			// hijo de MainDisplay (lo agregamos arriba con
			// super::append), asi que un solo render "de hijos" de
			// MainDisplay dibuja todo junto -- Sidebar/AppList de fondo,
			// y el dialogo encima. Antes esto usaba solo
			// renderBackground(true) (un relleno plano sin Sidebar ni
			// AppList detras), por lo que el fondo semitransparente no
			// tenia nada real que dejar ver y se veia como un negro
			// solido/opaco en vez de semitransparente.
			((Element*)RootDisplay::mainDisplay)->Element::render(NULL);
		}
		RootDisplay::mainDisplay->update();
	};

	// Render inicial antes de esperar input: recalcPosition() (que fija
	// xAbs/yAbs, usados por el touch hit-test) corre dentro de render(),
	// asi que sin este primer pase los botones tendrian coordenadas sin
	// calcular en el primerisimo frame.
	renderPromptFrame();

	// Vaciar cualquier evento de SDL que haya quedado en cola durante la
	// descarga/extraccion/instalacion (que corren sin procesar inputs; si
	// tardaron varios segundos es facil que se acumulen botones sueltos,
	// ruido del stick analogico, etc). Sin este flush, el primer boton
	// real que aprieta el usuario en este dialogo compite contra ese
	// backlog viejo, dando la sensacion de que hay que apretar A un par
	// de veces para que registre.
	{
		InputEvents flush;
		while (flush.update()) { /* descartar */ }
	}

	while (!responded)
	{
		InputEvents* events = new InputEvents();
		while (events->update())
		{
			// deja que Button::process maneje boton fisico + touch
			acceptBtn.process(events);
			if (withCancel)
				cancelBtn.process(events);

			// en un aviso informativo (sin cancelar), B tambien confirma,
			// igual que A
			if (!withCancel && events->pressed(B_BUTTON))
				wantAccept = true;

			if (wantAccept)
			{
				confirmed = true;
				responded = true;
				break;
			}
			if (withCancel && (wantCancel || events->pressed(B_BUTTON)))
			{
				confirmed = false;
				responded = true;
				break;
			}
		}

		renderPromptFrame();

		delete events;
		CST_Delay(16);
	}

	super::remove(&dim);
	super::remove(&titleText);
	super::remove(&msgText);
	super::remove(&acceptBtn);
	if (withCancel)
		super::remove(&cancelBtn);

	return confirmed;
}

// Ruta del .nro que se esta ejecutando (argv[0]). Si no se puede saber con seguridad,
// la ruta de siempre. Asi la actualizacion reemplaza el archivo REAL del usuario aunque
// lo tenga en otra carpeta o con otro nombre, en vez de crear una copia en otro lado.
static std::string selfNroPath()
{
	const std::string& p = gRunningNroPath;
	if (p.size() > 10 && p.rfind("sdmc:/", 0) == 0 && p.compare(p.size() - 4, 4, ".nro") == 0)
	{
		struct stat st = {};
		if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode))
			return p;
	}
	return APP_NRO_PATH;
}

bool MainDisplay::checkSelfUpdate()
{
#if !defined(SWITCH)
	// Self-update solo aplica en Switch por ahora
	return false;
#endif

	// sin cache (el CDN puede tener una version.json vieja varios minutos)
	std::string data;
	bool success = downloadFileToMemoryNoCache(SELF_UPDATE_URL + "?nocache=" + std::to_string((long long)time(nullptr)), &data);
	if (!success) {
		std::cout << "[self-update] no se pudo descargar version.json" << std::endl;
		return false;
	}

	rapidjson::Document d;
	d.Parse(data.c_str());
	if (d.HasParseError() || !d.IsObject() ||
	    !d.HasMember("version") || !d["version"].IsString() ||
	    !d.HasMember("download_url") || !d["download_url"].IsString()) {
		std::cout << "[self-update] version.json invalido" << std::endl;
		return false;
	}

	std::string remoteVersion = d["version"].GetString();
	std::string downloadUrl   = d["download_url"].GetString();

	// Solo se considera actualizacion disponible si la version remota es
	// NUMERICAMENTE mayor a la actual (no simplemente "distinta").
	if (!isVersionNewer(remoteVersion, APP_VERSION)) {
		std::cout << "[self-update] ya en la version mas reciente (" << APP_VERSION << ")" << std::endl;
		return false;
	}

	std::cout << "[self-update] nueva version disponible: " << remoteVersion
	          << " (actual: " << APP_VERSION << ")" << std::endl;

	// archivo real que se va a reemplazar, y los auxiliares junto a el
	const std::string nroPath = selfNroPath();
	const std::string tmpPath = nroPath + ".tmp";
	const std::string oldPath = nroPath + ".old";

	// --- Dialogo de confirmacion ---
	std::string msg =
		"Nueva version disponible: v" + remoteVersion + "\n"
		"Version actual:           v" + std::string(APP_VERSION) + "\n\n"
		"Se descargara y reemplazara el .nro en la SD.\n"
		"Deberas reiniciar la app para aplicar la actualizacion.";

	bool userConfirmed = showFullscreenPrompt("Actualizacion disponible", msg, true);
	if (!userConfirmed)
		return false;

	// --- Descarga a archivo temporal ---
	std::cout << "[self-update] descargando " << downloadUrl << " -> " << tmpPath << std::endl;

	// la carpeta del .nro tiene que existir (si el usuario lo mueve o la borra, la descarga fallaria)
	mkpath(dir_name(tmpPath));

	CST_Color white = { 0xFF, 0xFF, 0xFF, 0xFF };

	DimOverlay downloadDim;
	TextElement downloadStatusText("Descargando actualizacion...", 30 / SCALER, &white);
	TextElement downloadPercentText("0%", 24 / SCALER, &white);
	ProgressBar downloadBar;
	downloadBar.width = 450;
	downloadBar.percent = 0;
	downloadBar.dimBg = false; // el dim ya lo hace downloadDim

	downloadStatusText.update();
	downloadPercentText.update();

	downloadBar.position(SCREEN_WIDTH / 2 - downloadBar.width / 2, SCREEN_HEIGHT / 2);
	downloadStatusText.position(0, SCREEN_HEIGHT / 2 - 70 / SCALER);
	downloadPercentText.position(0, SCREEN_HEIGHT / 2 + 40 / SCALER);

	downloadStatusText.constrain(ALIGN_CENTER_HORIZONTAL, 0);
	downloadPercentText.constrain(ALIGN_CENTER_HORIZONTAL, 0);

	super::append(&downloadDim);
	super::append(&downloadStatusText);
	super::append(&downloadBar);
	super::append(&downloadPercentText);
	RootDisplay::mainDisplay->render(NULL);

	this->selfUpdateProgressBar = &downloadBar;
	this->selfUpdatePercentText = &downloadPercentText;
	networking_callback = MainDisplay::updateSelfUpdateProgress;
	networking_callback_data = nullptr;

	// un .tmp viejo (de un intento anterior que se corto) no debe mezclarse con esta descarga
	std::remove(tmpPath.c_str());
	bool dlOk = downloadFileToDisk(downloadUrl, tmpPath);

	networking_callback = nullptr;
	networking_callback_data = nullptr;
	this->selfUpdateProgressBar = nullptr;
	this->selfUpdatePercentText = nullptr;

	super::remove(&downloadDim);
	super::remove(&downloadStatusText);
	super::remove(&downloadBar);
	super::remove(&downloadPercentText);

	if (!dlOk) {
		std::cout << "[self-update] fallo la descarga" << std::endl;
		// Limpiar el .tmp si quedo a medias
		std::remove(tmpPath.c_str());

		showFullscreenPrompt(
			"Error de descarga",
			"No se pudo descargar la actualizacion.\n"
			"Verifica tu conexion a internet e intenta de nuevo.",
			false
		);
		return false;
	}

	// --- Validar el .tmp antes de reemplazar el .nro ---
	// downloadFileToDisk solo verifica que curl no dio error de red,
	// pero NO verifica el codigo HTTP. Si el archivo no existe en el servidor,
	// GitHub devuelve una pagina 404 HTML con CURLE_OK.
	// Hay que verificar que lo descargado es realmente un NRO valido antes
	// de reemplazar el ejecutable actual.

	// Verificacion 1: el .tmp existe y tiene un tamaño minimo razonable.
	// Un .nro funcional no puede ser menor a ~64KB (cabecera + codigo minimo).
	// Una pagina de error 404 de GitHub suele ser ~10KB.
	struct stat tmpStat = {};
	bool tmpExists = (stat(tmpPath.c_str(), &tmpStat) == 0);
	if (!tmpExists || tmpStat.st_size < 65536) {
		std::cout << "[self-update] .tmp demasiado pequeno ("
		          << (tmpExists ? tmpStat.st_size : 0)
		          << " bytes) - posible 404" << std::endl;
		std::remove(tmpPath.c_str());

		showFullscreenPrompt(
			"Archivo invalido",
			"El archivo descargado no es valido\n"
			"(posiblemente el servidor devolvio un error).\n\n"
			"Tu instalacion actual no fue modificada.",
			false
		);
		return false;
	}

	// Verificacion 2: los bytes en offset 0x10 deben ser el magic "NRO0".
	// Formato NRO de Switch (switchbrew.org/wiki/NRO):
	//   offset 0x00: MOD0 offset (4 bytes)
	//   offset 0x04: padding (4 bytes)
	//   offset 0x08: padding (4 bytes)
	//   offset 0x0C: padding (4 bytes)
	//   offset 0x10: magic "NRO0" (4 bytes) <-- aqui verificamos
	{
		FILE* tmpFile = fopen(tmpPath.c_str(), "rb");
		bool validNro = false;
		if (tmpFile) {
			uint8_t header[0x14] = {};
			if (fread(header, 1, sizeof(header), tmpFile) == sizeof(header)) {
				// magic NRO0 = 0x4E 0x52 0x4F 0x30
				validNro = (header[0x10] == 0x4E &&
				            header[0x11] == 0x52 &&
				            header[0x12] == 0x4F &&
				            header[0x13] == 0x30);
			}
			fclose(tmpFile);
		}

		if (!validNro) {
			std::cout << "[self-update] magic NRO0 no encontrado en .tmp - archivo corrupto o 404" << std::endl;
			std::remove(tmpPath.c_str());

			showFullscreenPrompt(
				"Archivo corrupto",
				"El archivo descargado no es un NRO valido.\n"
				"Puede que el link de descarga este desactualizado\n"
				"o que la descarga se haya interrumpido.\n\n"
				"Tu instalacion actual no fue modificada.",
				false
			);
			return false;
		}
	}

	// Verificacion 3: si el servidor informa el tamaño, el archivo debe tenerlo completo
	// (una descarga cortada a medias seguiria teniendo un encabezado NRO valido)
	if (d.HasMember("size") && d["size"].IsUint64() && d["size"].GetUint64() > 0 &&
	    (uint64_t)tmpStat.st_size != d["size"].GetUint64()) {
		std::cout << "[self-update] tamaño distinto al esperado: " << tmpStat.st_size
		          << " vs " << d["size"].GetUint64() << std::endl;
		std::remove(tmpPath.c_str());

		showFullscreenPrompt(
			"Descarga incompleta",
			"El archivo descargado esta incompleto.\n"
			"Intenta de nuevo.\n\n"
			"Tu instalacion actual no fue modificada.",
			false
		);
		return false;
	}

	std::cout << "[self-update] .tmp validado correctamente ("
	          << tmpStat.st_size << " bytes, magic NRO0 OK)" << std::endl;

	// --- Reemplazar el .nro ---
	// En exFAT (Switch), rename() falla si el destino ya existe, asi que el .nro actual
	// primero se aparta (.old) y SOLO si el nuevo queda en su lugar se borra el viejo.
	// (Antes se borraba el actual primero: si el rename fallaba despues, el usuario se
	// quedaba sin la aplicacion.) Es seguro tocarlo porque nx-hbloader ya lo cargo en RAM.
	std::remove(oldPath.c_str());
	bool movedAside = (std::rename(nroPath.c_str(), oldPath.c_str()) == 0);
	if (!movedAside) {
		// plan B (como antes): borrar el actual
		std::cout << "[self-update] no se pudo apartar el .nro actual: " << strerror(errno) << std::endl;
		std::remove(nroPath.c_str());
	}

	if (std::rename(tmpPath.c_str(), nroPath.c_str()) != 0) {
		const int renameErrno = errno;
		std::cout << "[self-update] fallo el rename de .tmp a .nro: " << strerror(renameErrno) << std::endl;

		// volver a dejar el .nro de antes en su sitio
		const bool restored = movedAside && (std::rename(oldPath.c_str(), nroPath.c_str()) == 0);

		std::string renameErr;
		if (restored) {
			std::remove(tmpPath.c_str());
			renameErr =
				std::string("No se pudo reemplazar el archivo .nro.\n") +
				"Error: " + strerror(renameErrno) + "\n\n" +
				"Ruta destino: " + nroPath + "\n\n" +
				"Tu instalacion actual no fue modificada.";
		} else {
			// no se pudo restaurar: el archivo nuevo (ya validado) se deja por si hay que ponerlo a mano
			renameErr =
				std::string("No se pudo reemplazar el archivo .nro.\n") +
				"Error: " + strerror(renameErrno) + "\n\n" +
				"La nueva version quedo guardada como:\n" + tmpPath + "\n" +
				"Renombrala a:\n" + nroPath;
		}
		showFullscreenPrompt("Error al aplicar update", renameErr, false);
		return false;
	}

	std::remove(oldPath.c_str()); // el nuevo ya esta en su lugar

	std::cout << "[self-update] actualizacion aplicada correctamente" << std::endl;

	// --- Dialogo final ---
	showFullscreenPrompt(
		"Actualizacion completada",
		"v" + remoteVersion + " instalada correctamente.\n\n"
		"Cierra la aplicacion y vuelve a abrirla\n"
		"para usar la nueva version.",
		false
	);

	return true; // indica al llamador que hubo actualizacion
}

void MainDisplay::render(Element* parent)
{
	if (showingSplash)
		renderedSplash = true;

	// Refrescar el color de fondo segun el tema actualmente activo,
	// para que un cambio de tema se vea reflejado de inmediato
	updateSidebarColor();

	renderBackground(true);

	// NOTA: la validacion del hash de package3 ya no bloquea la aplicacion
	// completa en el splash inicial. Su alcance ahora es por categoria
	// (ver ProtectedCategories.hpp) y se avisa al usuario unicamente cuando
	// intenta descargar un paquete de una categoria protegida sin haber
	// pasado la validacion (ver AppDetails.cpp).

	RootDisplay::render(parent);
}

bool MainDisplay::process(InputEvents* event)
{
	// Mientras se muestra el splash, bloquear TODOS los inputs
	// para que ningun boton cause error durante la carga inicial
	if (!RootDisplay::subscreen && showingSplash && renderedSplash)
	{
		if (!event->noop)
			return true; // consumir el evento sin procesarlo

		showingSplash = false;

		// initial loading spinner
		auto spinnerPath = RAMFS "res/spinner.png";
#ifdef SWITCH
		// switch gets a red spinner
		spinnerPath = RAMFS "res/spinner_red.png";
#endif

		if (isEarthDay()) {
			backgroundColor = fromRGB(12, 156, 91);
			spinnerPath = RAMFS "res/spinner_green.png";
		}

		spinner = new ImageElement(spinnerPath);
		spinner->resize(90, 90);
		if (splashImage) // entre "CARGANDO" y el logo de la imagen
			spinner->constrain(ALIGN_TOP, SCREEN_HEIGHT * 26 / 100)->constrain(ALIGN_CENTER_HORIZONTAL, 0);
		else
			spinner->constrain(ALIGN_TOP, 90)->constrain(ALIGN_CENTER_HORIZONTAL, 0)->constrain(OFFSET_LEFT, 45);
		super::append(spinner);

#if defined(_3DS) || defined(_3DS_MOCK)
		spinner->resize(40, 40);
		spinner->position(SCREEN_WIDTH / 2 - spinner->width / 2, 70);
#endif

		networking_callback = MainDisplay::updateLoader;

		// ANTES de cualquier descarga: si la hora de la consola esta mal, HTTPS falla
		// (certificados) y no cargaria ni la lista ni las actualizaciones
#if defined(SWITCH)
		ClockSync::syncSystemClock();
#endif

		// fetch repositories metadata
#if defined(WII)
		// default the repo type to OSC for wii
		get = new Get(DEFAULT_GET_HOME, DEFAULT_REPO, false, "osc");
#else
		get = new Get(DEFAULT_GET_HOME, DEFAULT_REPO, false);
#endif

		// update active repos according to the metarepo
		bool isOnline = checkMetaRepoForUpdates(get);

		// comprobar si hay una version nueva del propio HbScene
		// (se hace antes de cargar paquetes para que el usuario pueda
		//  reiniciar limpiamente si acepta la actualizacion)
		checkSelfUpdate();

		// actually download the repos
		get->update();

		// go through all repos and if one has an error, set the error flag
		for (auto repo : get->getRepos())
		{
			error = error || !repo->isLoaded();
			atLeastOneEnabled = atLeastOneEnabled || repo->isEnabled();
		}

		if (!isOnline)
		{
			std::string connTestMsg = replaceAll(i18n("errors.conntest"), "PLATFORM", PLATFORM);
			hideSplash();
			RootDisplay::switchSubscreen(new ErrorScreen(i18n("errors.nowifi"), connTestMsg + "\n" + i18n("errors.dnsmsg")));
			return true;
		}

		if (!atLeastOneEnabled)
		{
			hideSplash();
			RootDisplay::switchSubscreen(new ErrorScreen(i18n("errors.noserver"), i18n("errors.norepos") + "\n" + i18n("errors.onepkg")));
			return true;
		}

		// sd card write test, try to open a file on the sd root
		std::string tmp_dir = get->mTmp_path;
		std::string tmp_file = tmp_dir + "write_test.txt";

		bool writeFailed = false;
		std::string magic = "Whosoever holds this hammer, if they be worthy, shall possess the power of Thor.";

		// try to write to the file (no append)
		std::ofstream file(tmp_file);
		if (file.is_open()) {
			file << magic;
			file.close();
		}
		else writeFailed = true;
		
		// try to read from the file
		std::ifstream read_file(tmp_file);
		if (!writeFailed && read_file.is_open()) 
		{
			std::string line;
			std::getline(read_file, line);
			read_file.close();

			if (line != magic) writeFailed = true;

			// delete the file
			std::remove(tmp_file.c_str());
		}
		else writeFailed = true;

		if (writeFailed) {
			std::string cardText = replaceAll(i18n("errors.writetestfail"), "PATH", tmp_file) + "\n";
	#if defined(__WIIU__)
			cardText = i18n("errors.sdlock") + "\n"s + cardText;
	#elif defined (SWITCH)
			cardText = i18n("errors.exfat") + "\n"s + cardText;
	#endif

			hideSplash();
			RootDisplay::switchSubscreen(new ErrorScreen(i18n("errors.sdaccess"), cardText));
			return true;
		}

		beginInitialLoad();

		return true;
	}

	// if we need a redraw, also update the app list (for resizing events)
	// TODO: have a more generalized way to have a view describe what needs redrawing
	if (needsRedraw)
		appList.update();

	// Abrir el menu de Herramientas (Temas / Creditos / Limpieza de cache)
	// con el boton R, solo si no hay otro subscreen activo (por ejemplo,
	// no debe abrirse mientras se esta viendo AppDetails)
	if (!RootDisplay::subscreen && event->pressed(R_BUTTON))
	{
		RootDisplay::switchSubscreen(new ToolsScreen(this->get));
		return true;
	}

	return RootDisplay::process(event) || true;
}

int MainDisplay::updateLoader(void* clientp, double dlnow)
{
	(void)clientp;
	int now = CST_GetTicks();
	int diff = now - AppDetails::lastFrameTime;

	double amount = dlnow;

	// don't update the GUI too frequently here, it slows down downloading
	// (never return early if it's 100% done)
	if (diff < 32 && amount != 1)
		return 0;

	MainDisplay* display = (MainDisplay*)RootDisplay::mainDisplay;
	if (display->spinner)
		display->spinner->angle += 10;
	display->render(NULL);

	AppDetails::lastFrameTime = CST_GetTicks();

	return 0;
}

int MainDisplay::updateSelfUpdateProgress(void* clientp, double dlnow)
{
	(void)clientp;
	int now = CST_GetTicks();
	int diff = now - AppDetails::lastFrameTime;

	double amount = dlnow;
	if (amount < 0) amount = 0;
	if (amount > 1) amount = 1;

	if (diff < 32 && amount != 1)
		return 0;

#if defined(SWITCH)
	appletReportUserIsActive();
#endif

	MainDisplay* display = (MainDisplay*)RootDisplay::mainDisplay;
	if (display->selfUpdateProgressBar)
		display->selfUpdateProgressBar->percent = amount;

	if (display->selfUpdatePercentText)
	{
		int percentInt = (int)(amount * 100);
		display->selfUpdatePercentText->setText(std::to_string(percentInt) + "%");
		display->selfUpdatePercentText->update();
		display->selfUpdatePercentText->constrain(ALIGN_CENTER_HORIZONTAL, 0);
	}

	display->render(NULL);

	AppDetails::lastFrameTime = CST_GetTicks();

	return 0;
}


ErrorScreen::ErrorScreen(std::string mainErrorText, std::string troubleshootingText)
	: icon(LOGO_PATH)
	, title(i18n("credits.title"), 50 - 25, &HBAS::ThemeManager::textPrimary)
	, errorMessage(mainErrorText.c_str(), 40, &HBAS::ThemeManager::textPrimary)
	, troubleshooting((std::string(i18n("errors.troubleshooting") + "\n") + troubleshootingText).c_str(), 20, &HBAS::ThemeManager::textSecondary, false, 600)
	, btnQuit(i18n("listing.quit"), SELECT_BUTTON, false, 15)
{
	Container* logoCon = new Container(ROW_LAYOUT, 10);
	icon.resize(35, 35);
	logoCon->add(&icon);
	logoCon->add(&title);

#if defined(USE_OSC_BRANDING)
	// make the icon larger
	title.setText("HBAS + OSC Wii");
	title.update();
	icon.setScaleMode(SCALE_PROPORTIONAL_NO_BG);
	icon.resize(80, 80);
#endif

	// constraints
	logoCon->constrain(ALIGN_TOP | ALIGN_CENTER_HORIZONTAL, 25);
	errorMessage.constrain(ALIGN_CENTER_BOTH);
	troubleshooting.constrain(ALIGN_BOTTOM | ALIGN_CENTER_HORIZONTAL, 40);
	btnQuit.constrain(ALIGN_LEFT | ALIGN_BOTTOM, 100);

	super::append((new Button(i18n("errors.ignorethis"), X_BUTTON, false, 15))
		->constrain(ALIGN_RIGHT | ALIGN_BOTTOM, 100)
		->setAction([]() {
			auto mainDisplay = (MainDisplay*)RootDisplay::mainDisplay;
			mainDisplay->get->addLocalRepo();
			mainDisplay->needsRedraw = true;
			mainDisplay->beginInitialLoad();
			RootDisplay::switchSubscreen(nullptr);
	}));

	btnQuit.action = []() {
		RootDisplay::mainDisplay->requestQuit();
	};

	super::append(logoCon);
	super::append(&errorMessage);
	super::append(&troubleshooting);
	super::append(&btnQuit);
}

bool isEarthDay() {
	time_t now = time(0);
	tm* ltm = localtime(&now);

	return ltm->tm_mon == 3 && ltm->tm_mday == 22;
}
