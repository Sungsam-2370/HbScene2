#include <fstream>
#include <sstream>

#if defined(SWITCH)
#include <switch.h>
#include "../libs/get/src/nspinstall/NspAutoInstall.hpp"
#include "../libs/get/src/nspinstall/es_ipc.h"
#include "../libs/get/src/nspinstall/ns_ext_ipc.h"
#endif

#if defined(__WIIU__)
#include "../libs/librpxloader/include/rpxloader/rpxloader.h"
#endif

#include "../libs/get/src/Get.hpp"
#include "../libs/get/src/Utils.hpp"
#include "../libs/get/src/Cancel.hpp"

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/Constraint.hpp"

#include "AppDetails.hpp"
#include "AppList.hpp"
#include "Sidebar.hpp"
#include "FooterStyle.hpp"
#include "MainDisplay.hpp"
#include "Feedback.hpp"
#include "ThemeManager.hpp"
#include "ProtectedCategories.hpp"
#include "RecentContentPolicy.hpp"
#include "SupporterBenefit.hpp"
#include "main.hpp"
// gAtmosphereValid se declara en main.hpp y se define en main.cpp

int AppDetails::lastFrameTime = 99;

// --- cancelacion con B mantenido ---
static constexpr int CANCEL_HOLD_MS = 5000;
static int gBHeldSince = 0;      // instante en que se presiono B (0 = no esta presionada)
static int gLastHintSecs = -2;   // ultimo estado mostrado en el aviso (para no redibujar de mas)
static int gLastPollTick = 0;

// Botones de la pantalla de detalle: en Switch/PC van en la barra inferior (planos, solo
// icono + texto, igual que la lista); en 3DS se mantienen los botones de siempre.
#if defined(_3DS) || defined(_3DS_MOCK)
#define DETAILS_BTN_STYLE true, 30 / SCALER, 250
#else
#define DETAILS_BTN_STYLE false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f
#endif

AppDetails::AppDetails(Package& package, AppList* appList, AppCard* appCard)
	: package(&package)
	, get(appList->get)
	, appList(appList)
	, appCard(appCard)
	, downloadProgress()
	, download(getAction(&package), package.getStatus() == INSTALLED ? X_BUTTON : A_BUTTON, DETAILS_BTN_STYLE)
	, removeBtn(i18n("details.remove"), X_BUTTON, DETAILS_BTN_STYLE)
	, cancel(i18n("details.cancel"), B_BUTTON, DETAILS_BTN_STYLE)
	, details(getPackageDetails(&package).c_str(), 20 / SCALER, &white, false, 300)
	, content(&package, appList->useBannerIcons)
	, downloadStatus(i18n("details.status"), 30 / SCALER, &white)
	, downloadPercent("0%", 24 / SCALER, &white)
	, cancelHint("Manten B 5 s para cancelar", 20 / SCALER, &white)
{
	// TODO: show current app status somewhere

	// download/update/remove button (2)

	download.action = std::bind(&AppDetails::proceed, this);
	removeBtn.action = std::bind(&AppDetails::removePackage, this);
	cancel.action = std::bind(&AppDetails::back, this);
#if defined(_3DS) || defined(_3DS_MOCK)
	download.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 170);
	cancel.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 90);
