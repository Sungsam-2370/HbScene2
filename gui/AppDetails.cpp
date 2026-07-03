#include <fstream>
#include <sstream>

#if defined(SWITCH)
#include <switch.h>
#endif

#if defined(__WIIU__)
#include "../libs/librpxloader/include/rpxloader/rpxloader.h"
#endif

#include "../libs/get/src/Get.hpp"
#include "../libs/get/src/Utils.hpp"

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/Constraint.hpp"

#include "AppDetails.hpp"
#include "AppList.hpp"
#include "Feedback.hpp"
#include "ThemeManager.hpp"
#include "ProtectedCategories.hpp"
#include "RecentContentPolicy.hpp"
#include "SupporterBenefit.hpp"
#include "main.hpp"
// gAtmosphereValid se declara en main.hpp y se define en main.cpp

int AppDetails::lastFrameTime = 99;

AppDetails::AppDetails(Package& package, AppList* appList, AppCard* appCard)
	: package(&package)
	, get(appList->get)
	, appList(appList)
	, appCard(appCard)
	, downloadProgress()
	, download(getAction(&package), package.getStatus() == INSTALLED ? X_BUTTON : A_BUTTON, true, 30 / SCALER, 250)
	, cancel(i18n("details.cancel"), B_BUTTON, true, 30 / SCALER, 250)
	, details(getPackageDetails(&package).c_str(), 20 / SCALER, &white, false, 300)
	, content(&package, appList->useBannerIcons)
	, downloadStatus(i18n("details.status"), 30 / SCALER, &white)
	, downloadPercent("0%", 24 / SCALER, &white)
{
	// TODO: show current app status somewhere

	// download/update/remove button (2)

	download.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 170);
	download.action = std::bind(&AppDetails::proceed, this);

	cancel.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 90);
	cancel.action = std::bind(&AppDetails::back, this);

	// La validacion del hash de sd:atmosphere/package3 (ver checkAtmosphereHash()
	// en main.cpp) ya NO bloquea la aplicacion completa. Su alcance se redujo:
	// solo restringe la descarga de paquetes que pertenezcan a una categoria
	// "protegida" (ver ProtectedCategories.hpp, por defecto solo "PkUnico").
	// Cualquier otra categoria permite descargar sin importar el resultado
	// de la validacion.
	bool categoryRequiresValidation = isCategoryProtected(this->package->getCategory());
	bool blockedByCategoryValidation = categoryRequiresValidation && !gAtmosphereValid;

	// Restriccion independiente por antiguedad: los usuarios que NO son
	// beneficiarios (ver SupporterBenefit.hpp) deben esperar a que un
	// aporte tenga mas de kRecentContentRestrictionDays dias desde su
	// ultima actualizacion (repo.json -> "updated"). Esto aplica sin
	// importar la categoria, y es independiente de la validacion de arriba:
	// un beneficiario que descarga de una categoria protegida SIGUE
	// necesitando pasar la validacion de hash.
	bool isRecent = isPackageRecentlyUpdated(this->package->getUpdatedAtTimestamp());
	bool blockedByRecency = isRecent && !gIsSupporter;

	if (blockedByCategoryValidation && this->package->getStatus() != INSTALLED)
	{
		noValidationDialog = new AlertDialog(
			"Acceso restringido",
			"Para poder descargar debe usar\nel paquete de archivos de\nliberacion de Switch Scene."
		);
		noValidationDialog->onConfirm = [this]() {
			noValidationDialog->hidden = true;
		};
		super::append(noValidationDialog);

		download.action = [this]() {
			noValidationDialog->show();
		};
		download.updateText("Requiere PkUnico");
	}
	else if (blockedByRecency && this->package->getStatus() != INSTALLED)
	{
		std::stringstream recentMsg;
		recentMsg << "Este aporte es reciente, los aportes\n"
		           << "recientes solo pueden ser descargados\n"
		           << "por miembros que apoyaron, espera unos\n"
		           << "dias, hasta que el aporte tenga una fecha\n"
		           << "de actualizacion mayor a " << kRecentContentRestrictionDays << " dias,\n"
		           << "despues de esa fecha sera de libre descarga.";

		recentContentDialog = new AlertDialog(
			"Aporte reciente",
			recentMsg.str()
		);
		recentContentDialog->onConfirm = [this]() {
			recentContentDialog->hidden = true;
		};
		super::append(recentContentDialog);

		download.action = [this]() {
			recentContentDialog->show();
		};
		download.updateText("Aporte reciente");
	}

#if defined(_3DS) || defined(_3DS_MOCK)
	download.position(SCREEN_WIDTH / SCALER - download.width / SCALER, 360);
	cancel.position(SCREEN_WIDTH / SCALER - cancel.width / SCALER, 410);
