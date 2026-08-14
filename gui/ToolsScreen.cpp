#include <cstdio>
#include <dirent.h>
#include <sstream>

#include "ToolsScreen.hpp"
#include "ThemeScreen.hpp"
#include "AboutScreen.hpp"
#include "MainDisplay.hpp"
#include "ThemeManager.hpp"

#include "../libs/chesto/src/Texture.hpp"
#include "../libs/chesto/src/RootDisplay.hpp"

#define BTN_WIDTH  520
#define BTN_HEIGHT_GAP 30

ToolsScreen::ToolsScreen(Get* get)
	: get(get)
	, title("Herramientas", 32, &HBAS::ThemeManager::textPrimary)
	, hint("A: Seleccionar    B: Volver", 18, &HBAS::ThemeManager::textSecondary)
	, themesBtn("Temas", 0, false, 32, BTN_WIDTH)
	, creditsBtn("Creditos", 0, false, 32, BTN_WIDTH)
	, wipeCacheBtn("Limpieza de cache", 0, false, 32, BTN_WIDTH)
{
	this->width = SCREEN_WIDTH;
	this->height = SCREEN_HEIGHT;

	title.position(60, 40);
	this->append(&title);

	hint.position(60, SCREEN_HEIGHT - 50);
	this->append(&hint);

	int startX = 60;
	int startY = 130;
	int y = startY;

	themesBtn.position(startX, y);
	themesBtn.action = std::bind(&ToolsScreen::openThemes, this);
	this->append(&themesBtn);
	buttons.push_back(&themesBtn);
	y += themesBtn.height + BTN_HEIGHT_GAP;

	creditsBtn.position(startX, y);
	creditsBtn.action = std::bind(&ToolsScreen::openCredits, this);
	this->append(&creditsBtn);
	buttons.push_back(&creditsBtn);
	y += creditsBtn.height + BTN_HEIGHT_GAP;

	wipeCacheBtn.position(startX, y);
	wipeCacheBtn.action = std::bind(&ToolsScreen::wipeIconCache, this);
	this->append(&wipeCacheBtn);
	buttons.push_back(&wipeCacheBtn);
}

ToolsScreen::~ToolsScreen()
{
}

void ToolsScreen::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// fondo de toda la pantalla, igual que el resto de las pantallas de
	// configuracion (ThemeScreen, AboutScreen)
	CST_Rect dimens = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
	CST_FillRect(RootDisplay::renderer, &dimens);

	super::render(parent);

	// marco blanco alrededor del boton resaltado por el cursor de
	// navegacion (mismo patron que ThemeCard::cursorHere en ThemeScreen)
	if (highlighted >= 0 && highlighted < (int)buttons.size())
	{
		Button* b = buttons[highlighted];
		CST_Color cursorBorder = {0xff, 0xff, 0xff, 0xff};
		CST_SetDrawColor(RootDisplay::renderer, cursorBorder);
		for (int i = 0; i < 3; i++)
		{
			CST_Rect cursorRect = { b->x - i - 4, b->y - i - 4, b->width + (i + 4) * 2, b->height + (i + 4) * 2 };
			CST_DrawRect(RootDisplay::renderer, &cursorRect);
		}
	}
}

bool ToolsScreen::process(InputEvents* event)
{
	if (event->pressed(B_BUTTON))
	{
		this->back();
		return true;
	}

	if (event->pressed(A_BUTTON))
	{
		if (highlighted >= 0 && highlighted < (int)buttons.size() && buttons[highlighted]->action)
			buttons[highlighted]->action();
		return true;
	}

	if (event->pressed(DOWN_BUTTON))
		highlighted = (highlighted + 1) % buttons.size();
	else if (event->pressed(UP_BUTTON))
		highlighted = (highlighted - 1 + buttons.size()) % buttons.size();

	return super::process(event);
}

void ToolsScreen::back()
{
	RootDisplay::switchSubscreen(nullptr);
}

void ToolsScreen::openThemes()
{
	RootDisplay::switchSubscreen(new ThemeScreen());
}

void ToolsScreen::openCredits()
{
	RootDisplay::switchSubscreen(new AboutScreen(this->get));
}

void ToolsScreen::wipeIconCache()
{
	// borramos todos los archivos del cache de iconos en disco (el que
	// usa AppCard para no re-descargar icon.jpg en cada apertura). OJO:
	// esto NO toca el icono individual que se guarda por-paquete al
	// instalar (mPkg_path/<paquete>/icon.png) -- ese es el fallback
	// offline para paquetes instalados, y borrarlo los dejaria sin
	// icono si despues no hay internet.
	int deleted = 0;
	DIR* dir = opendir(get->mIconCache_path.c_str());
	if (dir)
	{
		struct dirent* ent;
		while ((ent = readdir(dir)) != nullptr)
		{
			std::string name = ent->d_name;
			if (name == "." || name == "..")
				continue;

			std::string fullPath = get->mIconCache_path + name;
			if (std::remove(fullPath.c_str()) == 0)
				deleted++;
		}
		closedir(dir);
	}

	// tambien limpiamos el cache de texturas en RAM -- sin esto, si en
	// esta misma sesion se vuelve a construir una tarjeta para un
	// paquete cuyo icono ya estaba cargado en memoria, seguiria
	// mostrando esa textura vieja aunque el archivo en disco ya no
	// exista mas.
	Texture::wipeEntireCache();

	std::stringstream msg;
	msg << "Se borraron " << deleted << " iconos del cache.\n\n"
	    << "Se van a volver a descargar la proxima vez\n"
	    << "que se muestren (por ejemplo, al entrar de\n"
	    << "nuevo a una categoria de la lista).";

	((MainDisplay*)RootDisplay::mainDisplay)->showFullscreenPrompt(
		"Cache de iconos vaciado",
		msg.str(),
		false
	);
}