#endif

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
		// Mismo estilo que el resto de avisos de la app (fondo
		// semitransparente + titulo + mensaje + boton Aceptar), en vez
		// del AlertDialog flotante que se usaba antes.
		download.action = []() {
			((MainDisplay*)RootDisplay::mainDisplay)->showFullscreenPrompt(
				"Acceso restringido",
				"Para poder descargar en esta seccion debes de usar\n"
				"el paquete de archivos del grupo Switch Scene (PkUnico)\n\n"
				"Consulta en el grupo para mas informacion\n\n"
				"Las otras secciones son libres para descargar",
				false
			);
		};
		download.updateText("Requiere PkUnico");
	}
	else if (blockedByRecency && this->package->getStatus() != INSTALLED)
	{
		std::stringstream recentMsg;
		recentMsg << "Este es un aporte reciente\n\n"
		           << "Los aportes recientes solo pueden ser\n"
		           << "descargados por miembros que apoyaron\n\n"
		           << "Deberas esperar unos dias, hasta que la \n"
		           << "fecha de subida del aporte o actualizacion\n"
		           << "sea mayor a " << kRecentContentRestrictionDays << " dias,\n\n"
		           << "Despues de la fecha se podra descarga\n"
		           << "de forma normal\n\n"
		           << "Consulta la fecha en el menu lateral\n"
		           << "derecho en cada aporte";

		// Mismo estilo que el resto de avisos de la app (fondo
		// semitransparente + titulo + mensaje + boton Aceptar), en vez
		// del AlertDialog flotante que se usaba antes.
		std::string recentMsgStr = recentMsg.str();
		download.action = [recentMsgStr]() {
			((MainDisplay*)RootDisplay::mainDisplay)->showFullscreenPrompt(
				"Aporte reciente",
				recentMsgStr,
				false
			);
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
#if defined(_3DS) || defined(_3DS_MOCK)
		download.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 250);
		cancel.position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 90);
#endif

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

			start = new Button(buttonLabel, START_BUTTON, DETAILS_BTN_STYLE);
#if defined(_3DS) || defined(_3DS_MOCK)
			start->position(SCREEN_WIDTH - 310, SCREEN_HEIGHT - 170);