#endif

	// display an additional launch/install button if the package is installed,  and has a binary or is a theme

	bool hasBinary = package.getBinary() != "none";
	bool isTheme = package.getCategory() == "theme";

	if (package.getStatus() != GET && (hasBinary || isTheme))
	{
		download.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 250);
		cancel.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 90);

		std::string buttonLabel = i18n("details.launch");
		bool injectorPresent = false;

		if (isTheme) // should only happen on switch
		{
			auto installer = get->lookup("NXthemes_Installer");
			injectorPresent = installer ? true : false; // whether or not the currently hardcoded installer package exists, in the future becomes something functionality-based like "theme_installer"
			buttonLabel = (injectorPresent && installer->getStatus() == GET) ? i18n("details.injector") : i18n("details.inject");
		}

		// show the third button if a binary is present, or a theme injector is available (installed or not)
		if (hasBinary || injectorPresent)
		{
			this->canLaunch = true;

			start = new Button(buttonLabel, START_BUTTON, true, 30, 250);
			start->position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 170);
			start->action = std::bind(&AppDetails::launch, this);
			super::append(start);
		}
	}

	// more details

	details.position(SCREEN_WIDTH - 310, 50);
	super::append(&details);

	// the scrollable portion of the app details page
	super::append(&content);

	super::append(&download);
	super::append(&cancel);

	downloadProgress.width = PANE_WIDTH;
	downloadProgress.position(SCREEN_WIDTH / 2 - downloadProgress.width / 2, PANE_WIDTH / 2 - 5);
	downloadProgress.color = 0xff0000ff;
	downloadProgress.dimBg = true;

	// download informations (not visible until the download is started)
	downloadStatus.position(SCREEN_WIDTH / 2 - downloadProgress.width / 2, PANE_WIDTH / 2 - 70 / SCALER);

	// contador de porcentaje, centrado, debajo de la barra de progreso
	downloadPercent.position(SCREEN_WIDTH / 2, PANE_WIDTH / 2 + 15 / SCALER);
	downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);
}

AppDetails::~AppDetails()
{
	if (start)
	{
		super::remove(start);
		delete start;
	}
	if (errorText)
	{
		super::remove(errorText);
		delete errorText;
	}
}

std::string AppDetails::getPackageDetails(Package* package)
{
	// lots of details that we know about the package
	std::stringstream more_details;
	more_details << i18n("details.title") << " " << package->getTitle() << "\n"
				 << package->getShortDescription() << "\n\n"
				 << i18n("details.author") << " " << package->getAuthor() << "\n"
				 << i18n("details.version") << " " << package->getVersion() << "\n\n"
				 << i18n("details.license") << " " << package->getLicense() << "\n\n"
				 // << i18n("details.downloads") << " " << i18n_number(package->getDownloadCount()) << "\n"
				 << i18n("details.updated") << " " << i18n_date(package->getUpdatedAtTimestamp())<< "\n\n"
				 << i18n("details.size") << " " << package->getHumanDownloadSize() << "\n";
	return more_details.str();
}

std::string AppDetails::getAction(Package* package)
{
	switch (package->getStatus())
	{
	case GET:
		return i18n("details.download");
	case UPDATE:
		return i18n("details.update");
	case INSTALLED:
		return i18n("details.remove");
	case LOCAL:
		return i18n("details.reinstall");
	default:
		break;
	}
	return "?";
}

void AppDetails::proceed()
{
	if (this->operating) return;

	this->operating = true;
	// event->update();

	// description of what we're doing
	super::append(&downloadProgress);
	super::append(&downloadStatus);
	super::append(&downloadPercent);

	// setup progress bar callback
	networking_callback = AppDetails::updateCurrentlyDisplayedPopup;
	libget_status_callback = AppDetails::updatePopupStatus;

	// if we're installing ourselves, we need to quit after on switch
	preInstallHook();

	// install or remove this package based on the package status
	if (this->package->getStatus() == INSTALLED) {
		get->remove(*package);
	} else {
		get->install(*package);
		// save the icon to the SD card, for offline use
		if (appCard != NULL) {
			auto iconSavePath = std::string(get->mPkg_path) + "/" + package->getPackageName() + "/icon.png";
			appCard->icon.saveTo(iconSavePath);
			//TODO: load from a cache instead!!
		}
	}

	postInstallHook();

	// refresh the screen
    RootDisplay::switchSubscreen(nullptr);

    // forzar re-evaluacion del status tras borrar
    this->package->updateStatus(get->mPkg_path);

    this->operating = false;
    this->appList->update();
}

