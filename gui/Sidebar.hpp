#include "../libs/chesto/src/ImageElement.hpp"
#include "../libs/chesto/src/ListElement.hpp"
#include "../libs/chesto/src/TextElement.hpp"

class AppList;

#if defined(WII) || defined(WII_MOCK)
#define USE_OSC_BRANDING 1
#endif

#if defined(USE_OSC_BRANDING)
#define TOTAL_CATS 6
#else
#define TOTAL_CATS 10
#endif
#pragma once

class Sidebar : public ListElement
{
public:
	Sidebar();
	~Sidebar();

	std::string currentCatName();
	std::string currentCatValue();

	void addHints();
	void updateCategoryScroll();

	std::string searchQuery = "";

	// true cuando se esta mostrando resultados de busqueda (usado por
	// AppDetails::moreByAuthor()). Ya NO depende de que exista una
	// categoria "Buscar" visible en el menu: es un estado independiente,
	// asi se puede seguir usando "mas por este autor" aunque el usuario
	// no tenga forma de entrar a busqueda manualmente desde el sidebar.
	bool searchModeActive = false;

	AppList* appList = NULL;

	void render(Element* parent);
	bool process(InputEvents* event);

	int currentSelection = -1;

	bool showCurrentCategory = false;

	// the currently selected category index
	int curCategory = 0; // 0 es "Novedades" (primera categoria, se muestra al iniciar)

	// vertical scroll offset for the category list (in pixels, scaled)
	int scrollOffset = 0;

	// list of human-readable category titles and short names from the json
#ifdef USE_OSC_BRANDING
	const char* cat_names[TOTAL_CATS] = { "sidebar.search", "sidebar.all", "sidebar.utilities", "sidebar.emulators", "sidebar.games", "sidebar.media" };
	const char* cat_value[TOTAL_CATS] = { "_search", "_all", "utilities", "emulators", "games", "media" };
#else
	const char* cat_names[TOTAL_CATS] = { "Novedades", "sidebar.all", "N64 Nativos", "NSO Juegos Extra", "Traducciones", "Emuladores", "Ports Juegos", "Mods Juegos", "PkUnico", "sidebar.misc" };
	const char* cat_value[TOTAL_CATS] = { "_novedades", "_all", "N64_Nativos", "NSO", "Traducciones", "Emuladores", "Ports", "Mods Juegos", "PkUnico", "_misc" };
#endif

	ImageElement* hider = nullptr;
	TextElement* hint = nullptr;

private:
	struct
	{
		ImageElement* icon;
		TextElement* name;
	} category[TOTAL_CATS];

	ImageElement logo;
	TextElement title;
	TextElement subtitle;

	// "Gracias por tu apoyo" — solo se crea/muestra si gIsSupporter es true
	// (ver SupporterBenefit.hpp). Sirve como confirmacion visual de que la
	// consola fue reconocida como beneficiaria.
	TextElement* supporterBadge = nullptr;
	CST_Color supporterBadgeColor = { 0xFF, 0xD7, 0x00, 0xff }; // dorado
};

#if defined(USE_OSC_BRANDING)
	rgb getOSCCategoryColor(std::string category);
#endif