#endif
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
	// con una actualizacion pendiente el boton principal es "Actualizar"; tambien se
	// puede borrar el componente (antes solo aparecia Borrar si estaba al dia)
	if (package.getStatus() == UPDATE)
	{
		hasRemoveBtn = true;
		super::append(&removeBtn);
	}
	super::append(&cancel);
	layoutFooter();

	downloadProgress.width = PANE_WIDTH;
	downloadProgress.position(SCREEN_WIDTH / 2 - downloadProgress.width / 2, PANE_WIDTH / 2 - 5);
	downloadProgress.color = 0xff0000ff;
	downloadProgress.dimBg = true;

	// download informations (not visible until the download is started)
	downloadStatus.position(SCREEN_WIDTH / 2 - downloadProgress.width / 2, PANE_WIDTH / 2 - 70 / SCALER);

	// contador de porcentaje, centrado, debajo de la barra de progreso
	downloadPercent.position(SCREEN_WIDTH / 2, PANE_WIDTH / 2 + 15 / SCALER);
	downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);

	// aviso de cancelacion, debajo del porcentaje
	cancelHint.position(SCREEN_WIDTH / 2, PANE_WIDTH / 2 + 70 / SCALER);
	cancelHint.constrain(ALIGN_CENTER_HORIZONTAL, 0);
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

	// Si el paquete trae la linea "aviso" en repo.json, se muestra antes de
	// descargar: A / touch en "Aceptar" confirma, B / touch en "Cancelar" cancela.
	// (al desinstalar no hace falta)
	if (!isUninstall() && !this->package->getNotice().empty())
	{
		bool confirmed = ((MainDisplay*)RootDisplay::mainDisplay)->showFullscreenPrompt(
			"Aviso", this->package->getNotice(), true);
		if (!confirmed)
			return;
	}

	this->operating = true;
	// event->update();

	// estado de cancelacion limpio para esta operacion
	libget_cancel_reset();
	g_libget_created.clear();
	gBHeldSince = 0;
	gLastHintSecs = -2;
	gLastPollTick = 0;
	cancelHint.setText("Manten B 5 s para cancelar");
	cancelHint.update();
	cancelHint.constrain(ALIGN_CENTER_HORIZONTAL, 0);

	// description of what we're doing
	super::append(&downloadProgress);
	super::append(&downloadStatus);
	super::append(&downloadPercent);
	if (!isUninstall()) // al desinstalar no aplica
	{
		super::append(&cancelHint);
		g_libget_poll = &AppDetails::pollCancelHook;
	}

	// setup progress bar callback
	networking_callback = AppDetails::updateCurrentlyDisplayedPopup;
	libget_status_callback = AppDetails::updatePopupStatus;

	// if we're installing ourselves, we need to quit after on switch
	preInstallHook();

	bool wasUninstall = isUninstall();
	bool succeeded = true;

	// copia del registro actual del paquete (manifest.install + info.json), para
	// poder dejarlo EXACTAMENTE igual si el usuario cancela a mitad de camino
	const std::string pkgDir = get->mPkg_path + this->package->getPackageName() + "/";
	std::string bakManifest, bakInfo;
	bool hadManifest = false, hadInfo = false;
	if (!wasUninstall)
	{
		auto slurp = [](const std::string& path, std::string& out) -> bool {
			std::ifstream f(path, std::ios::binary);
			if (!f) return false;
			std::stringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		};
		hadManifest = slurp(pkgDir + "manifest.install", bakManifest);
		hadInfo = slurp(pkgDir + "info.json", bakInfo);
	}

	// install or remove this package based on the package status
	if (wasUninstall) {
		succeeded = (get->remove(*package) != 0);
	} else {
		// Paso 1 (descarga) + Paso 2 (extraccion): Get::install() hace
		// ambos, incluyendo multi-zip si el paquete lo requiere. Cada uno
		// ya tiene su propia barra de progreso (via networking_callback +
		// libget_status_callback, arriba).
		// con actualizacion incremental aplicable se usa SOLO el zip de actualizacion
		// (aunque el componente sea de instalacion directa): ni zip_url ni directa
		const bool useDeltaUpdate = package->getStatus() == UPDATE && package->canUseUpdateZip();
		const bool directOnly = package->isDirectOnly() && !useDeltaUpdate;
		if (directOnly)
		{
			// instalacion directa sin zips: no hay nada que descargar/extraer
			// a la SD, el NSP/NSZ se instala mientras se baja (mas abajo)
			package->runtime_used_delta = false;
			succeeded = true;
		}
		else
		{
			succeeded = (get->install(*package) != 0);
		}

#if defined(SWITCH)
		// si la descarga o la extraccion fallaron a medias, el archivo de
		// "instalacion" (si llego a extraerse) tampoco se necesita: se borra solo ese archivo
		if (!succeeded)
			for (const auto& f : package->getInstallList())
				nspinstall::DeleteInstallFile(ROOT_PATH, f);
#endif

		// save the icon to the SD card, for offline use
		if (appCard != NULL && !directOnly && !libget_cancelled()) {
			// IMPORTANTE: el caché LOCAL del icono se guarda como PNG
			// (saveTo, ya probado/confiable) aunque el servidor lo tenga
			// en JPG y la descarga/visualizacion en red siga usando JPG.
			// saveToJpg()/IMG_SaveJPG parece tener un problema real en
			// esta plataforma (crash reproducible con una sola instalacion,
			// ver historial) -- como el cache local es solo un detalle
			// interno (fallback offline), no hace falta que coincida con
			// el formato remoto.
			auto iconSavePath = std::string(get->mPkg_path) + "/" + package->getPackageName() + "/icon.png";
			appCard->icon.saveTo(iconSavePath, 256); // 256: nunca se muestra mas grande que eso, ver AppCard
			//TODO: load from a cache instead!!
		}

#if defined(SWITCH)
		// --- Paso 3: instalacion del NSP/NSZ/XCI (repo.json: "instalacion") ---
		// Paso explicito y SEPARADO de la descarga/extraccion de arriba.
		// Solo corre si el paso 1+2 salio bien y el paquete realmente
		// trae la clave "instalacion" en su repo.json.
		// "instalacion" puede ser un archivo o una lista: se instalan en orden
		std::vector<std::string> nspList;
		if (succeeded)
			nspList = package->getInstallList();
		if (!nspList.empty() && package->runtime_used_delta)
		{
			// actualizacion incremental: el zip solo trae lo que cambio. Los
			// archivos de "instalacion" que no esten en la SD se omiten (no es un
			// error); si no queda ninguno, no hay nada nuevo que instalar.
			std::vector<std::string> present;
			for (const auto& f : nspList)
			{
				std::string rel = f;
				while (!rel.empty() && rel.front() == '/')
					rel.erase(0, 1);
				struct stat nspStat = {};
				if (stat((std::string(ROOT_PATH) + rel).c_str(), &nspStat) == 0)
					present.push_back(f);
				else
					printf("--> Actualizacion incremental sin archivo de instalacion [%s], se omite\n", f.c_str());
			}
			nspList = present;
		}
		bool nspStepNeeded = !nspList.empty();
			auto nspProgress = [](std::uint64_t done, std::uint64_t total, const std::string &, bool preparing) {
					// reutiliza el mismo callback de progreso/heartbeat que
					// ya usa la descarga -- todavia esta activo aca porque
					// postInstallHook() (que lo limpia) corre despues
					//
					// "preparing" = true durante CreatePlaceholder (NCM
					// reservando de antemano el espacio del archivo). Esa
					// reserva es una sola llamada bloqueante sin progreso
					// real posible -- para un NCA grande puede tardar
					// bastante, y sin este texto la barra se queda en 0%
					// sin ninguna senal de que sigue viva. Una vez que
					// arranca la escritura real (preparing == false),
					// volvemos al texto normal y el % ya avanza solo.
					auto* popup = (AppDetails*)RootDisplay::subscreen;
					if (popup != NULL)
					{
						static bool wasPreparing = false;
						if (preparing && !wasPreparing)
						{
							popup->downloadStatus.setText("Reservando espacio...");
							popup->downloadStatus.update();
						}
						else if (!preparing && wasPreparing)
						{
							popup->downloadStatus.setText("Instalando...");
							popup->downloadStatus.update();
						}
						wasPreparing = preparing;
					}

					if (networking_callback != nullptr && total > 0)
						networking_callback(nullptr, (double)done / (double)total);
				};

		if (nspStepNeeded)
		{
			// pantalla propia: reusamos los mismos elementos de
			// descarga/extraccion (misma barra, mismo estilo), solo
			// cambiando el texto para que se note que es un paso distinto
			downloadStatus.setText("Instalando...");
			downloadStatus.update();
			downloadPercent.setText("0%");
			downloadPercent.update();
			downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);
			downloadProgress.percent = 0;
			RootDisplay::mainDisplay->render(NULL);

			// Los servicios de instalacion de titulos (spl/ncm/es/ns) se
			// abren SOLO aca, justo antes de usarlos, y se cierran apenas
			// terminamos -- no se mantienen abiertos toda la sesion.
			splInitialize();
			splCryptoInitialize();
			ncmInitialize();
			esInitialize();
			nsInitialize();
			nsextInitialize();

			nspinstall::AutoInstallResult nspResult;
			nspResult.success = true;
			size_t nspDone = 0;
			for (size_t i = 0; i < nspList.size(); i++)
			{
				if (nspList.size() > 1)
				{
					char txt[48];
					snprintf(txt, sizeof(txt), "Instalando %d/%d...", (int)i + 1, (int)nspList.size());
					downloadStatus.setText(txt);
					downloadStatus.update();
					downloadPercent.setText("0%");
					downloadPercent.update();
					downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);
					downloadProgress.percent = 0;
					RootDisplay::mainDisplay->render(NULL);
				}

				nspResult = nspinstall::InstallNspIfRequested(
					ROOT_PATH, nspList[i], false, nspProgress);

				// Ya intentamos instalar (bien o mal): el .nsp/.nsz/.xci extraido
				// ya no se necesita en la SD, asi que se borra SOLO ese archivo.
				nspinstall::DeleteInstallFile(ROOT_PATH, nspList[i]);
				printf("--> Instalacion de NSP [%s]: %s\n",
				       nspList[i].c_str(), nspResult.success ? "OK" : "FALLO");

				if (!nspResult.success)
				{
					// se detiene en el primer fallo; los archivos restantes no se necesitan
					for (size_t j = i + 1; j < nspList.size(); j++)
						nspinstall::DeleteInstallFile(ROOT_PATH, nspList[j]);
					break;
				}
				nspDone++;
			}
			if (nspResult.success && nspList.size() > 1)
				nspResult.message = "INSTALACION EXITOSA (" + std::to_string(nspDone) + " archivos)";

			nsextExit();
			nsExit();
			esExit();
			ncmExit();
			splCryptoExit();
			splExit();

			package->runtime_install_status = nspResult.message;

			if (!nspResult.success)
				succeeded = false;
		}

		// --- Paso 3b: instalacion directa (repo.json: "instalacion_directa") ---
		// El .nsp/.nsz se lee de internet con HTTP Range y se instala mientras
		// se descarga; nunca se escribe en la SD (sin limite de 4 GB de FAT32).
		if (succeeded && !package->getDirectInstallUrl().empty() && !(useDeltaUpdate && package->runtime_used_delta))
		{
			downloadStatus.setText("Instalando (directo)...");
			downloadStatus.update();
			downloadPercent.setText("0%");
			downloadPercent.update();
			downloadPercent.constrain(ALIGN_CENTER_HORIZONTAL, 0);
			downloadProgress.percent = 0;
			RootDisplay::mainDisplay->render(NULL);

			splInitialize();
			splCryptoInitialize();
			ncmInitialize();
			esInitialize();
			nsInitialize();
			nsextInitialize();

			auto directResult = nspinstall::InstallNspFromUrl(
				package->getDirectInstallUrl(), false, nspProgress);

			nsextExit();
			nsExit();
			esExit();
			ncmExit();
			splCryptoExit();
			splExit();

			package->runtime_install_status = directResult.message;
			printf("--> Instalacion directa [%s]: %s\n",
			       package->getDirectInstallUrl().c_str(), directResult.success ? "OK" : "FALLO");

			if (!directResult.success)
			{
				succeeded = false;
			}
			else if (package->isDirectOnly())
			{
				// recien ahora el paquete "existe": manifest + info.json (version)
				// y el icono para uso offline. Si fallo antes, no se creo nada.
				if (!package->writeDirectInstallRecord(get->mPkg_path))
				{
					printf("--> No se pudo escribir el registro del paquete\n");
					package->runtime_install_status += "\n\n(Aviso: no se pudo guardar el registro; el juego quedo instalado pero la tienda no lo marcara como instalado.)";
				}
				else if (appCard != NULL)
				{
					auto iconSavePath = std::string(get->mPkg_path) + "/" + package->getPackageName() + "/icon.png";
					appCard->icon.saveTo(iconSavePath, 256);
				}
			}
		}