void AppDetails::launch()
{
	if (!this->canLaunch) return;

	char path[8 + strlen(package->getBinary().c_str())];

	snprintf(path, sizeof(path), ROOT_PATH "%s", package->getBinary().c_str()+1);
	printf("Launch path: %s\n", path);

	FILE* file;
	bool successLaunch = false;

	if (package->getCategory() == "theme")
	{
		auto installer = get->lookup("NXthemes_Installer"); // This should probably be more dynamic in future, e.g. std::vector<Package*> Get::find_functionality("theme_installer")
		if (installer && installer->getStatus() != GET)
		{
			snprintf(path, sizeof(path), ROOT_PATH "%s", installer->getBinary().c_str()+1);
			successLaunch = this->themeInstall(path);
		}
		else
		{
			successLaunch = true;
			this->getSupported();
		}
	}
	else
	{
		// Final check if path actually exists
		if ((file = fopen(path, "r")))
		{
			fclose(file);
			printf("Path OK, Launching...\n");
			successLaunch = this->launchFile(path, path);
		}
		else
			successLaunch = false;
	}

	if (!successLaunch)
	{
		// printf("Failed to launch.");
		errorText = new TextElement(i18n("errors.applaunch"), 24, &red, false, 300);
		errorText->position(970, 430);
		super::append(errorText);
		this->canLaunch = false;
	}

}

void AppDetails::getSupported()
{
	auto installer = get->lookup("NXthemes_Installer");
	if (installer)
		RootDisplay::switchSubscreen(new AppDetails(installer.value(), appList));
}

void AppDetails::back()
{
	if (this->operating) return;

	RootDisplay::switchSubscreen(nullptr);
}

void AppDetails::moreByAuthor()
{
	const char* author = this->package->getAuthor().c_str();
	appList->sidebar->searchQuery = std::string(author);
	appList->sidebar->curCategory = 0;
	appList->update();
	appList->y = 0;
	appList->keyboard.hidden = true;
	RootDisplay::switchSubscreen(nullptr);
}

void AppDetails::leaveFeedback()
{
	RootDisplay::switchSubscreen(new Feedback(*(this->package)));
}

bool AppDetails::process(InputEvents* event)
{
	if (event->isTouchDown())
		this->dragging = true;

	if (this->operating) return false;

	if (content.showingScreenshot)
	{
		// if the screenshot is displayed, it's kind of like a second subscreen, and eats all inputs
		// TODO: this is a pattern chesto should handle better (like a stack of subscreens)
		return elements[elements.size() - 1]->process(event);
	}
	return super::process(event);
}

void AppDetails::preInstallHook()
{
// if on wii u and installing ourselves, we need to unmount our WUHB and exit after
#if defined(__WIIU__)
	if (this->package->getPackageName() == APP_SHORTNAME)
	{
		RPXLoaderStatus ret = RPXLoader_InitLibrary();
		if (ret == RPX_LOADER_RESULT_SUCCESS)
		{
			// unmount ourselves
			RPXLoader_UnmountCurrentRunningBundle();
		}
	}
#endif
}

bool AppDetails::themeInstall(char* installerPath)
{
	std::string ManifestPathInternal = "manifest.install";
	std::string ManifestPath = get->mPkg_path + this->package->getPackageName() + "/" + ManifestPathInternal;

	std::vector<std::string> themePaths;

	if (!package->manifest.isValid()) {
		package->manifest = Manifest(ManifestPath, ROOT_PATH);
	}

	if (package->manifest.isValid())
	{
		auto entries = package->manifest.getEntries();
		for (size_t i = 0; i <= entries.size() - 1; i++)
		{
			if (entries[i].operation == MUPDATE && entries[i].extension == "nxtheme")
			{
				printf("Found nxtheme\n");
				themePaths.push_back(entries[i].path);
			}
		}
	}
	else
	{
		printf("--> ERROR: no manifest found/manifest invalid at %s\n", ManifestPath.c_str());
		return false;
	}

	std::string themeArg = "installtheme=";
	for (int i = 0; i < (int)themePaths.size(); i++)
	{
		if (i == (int)themePaths.size() - 1)
		{
			themeArg.append(themePaths[i]);
		}
		else
		{
			themeArg.append(themePaths[i]);
			themeArg.append(",");
		}
	}
	printf("Theme Install: %s\n", themeArg.c_str());
	size_t index = 0;
	while (true)
	{
		index = themeArg.find(" ", index);
		if (index == std::string::npos) break;
		themeArg.replace(index, 1, "(_)");
	}
	char args[strlen(installerPath) + themeArg.size() + 8];
	snprintf(args, sizeof(args), "%s %s", installerPath, themeArg.c_str()+1);

	return this->launchFile(installerPath, args);
}

