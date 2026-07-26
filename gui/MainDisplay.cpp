#if defined(SWITCH)
#include <switch.h>
#endif
#if defined(WII)
#include <ogc/conf.h>
#endif
#include <cstdio>
#include <cstdint>
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
#include "ThemeScreen.hpp"
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
}

void MainDisplay::beginInitialLoad() {
	networking_callback = nullptr;
	
	if (spinner) {
		// remove spinner
		super::remove(spinner);
		delete spinner;
		spinner = nullptr;
	}

	// set get instance to our applist
	appList.get = get;
	appList.update();
	appList.sidebar->addHints();
}

bool MainDisplay::checkMetaRepoForUpdates(Get* get) {
	// download the metarepo (+1 network call)
	std::string data("");
	bool success = downloadFileToMemory(META_REPO + "/index.json", &data);

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
		try {
			parts.push_back(std::stoi(part));
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

bool MainDisplay::checkSelfUpdate()
{
#if !defined(SWITCH)
	// Self-update solo aplica en Switch por ahora
	return false;
#endif

	std::string data;
	bool success = downloadFileToMemory(SELF_UPDATE_URL, &data);
	if (!success) {
		std::cout << "[self-update] no se pudo descargar version.json" << std::endl;
		return false;
	}

	rapidjson::Document d;
	d.Parse(data.c_str());
	if (d.HasParseError() || !d.HasMember("version") || !d.HasMember("download_url")) {
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

	// --- Dialogo de confirmacion ---
	// Usamos una variable de sincronizacion simple porque AlertDialog
	// funciona de forma asincrona (callbacks). Bloqueamos el loop
	// de eventos con una flag hasta que el usuario responda.
	bool userConfirmed = false;
	bool userResponded = false;

	std::string msg =
		"Nueva version disponible: v" + remoteVersion + "\n"
		"Version actual:           v" + std::string(APP_VERSION) + "\n\n"
		"Se descargara y reemplazara el .nro en la SD.\n"
		"Deberas reiniciar la app para aplicar la actualizacion.\n\n"
		"[A] Actualizar    [B] Cancelar";

	auto* dlg = new AlertDialog("Actualizacion disponible", msg);
	dlg->onConfirm = [&]() {
		userConfirmed = true;
		userResponded = true;
		dlg->hidden = true;
	};
	dlg->onCancel = [&]() {
		userConfirmed = false;
		userResponded = true;
		dlg->hidden = true;
	};
	super::append(dlg);
	dlg->show();

	// Mini event-loop hasta que el usuario responda.
	// NO llamamos dlg->process(events) porque chesto intenta navegar con
	// botones fisicos dentro del dialogo y produce un crash.
	// Manejamos A y B manualmente aqui, y el touch funciona via render+SDL.
	while (!userResponded) {
		InputEvents* events = new InputEvents();
		while (events->update()) {
			if (events->pressed(A_BUTTON)) {
				userConfirmed = true;
				userResponded = true;
				dlg->hidden = true;
				break;
			}
			if (events->pressed(B_BUTTON)) {
				userConfirmed = false;
				userResponded = true;
				dlg->hidden = true;
				break;
			}
			// touch: dejar que el dialogo lo maneje (no crashea con touch)
			if (events->isTouch()) {
				dlg->process(events);
			}
		}
		RootDisplay::mainDisplay->render(NULL);
		delete events;
		CST_Delay(16);
	}

	super::remove(dlg);
	delete dlg;

	if (!userConfirmed)
		return false;

	// --- Descarga a archivo temporal ---
	std::cout << "[self-update] descargando " << downloadUrl << " -> " << APP_NRO_TMP << std::endl;

	// Mostrar mensaje de progreso (texto simple sobre pantalla)
	auto* progressMsg = new TextElement(
		"Descargando actualizacion, por favor espera...",
		24, nullptr, NORMAL, 700
	);
	progressMsg->constrain(ALIGN_CENTER_BOTH);
	super::append(progressMsg);
	RootDisplay::mainDisplay->render(NULL);

	bool dlOk = downloadFileToDisk(downloadUrl, APP_NRO_TMP);

	super::remove(progressMsg);
	delete progressMsg;

	if (!dlOk) {
		std::cout << "[self-update] fallo la descarga" << std::endl;
		// Limpiar el .tmp si quedo a medias
		std::remove(APP_NRO_TMP);

		bool errClosed = false;
		auto* errDlg = new AlertDialog(
			"Error de descarga",
			"No se pudo descargar la actualizacion.\n"
			"Verifica tu conexion a internet e intenta de nuevo."
		);
		errDlg->onConfirm = [&]() { errClosed = true; errDlg->hidden = true; };
		super::append(errDlg);
		errDlg->show();
		while (!errClosed) {
			InputEvents* ev = new InputEvents();
			while (ev->update()) {
				if (ev->pressed(A_BUTTON) || ev->pressed(B_BUTTON)) { errClosed = true; errDlg->hidden = true; break; }
				errDlg->process(ev);
			}
			RootDisplay::mainDisplay->render(NULL);
			delete ev;
			CST_Delay(16);
		}
		super::remove(errDlg);
		delete errDlg;
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
	bool tmpExists = (stat(APP_NRO_TMP, &tmpStat) == 0);
	if (!tmpExists || tmpStat.st_size < 65536) {
		std::cout << "[self-update] .tmp demasiado pequeno (" 
		          << (tmpExists ? tmpStat.st_size : 0) 
		          << " bytes) - posible 404" << std::endl;
		std::remove(APP_NRO_TMP);

		bool errClosed = false;
		auto* errDlg = new AlertDialog(
			"Archivo invalido",
			"El archivo descargado no es valido\n"
			"(posiblemente el servidor devolvio un error).\n\n"
			"Tu instalacion actual no fue modificada."
		);
		errDlg->onConfirm = [&]() { errClosed = true; errDlg->hidden = true; };
		super::append(errDlg);
		errDlg->show();
		while (!errClosed) {
			InputEvents* ev = new InputEvents();
			while (ev->update()) {
				if (ev->pressed(A_BUTTON) || ev->pressed(B_BUTTON)) { errClosed = true; errDlg->hidden = true; break; }
				errDlg->process(ev);
			}
			RootDisplay::mainDisplay->render(NULL);
			delete ev;
			CST_Delay(16);
		}
		super::remove(errDlg);
		delete errDlg;
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
		FILE* tmpFile = fopen(APP_NRO_TMP, "rb");
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
			std::remove(APP_NRO_TMP);

			bool errClosed = false;
			auto* errDlg = new AlertDialog(
				"Archivo corrupto",
				"El archivo descargado no es un NRO valido.\n"
				"Puede que el link de descarga este desactualizado\n"
				"o que la descarga se haya interrumpido.\n\n"
				"Tu instalacion actual no fue modificada."
			);
			errDlg->onConfirm = [&]() { errClosed = true; errDlg->hidden = true; };
			super::append(errDlg);
			errDlg->show();
			while (!errClosed) {
				InputEvents* ev = new InputEvents();
				while (ev->update()) {
					if (ev->pressed(A_BUTTON) || ev->pressed(B_BUTTON)) { errClosed = true; errDlg->hidden = true; break; }
					errDlg->process(ev);
				}
				RootDisplay::mainDisplay->render(NULL);
				delete ev;
				CST_Delay(16);
			}
			super::remove(errDlg);
			delete errDlg;
			return false;
		}
	}

	std::cout << "[self-update] .tmp validado correctamente (" 
	          << tmpStat.st_size << " bytes, magic NRO0 OK)" << std::endl;

	// --- Reemplazar el .nro ---
	// En exFAT (Switch), rename() falla si el destino ya existe.
	// Hay que borrar el .nro actual primero. Es seguro porque el .nro
	// ya fue cargado completamente en RAM por nx-hbloader al iniciar.
	std::remove(APP_NRO_PATH);
	if (std::rename(APP_NRO_TMP, APP_NRO_PATH) != 0) {
		std::cout << "[self-update] fallo el rename de .tmp a .nro: " << strerror(errno) << std::endl;
		std::remove(APP_NRO_TMP);

		bool errClosed = false;
		std::string renameErr =
			std::string("No se pudo reemplazar el archivo .nro.\n") +
			"Error: " + strerror(errno) + "\n\n" +
			"Ruta destino: " APP_NRO_PATH "\n\n" +
			"Tu instalacion actual no fue modificada.";
		auto* errDlg = new AlertDialog("Error al aplicar update", renameErr);
		errDlg->onConfirm = [&]() { errClosed = true; errDlg->hidden = true; };
		super::append(errDlg);
		errDlg->show();
		while (!errClosed) {
			InputEvents* ev = new InputEvents();
			while (ev->update()) {
				if (ev->pressed(A_BUTTON) || ev->pressed(B_BUTTON)) { errClosed = true; errDlg->hidden = true; break; }
				if (ev->isTouch()) errDlg->process(ev);
			}
			RootDisplay::mainDisplay->render(NULL);
			delete ev;
			CST_Delay(16);
		}
		super::remove(errDlg);
		delete errDlg;
		return false;
	}

	std::cout << "[self-update] actualizacion aplicada correctamente" << std::endl;

	// --- Dialogo final ---
	bool closedDone = false;
	auto* doneDlg = new AlertDialog(
		"Actualizacion completada",
		"v" + remoteVersion + " instalada correctamente.\n\n"
		"Cierra la aplicacion y vuelve a abrirla\n"
		"para usar la nueva version."
	);
	doneDlg->onConfirm = [&]() {
		closedDone = true;
		doneDlg->hidden = true;
	};
	super::append(doneDlg);
	doneDlg->show();

	while (!closedDone) {
		InputEvents* events = new InputEvents();
		while (events->update())
			doneDlg->process(events);
		RootDisplay::mainDisplay->render(NULL);
		delete events;
		CST_Delay(16);
	}

	super::remove(doneDlg);
	delete doneDlg;

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
		spinner->constrain(ALIGN_TOP, 90)->constrain(ALIGN_CENTER_HORIZONTAL, 0)->constrain(OFFSET_LEFT, 45);
		super::append(spinner);

#if defined(_3DS) || defined(_3DS_MOCK)
		spinner->resize(40, 40);
		spinner->position(SCREEN_WIDTH / 2 - spinner->width / 2, 70);
#endif

		networking_callback = MainDisplay::updateLoader;

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
			RootDisplay::switchSubscreen(new ErrorScreen(i18n("errors.nowifi"), connTestMsg + "\n" + i18n("errors.dnsmsg")));
			return true;
		}

		if (!atLeastOneEnabled)
		{
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

	// Abrir la pantalla de temas con el boton R, solo si no hay otro subscreen activo
	// (por ejemplo, no debe abrirse mientras se esta viendo AppDetails)
	if (!RootDisplay::subscreen && event->pressed(R_BUTTON))
	{
		RootDisplay::switchSubscreen(new ThemeScreen());
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