#else
		if (directOnly)
		{
			package->runtime_install_status = "La instalacion directa solo esta disponible en Switch.";
			succeeded = false;
		}
#endif
	}

	// --- Cancelado por el usuario (B 5 s) ---
	// se deshace lo que esta operacion creo y se deja el registro como estaba:
	// ni info.json ni manifest.install quedan modificados
	// (si la operacion ya habia terminado bien, una cancelacion tardia se ignora)
	const bool userCancelled = libget_cancelled() && !wasUninstall && !succeeded;
	if (userCancelled)
	{
		printf("--> Cancelado por el usuario: deshaciendo lo creado\n");
		g_libget_created.rollback(); // archivos/carpetas NUEVOS de esta operacion

		auto restore = [](const std::string& path, bool had, const std::string& content) {
			if (had)
			{
				std::ofstream f(path, std::ios::binary | std::ios::trunc);
				f << content;
			}
			else
				std::remove(path.c_str());
		};
		restore(pkgDir + "manifest.install", hadManifest, bakManifest);
		restore(pkgDir + "info.json", hadInfo, bakInfo);
		std::remove((pkgDir + "manifest.install.new").c_str());
		std::remove((pkgDir + "info.json.new").c_str());

		if (!hadManifest)
		{
			// era una instalacion nueva: tampoco debe quedar el icono ni la carpeta
			std::remove((pkgDir + "icon.png").c_str());
			std::remove((pkgDir + "icon.jpg").c_str());
			rmdir(pkgDir.c_str());
		}

		package->removeTempDownloads(get->mTmp_path);
		succeeded = false;
	}

	postInstallHook();
	g_libget_poll = nullptr;
	gBHeldSince = 0;
	libget_cancel_reset();

	// Sacar de pantalla la barra de progreso (descarga/extraccion/instalacion)
	// ANTES de mostrar el mensaje final -- si no, queda dibujada de fondo
	// con su ultimo valor ("Instalando... 0%") superpuesta con el mensaje,
	// dando la falsa impresion de que se quedo pegada.
	super::remove(&downloadProgress);
	super::remove(&downloadStatus);
	super::remove(&downloadPercent);
	super::remove(&cancelHint);

	// --- Paso 4: mensaje final ---
	// Solo para instalaciones (no al desinstalar), mismo estilo que el
	// dialogo de auto-actualizacion: fondo oscurecido + texto suelto,
	// titulo grande, [A]/[B] para continuar.
	if (!wasUninstall)
	{
		std::string title = succeeded ? "Instalacion exitosa" : "Hubo un problema";
		std::string message;

		if (userCancelled)
		{
			title = "Operacion cancelada";
			message = "Se cancelo la operacion.\n\nSe borro lo descargado y no se modifico "
			          "el registro (info.json / manifest) de este componente.";
			if (hadManifest)
				message += "\n\nSi ya se habian reemplazado algunos archivos, esos no se pueden "
				           "restaurar: vuelve a actualizar para completarlo.";
		}
		else if (succeeded)
		{
			if (package->isDirectOnly())
			{
				message = "Descarga e instalacion completadas con exito.";
			}
			else
			{
				message = "Descarga, extraccion";
				if (!package->getInstallList().empty() || !package->getDirectInstallUrl().empty())
					message += " e instalacion";
				message += " completadas con exito.";
			}

			if (!package->runtime_install_status.empty())
				message += "\n\n" + package->runtime_install_status;

			if (!package->getInstallMessage().empty())
				message += "\n\n" + package->getInstallMessage();
		}
		else
		{
			message = "No se pudo completar la instalacion de este paquete.";
			if (!package->runtime_install_status.empty())
				message += "\n\n" + package->runtime_install_status;
		}

		((MainDisplay*)RootDisplay::mainDisplay)->showFullscreenPrompt(title, message, false);
	}

	// refresh the screen
    RootDisplay::switchSubscreen(nullptr);

    // forzar re-evaluacion del status tras borrar
    this->package->updateStatus(get->mPkg_path);

    this->forceUninstall = false;
    this->operating = false;
    this->appList->update();
}

