#pragma once

#include "../libs/get/src/Get.hpp"

#include "../libs/chesto/src/Button.hpp"
#include "../libs/chesto/src/ImageElement.hpp"
#include "../libs/chesto/src/ListElement.hpp"
#include "../libs/chesto/src/TextElement.hpp"

#include "AppCard.hpp"
#include "AppDetails.hpp"
#include "Sidebar.hpp"

#include <random>
#include <list>

#define TOTAL_SORTS 5 // alphabetical (with updates at top), downloads, last updated, size, shuffled
#define RECENT 0
#define POPULARITY 1
#define ALPHABETICAL 2
#define SIZE 3
#define RANDOM 4

#define PANE_WIDTH SCREEN_HEIGHT

class AppList : public ListElement
{
public:
	AppList(Get* get, Sidebar* sidebar);

	bool process(InputEvents* event);
	void render(Element* parent);
	void update();

	Get* get = NULL;
	Sidebar* sidebar = NULL;

	// Busqueda con el teclado del sistema (Switch): abre el teclado y filtra la lista
	void openSearch();
	// muestra los resultados de "query" (tambien lo usa "mas por este autor")
	void applySearch(const std::string& query);
	// sale de la busqueda y vuelve a la categoria anterior
	void clearSearch();
	void cycleSort();
	void reorient();
	void toggleAudio();

	// suelta todas las tarjetas (y con ellas las referencias a los paquetes); se usa
	// justo antes de reemplazar la lista de paquetes al recargar
	void clearCards();

	bool touchMode = true;
	bool needsUpdate = false;

	// the total number of apps displayed in this list
	int totalCount = 0;

	// default number of items per row TODO: save this value as config
	int R = 3;

	int sortMode = RECENT;
	bool useBannerIcons = true;

	void launchSettings(bool isCredits);
	bool hideSidebar = false;

	// posiciona el titulo/subtitulo de la seccion (arriba, fijo) y todos los
	// botones de accion (abajo, en la franja compartida con Sidebar). Se
	// llama desde update() para que ambos grupos usen siempre las mismas
	// coordenadas, sin importar cuanto haya scrolleado la lista de tarjetas.
	void positionChrome();

	// borde superior de la zona visible de las tarjetas (justo debajo del titulo)
	int listClipTop() const;

#if defined(MUSIC)
	// actualiza el texto "reproduciendo ahora" segun el estado de la musica
	void updateNowPlayingText();
#endif

private:
	bool sortCompare(const Package& left, const Package& right);
	std::random_device randDevice;


	// the title of this category (from the sidebar)
	static CST_Color black, gray, red, lighterRed;
	static std::string sortingDescriptions[TOTAL_SORTS];

	TextElement sortBlurb;
	TextElement category;
	Button quitBtn;
	Button sortBtn;
	Button keyboardBtn;
	Button backspaceBtn;
	Button reloadBtn; // "Recargar lista" (+): vuelve a descargar repo.json sin cache
	TextElement noResults; // "Sin resultados para ..." cuando una busqueda no encuentra nada
	TextElement nowPlayingText;

#if defined(MUSIC)
	ImageElement nowPlayingIcon;
	Button muteBtn;
	ImageElement muteIcon;
	ImageElement unmuteIcon;

	// color con el que estan pintados los iconos de sonido (mute/unmute);
	// se repintan cuando cambia el color de texto del tema
	CST_Color iconTint = {0, 0, 0, 0};
	bool iconTinted = false;
	void tintAudioIcons();
	Button toolsBtn;
	std::vector<std::string> musicInfo;
#endif

	// list of visible app cards
	std::list<AppCard> appCards;
};