bool AppDetails::launchFile(char* path, char* context)
{
#if defined(SWITCH)
	// If setnexload works without problems, quit to make loader open next nro
	if (R_SUCCEEDED(envSetNextLoad(path, context)))
	{
		RootDisplay::mainDisplay->requestQuit();
		return true;
	}
#elif defined(__WIIU__)
	RPXLoaderStatus ret = RPXLoader_InitLibrary();
	if (ret == RPX_LOADER_RESULT_SUCCESS)
	{
		return RPXLoader_LaunchHomebrew(path) == RPX_LOADER_RESULT_SUCCESS;
	}
#endif
	printf("Would have launched %s, but not implemented on this platform\n", path);
	return false;
}

void AppDetails::postInstallHook()
{
	networking_callback = nullptr;
	libget_status_callback = nullptr;

	if (quitAfterInstall) {
		RootDisplay::mainDisplay->requestQuit();
	}
}

void AppDetails::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// Panel derecho: ancho ajustado para alinearse con los botones (SCREEN_WIDTH - 310)
	// se le da un margen extra de 20px para que el texto no quede pegado al borde
	int rightPanelX = SCREEN_WIDTH - 310 - 20;
	int rightPanelW = SCREEN_WIDTH - rightPanelX;

	// 1. Fondo del panel de contenido (toda la pantalla menos el panel derecho)
	CST_Rect contentDimens = { 0, 0, rightPanelX, SCREEN_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
	CST_FillRect(RootDisplay::renderer, &contentDimens);

	// 2. Fondo del panel derecho (detalles: autor, version, tamaño, botones)
	// usa el color del sidebar para diferenciarse visualmente del panel de contenido
	CST_Rect rightPanelDimens = { rightPanelX, 0, rightPanelW, SCREEN_HEIGHT };
	CST_Color rightPanelColor = {
		HBAS::ThemeManager::sidebarColor.r,
		HBAS::ThemeManager::sidebarColor.g,
		HBAS::ThemeManager::sidebarColor.b,
		0xff
	};
	CST_SetDrawColor(RootDisplay::renderer, rightPanelColor);
	CST_FillRect(RootDisplay::renderer, &rightPanelDimens);

	// draw all elements
	super::render(parent);

	// Renderizar el dialogo de validacion AL FINAL para que quede
	// siempre por encima del contenido y de las imagenes/descripcion
	if (noValidationDialog)
		noValidationDialog->render(parent);

	if (recentContentDialog)
		recentContentDialog->render(parent);
}

int AppDetails::updatePopupStatus(int status, int num, int num_total)
{
	auto screen = RootDisplay::subscreen;
	std::stringstream statusText;

	if (screen != NULL)
	{
		AppDetails* popup = (AppDetails*)screen;
		Package* package = popup->package;

		if (status < 0 || status >= 5) return 0;
		std::string statuses[6] = {
			i18n("details.download.verb") + " ",
			"Extrayendo ",
			i18n("details.remove.verb") + " ",
			i18n("details.reloading"),
			i18n("details.syncing") + " ",
			i18n("details.analyzing") + " "
		};

		statusText << statuses[status];

		if (status <= STATUS_REMOVING)
			statusText << package->getTitle();

		statusText << "...";

		if (num_total != 1)
		{
			// num_total for this operation isn't 1, so let's display a counter in parens
			// (for instance, with multiple repos)
			statusText << " (" << num << "/" << num_total << ")";
		}

		popup->downloadStatus.setText(statusText.str());
		popup->downloadStatus.update();
	}
	return 0;
}

int AppDetails::updateCurrentlyDisplayedPopup(void* clientp, double dlnow)
{
	(void)clientp;
	int now = CST_GetTicks();
	int diff = now - AppDetails::lastFrameTime;

	double amount = dlnow;

	// don't update the GUI too frequently here, it slows down downloading
	// (never return early if it's 100% done)
	if (diff < 32 && amount != 1)
		return 0;

	AppDetails* popup = (AppDetails*)RootDisplay::subscreen;

	// Reportar actividad del usuario para resetear el temporizador de
	// atenuado de pantalla, sin desactivar el sleep permanentemente.
	// Esto evita que la pantalla se atenúe durante descargas largas.
#if defined(SWITCH)
	appletReportUserIsActive();
#endif

	// update the amount
	if (popup != NULL)
	{
		popup->downloadProgress.percent = amount;

		// actualizar el texto del porcentaje (0-100%)
		int percentInt = (int)(amount * 100);
		if (percentInt < 0) percentInt = 0;
		if (percentInt > 100) percentInt = 100;
		popup->downloadPercent.setText(std::to_string(percentInt) + "%");
		popup->downloadPercent.update();
		popup->downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);

		// force render the element right here (and it's progress bar too)
		if (popup->parent != NULL)
		{
			InputEvents* events = new InputEvents();
			while (events->update())
				RootDisplay::mainDisplay->process(events);
			RootDisplay::mainDisplay->render(NULL);
		}
	}

	AppDetails::lastFrameTime = CST_GetTicks();

	return 0;
}