// Borrar: quita el componente aunque tenga una actualizacion pendiente
// (mismo proceso que el boton de borrar de un componente al dia)
void AppDetails::removePackage()
{
	if (this->operating) return;
	this->forceUninstall = true;
	this->proceed();
	this->forceUninstall = false;
}

bool AppDetails::isUninstall() const
{
	return forceUninstall || package->getStatus() == INSTALLED;
}

// Barra inferior: mismos colores/alto que la de la lista; los botones van planos
// (solo icono + texto con el color del texto del tema). Abrir/Descargar/Borrar a
// la izquierda y Cancelar (volver) fijo en el extremo derecho, como "Salir".
void AppDetails::layoutFooter()
{
#if !defined(_3DS) && !defined(_3DS_MOCK)
	const bool light = FooterStyle::iconsLight();
	CST_Color* textColor = &HBAS::ThemeManager::textPrimary;

	download.setFlat(true, light, textColor);
	removeBtn.setFlat(true, light, textColor);
	cancel.setFlat(true, light, textColor);
	if (start)
		start->setFlat(true, light, textColor);

	const int footerY = SCREEN_HEIGHT - (SIDEBAR_FOOTER_HEIGHT - SIDEBAR_FOOTER_LINE) / 2 - cancel.height / 2;
	const int gap = 24 / SCALER;

	cancel.position(SCREEN_WIDTH - cancel.width - 20 / SCALER, footerY);

	int nextX = 20 / SCALER;
	if (start)
	{
		start->position(nextX, footerY);
		nextX += start->width + gap;
	}
	download.position(nextX, footerY);
	nextX += download.width + gap;
	if (hasRemoveBtn)
		removeBtn.position(nextX, footerY);
#endif
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
	appList->sidebar->searchModeActive = true;
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

#if defined(_3DS) || defined(_3DS_MOCK)
	// draw all elements
	super::render(parent);
#else
	// necesario para que xAbs/yAbs esten al dia (antes lo hacia super::render)
	this->recalcPosition(parent);

	auto isFooterButton = [this](Element* e) {
		return e == &download || e == &removeBtn || e == &cancel || (start != nullptr && e == start);
	};

	// 3. Contenido (banner, textos, capturas) y panel de detalles: recortados para que
	// nunca dibujen encima de la barra inferior al hacer scroll
	CST_Rect clip = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT };
	SDL_RenderSetClipRect(RootDisplay::renderer, &clip);
	for (Element* e : elements)
		if (e == &details || e == &content)
			e->render(this);
	SDL_RenderSetClipRect(RootDisplay::renderer, NULL);

	// 4. Barra inferior (mismos colores y linea que la de la lista)
	CST_Rect footerRect = { 0, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT, SCREEN_WIDTH, SIDEBAR_FOOTER_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::sidebarFooterBg);
	CST_FillRect(RootDisplay::renderer, &footerRect);
	CST_Rect footerLineRect = { 0, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT, SCREEN_WIDTH, SIDEBAR_FOOTER_LINE };
	CST_SetDrawColor(RootDisplay::renderer, FooterStyle::lineColor());
	CST_FillRect(RootDisplay::renderer, &footerLineRect);

	// 5. Botones de la barra
	for (Element* e : elements)
		if (isFooterButton(e))
			e->render(this);

	// 6. Todo lo demas (barra de progreso, avisos, captura a pantalla completa...)
	// va encima de la barra, sin recorte
	for (Element* e : elements)
		if (e != &details && e != &content && !isFooterButton(e))
			e->render(this);
#endif
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

void AppDetails::pollCancelHook()
{
	pollCancel(false);
}

void AppDetails::pollCancel(bool force)
{
	AppDetails* popup = (AppDetails*)RootDisplay::subscreen;
	if (popup == NULL || !popup->operating || popup->parent == NULL)
		return;

	int now = CST_GetTicks();
	if (!force && now - gLastPollTick < 60)
		return;
	gLastPollTick = now;

	// atender los eventos pendientes (como antes) y seguir el boton B
	InputEvents events;
	while (events.update())
	{
		if (events.pressed(B_BUTTON))
		{
			if (gBHeldSince == 0)
				gBHeldSince = now ? now : 1;
		}
		else if (events.released(B_BUTTON))
			gBHeldSince = 0;

		RootDisplay::mainDisplay->process(&events);
	}

	// cuanto lleva presionada (si se solto antes de tiempo, no pasa nada)
	int secs = -1; // -1: sin cuenta regresiva
	if (gBHeldSince != 0)
	{
		int held = now - gBHeldSince;
		if (held >= CANCEL_HOLD_MS)
			libget_cancel_request();
		else if (held >= 300)
			secs = (CANCEL_HOLD_MS - held + 999) / 1000; // 5..1
	}
	if (libget_cancelled())
		secs = 0;

	if (secs != gLastHintSecs)
	{
		gLastHintSecs = secs;
		std::string text;
		if (secs == 0)
			text = "Cancelando...";
		else if (secs > 0)
			text = "Cancelando en " + std::to_string(secs) + "  (suelta B para continuar)";
		else
			text = "Manten B 5 s para cancelar";

		popup->cancelHint.setText(text);
		popup->cancelHint.update();
		popup->cancelHint.constrain(ALIGN_CENTER_HORIZONTAL, 0);

		// que se vea aunque la operacion no este reportando progreso
		RootDisplay::mainDisplay->render(NULL);
	}
}

int AppDetails::updateCurrentlyDisplayedPopup(void* clientp, double dlnow)
{
	(void)clientp;

	// ya cancelado: devolver != 0 hace que curl (y la descarga por tramos) aborten
	if (libget_cancelled())
		return 1;

	int now = CST_GetTicks();
	int diff = now - AppDetails::lastFrameTime;

	double amount = dlnow;

	// don't update the GUI too frequently here, it slows down downloading
	// and extracting (every redraw takes real time, and while extracting thousands
	// of small files this callback is called for each one of them).
	// (never return early if it's 100% done)
	if (diff < 120 && amount != 1)
		return 0;

	// revisar B (5 s para cancelar) y atender eventos pendientes
	pollCancel(true);
	if (libget_cancelled())
		return 1;

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
			RootDisplay::mainDisplay->render(NULL);
	}

	AppDetails::lastFrameTime = CST_GetTicks();

	return libget_cancelled() ? 1 : 0;
}
