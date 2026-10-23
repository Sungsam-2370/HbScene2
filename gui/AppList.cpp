#include "MainDisplay.hpp"
#include "AboutScreen.hpp"
#include "FeedbackCenter.hpp"
#include "ThemeManager.hpp"
#include "FooterStyle.hpp"
#include "ToolsScreen.hpp"
#include "main.hpp"

#include "../libs/get/src/Utils.hpp"

#include "../libs/chesto/src/EKeyboard.hpp"
#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/Constraint.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdlib> // std::rand, std::srand
#include <ctime>   // std::time

#if defined(SWITCH)
#include <switch.h>
#endif

std::string AppList::sortingDescriptions[TOTAL_SORTS] = {
	"listing.sort.recent",
	"listing.sort.downloads",
	"listing.sort.alpha",
	"listing.sort.size",
	"listing.sort.random"
 };

CST_Color AppList::red = { 0xff, 0, 0, 0xff };
CST_Color AppList::lighterRed = { 0xef, 0x48, 0x48, 0xff };

AppList::AppList(Get* get, Sidebar* sidebar)
	: get(get)			// the main get instance that contains repo info and stuff
	, sidebar(sidebar)	// the sidebar, which will store the currently selected category info
	, quitBtn(i18n("listing.quit"), SELECT_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f)
	, sortBtn(i18n("listing.adjustsort"), Y_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f)
	, keyboardBtn(i18n("listing.togglekeyboard"), Y_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f)
	, backspaceBtn(i18n("listing.delete"), B_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f)
	, nowPlayingText(" ", 16, &HBAS::ThemeManager::textPrimary)
#if defined(MUSIC)
	, nowPlayingIcon(RAMFS "res/nowplaying.png")
	, muteBtn(" ", X_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 100, 1.8f)
	, muteIcon(RAMFS "res/mute.png")
	, unmuteIcon(RAMFS "res/unmute.png")
#endif
	, toolsBtn("Herramientas", R_BUTTON, false, FOOTER_BTN_TEXT_SIZE, 0, 1.8f)
{
	this->x = 400/SCALER - 260/SCALER * hideSidebar;
	sidebar->width = this->x - 35/SCALER; // width of the sidebar is space between edge and applist

	// the offset of how far along scroll'd we are
	this->y = 0;

	// El titulo de la seccion y todos los botones de accion viven fuera del
	// flujo scrolleable: se marcan isAbsolute para que su posicion en
	// pantalla (xAbs/yAbs) se tome directo de su x/y, sin sumar el offset
	// de scroll de esta lista (this->y). Asi el titulo y los botones ya no
	// se mueven cuando se scrollean las tarjetas.
	category.isAbsolute = true;
	sortBlurb.isAbsolute = true;
	quitBtn.isAbsolute = true;
	sortBtn.isAbsolute = true;
	keyboardBtn.isAbsolute = true;
	backspaceBtn.isAbsolute = true;
	nowPlayingText.isAbsolute = true;
	toolsBtn.isAbsolute = true;
#if defined(MUSIC)
	nowPlayingIcon.isAbsolute = true;
	muteBtn.isAbsolute = true;
	muteIcon.isAbsolute = true;
	unmuteIcon.isAbsolute = true;
#endif

	// initialize random numbers used for sorting
	std::srand(unsigned(std::time(0)));

	// quit button
	quitBtn.action = []() {
		RootDisplay::mainDisplay->requestQuit();
	};

	// additional buttons
	sortBtn.action = std::bind(&AppList::cycleSort, this);

	// Abre el menu de Herramientas (Temas / Creditos / Limpieza de
	// cache). El boton fisico R tambien se maneja en
	// MainDisplay::process; el guard de subscreen evita abrir dos veces
	// si ambos detectan la pulsacion en el mismo frame.
	toolsBtn.action = [this]() {
		if (!RootDisplay::subscreen)
			RootDisplay::switchSubscreen(new ToolsScreen(this->get));
	};
	
#if defined(MUSIC)
	muteBtn.action = std::bind(&AppList::toggleAudio, this);
	muteIcon.resize(32, 32);
	unmuteIcon.resize(32, 32);
	nowPlayingIcon.resize(26, 26);
#endif

	// search buttons
	keyboardBtn.action = std::bind(&AppList::toggleKeyboard, this);

	backspaceBtn.action = [this](void) {
		this->keyboard.backspace();
	};

	// keyboard input callback
	// keyboard.hasRoundedKeys = true;
	keyboard.typeAction = std::bind(&AppList::keyboardInputCallback, this);
	keyboard.preventEnterAndTab = true;
	keyboard.width = SCREEN_HEIGHT/SCALER - 20;
	keyboard.updateSize();

	// category text
	category.setSize(28);
	category.setColorPtr(&HBAS::ThemeManager::textCard);

	// sort mode text
	sortBlurb.setSize(15);
	sortBlurb.setColorPtr(&HBAS::ThemeManager::textSecondary);

	auto myRed = HBAS::ThemeManager::isDarkMode ? lighterRed : red;

#if defined(__WIIU__)
  useBannerIcons = true;
#elif defined(SWITCH)
  // don't use banner icons if we're in applet mode
  // they use up too much memory, and a lot of users only use applet mode
  AppletType at = appletGetAppletType();
  useBannerIcons = (at == AppletType_Application || at == AppletType_SystemApplication);

  if (!useBannerIcons) {
	// applet mode, display a warning
	nowPlayingText.setText(i18n("listing.appletwarning").c_str());
	nowPlayingText.setColor(myRed);
	nowPlayingText.update();
  }
#endif

#ifdef DEBUG_BUILD
	nowPlayingText.setText(i18n("listing.debugwarning").c_str());
	nowPlayingText.constrain(ALIGN_TOP | ALIGN_LEFT, 25);
	nowPlayingText.setWrappedWidth(PANE_WIDTH + 20 / SCALER);
	nowPlayingText.setColor(myRed);
	nowPlayingText.update();
#endif

	// update current app listing
	update();
}

bool AppList::process(InputEvents* event)
{
	bool ret = false;

	// R is the number of cards per row, let's figure it out based on app card size
	// and screen size
	R = (SCREEN_WIDTH - 400) / 260 + hideSidebar;

	if (event->pressed(ZL_BUTTON) || event->pressed(L_BUTTON))
	{
		hideSidebar = !hideSidebar;
		R = (SCREEN_WIDTH - 400) / 260 + hideSidebar;
		this->x = 400/SCALER - 260/SCALER * hideSidebar;
		sidebar->width = this->x - 35/SCALER; // width of the sidebar is space between edge and applist

		// El indicador L de la franja inferior ya no desaparece por
		// completo: Sidebar::setCollapsed() se encarga de dejar el icono
		// visible (contraido o no) y de mostrar/ocultar solo el texto.
		sidebar->setCollapsed(hideSidebar);

		update();
		return true;
	}

	// must be done before keyboard stuff to properly switch modes
	if (event->isTouchDown())
	{
		// remove a highlight if it exists (TODO: same as an above if statement)
		if (this->highlighted >= 0 && this->highlighted < (int)this->elements.size() && this->elements[this->highlighted])
			this->elements[this->highlighted]->elasticCounter = NO_HIGHLIGHT;

		// got a touch, so let's enter touchmode
		this->highlighted = -1;
		this->touchMode = true;
	}

	// if we're showing a keyboard, make sure we're not in its bounds
	// also make sure the children elements exist before trying the keyboard
	// AND we're actually on the search category
	// also if we're not in touchmode, always go in here regardless of any button presses (user can only interact with keyboard)
	bool keyboardIsShowing = sidebar && sidebar->searchModeActive && !keyboard.hidden;
	if (keyboardIsShowing && ((event->isTouchDown() && event->touchIn(keyboard.x, keyboard.y, keyboard.width + 305, keyboard.height + 200)) || !touchMode))
	{
		// wow I'm surprised this still works with the chesto keyboard
		ret |= keyboard.process(event);
		if (event->isKeyDown() && (event->held(Y_BUTTON) || event->held(B_BUTTON)))
			ret |= ListElement::process(event); // continue processing ONLY if they're pressing Y or B
		else if (event->noop)
			ret |= ListElement::process(event); // continue processing if they're not pressing anything
		
    if (needsUpdate) update();
    return ret;
	}

	int origHighlight = this->highlighted;
	auto mainDisplay = (MainDisplay*)RootDisplay::mainDisplay;

	// process some joycon input events
	if (event->isKeyDown())
	{
		if (keyboardIsShowing && !event->held(B_BUTTON))
		{
			// keyboard is showing, but we'r epressing buttons, and we're down here, so set touch mode and get out
			touchMode = false;
			if (event->held(Y_BUTTON)) // again, only let a Y through to toggle keyboard (TODO: redo this!)
				ret |= ListElement::process(event);
			return true; // short circuit, should be handled by someone else
		}

		if (event->held(A_BUTTON | B_BUTTON | UP_BUTTON | DOWN_BUTTON | LEFT_BUTTON | RIGHT_BUTTON))
		{
			auto currentSelected = this->highlighted;

			// if we were in touch mode, draw the cursor in the applist
			// and reset our position
			if (this->touchMode)
			{
				this->touchMode = false;
				this->highlighted = 0;
				this->y = 0;		 // reset scroll TODO: maintain scroll when switching back from touch mode
				event->keyCode = -1; // we already have the cursor where we want it, no further updates
				ret |= true;
			}

			if (event->held(A_BUTTON) && this->highlighted >= 0)
			{
				this->elements[this->highlighted]->action();
				ret |= true;
			}

			// touchmode is false, but our highlight value is negative
			// (do nothing, let sidebar update our highlight value)
			if (this->highlighted < 0) return false;

			// look up whatever is currently chosen as the highlighted position
			// and remove its highlight
			if (this->elements[this->highlighted])
				this->elements[this->highlighted]->elasticCounter = NO_HIGHLIGHT;

			// if we got a LEFT key while on the left most edge already, transfer to categories
			if (this->highlighted % R == 0 && event->held(LEFT_BUTTON))
			{
				this->highlighted = -1;
				this->sidebar->highlighted = this->sidebar->curCategory;
				mainDisplay->playSFX();
				return true;
			}

			// similarly, prevent a RIGHT from wrapping to the next line
			if (this->highlighted % R == (R - 1) && event->held(RIGHT_BUTTON)) return false;

			// adjust the cursor by 1 for left or right
			this->highlighted += -1 * (event->held(LEFT_BUTTON)) + (event->held(RIGHT_BUTTON));

			// adjust it by R for up and down
			this->highlighted += -1 * R * (event->held(UP_BUTTON)) + R * (event->held(DOWN_BUTTON));

			// don't let the cursor go out of bounds
			if (this->highlighted >= (int)this->elements.size()) this->highlighted = this->elements.size() - 1;

			if (this->highlighted < 0) this->highlighted = 0;
			if (this->highlighted >= (int)this->totalCount) this->highlighted = this->totalCount - 1;

			if (currentSelected != this->highlighted) {
				// we moved the cursor, so play a sound
				mainDisplay->playSFX();
			}
		}
	}

	// always check the currently highlighted piece and try to give it a thick border or adjust the screen
	if (!touchMode && (int)this->elements.size() > this->highlighted && this->highlighted >= 0 && this->elements[this->highlighted])
	{
		// if our highlighted position is large enough, force scroll the screen so that our cursor stays on screen
		Element* curTile = this->elements[this->highlighted];

		// the y-position of the currently highlighted tile, precisely on them screen (accounting for scroll)
		// this means that if it's < 0 or > SCREEN_HEIGHT then it's not visible
		int normalizedY = curTile->y + this->y;

		// zona realmente visible de las tarjetas: entre el titulo (arriba) y la
		// barra inferior (abajo). Antes se usaba la pantalla completa y el
		// cursor podia quedar con el nombre/version detras de la barra.
		const int visibleTop = listClipTop();
		const int visibleBottom = SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT;

		// if we're FAR out of range upwards, speed up the scroll wheel (additive) to get back in range quicker
		if (normalizedY < -200)
			event->wheelScroll += 0.3;

		// far out of range, for bottom of screen
		else if (normalizedY > visibleBottom - curTile->height + 200)
			event->wheelScroll -= 0.3;

		// if we're slightly out of range above, recenter at the top row slowly
		else if (normalizedY < visibleTop + 10)
			event->wheelScroll = 1;

		// special case, scroll when we're on the bottom row of the top of the not-yet-scrolled screen
		else if (this->y == 0 && normalizedY > SCREEN_HEIGHT/2)
			event->wheelScroll -= 0.5;

		// if we're out of range below, recenter at bottom row
		else if (normalizedY + curTile->height > visibleBottom - 20)
			event->wheelScroll = -1;

		// if the card is this close to the top, just set it the list offset to 0 to scroll up to the top
		else if (this->y != 0 && this->highlighted < R)
			event->wheelScroll = 1;

		if (this->elements[this->highlighted] && this->elements[this->highlighted]->elasticCounter == NO_HIGHLIGHT)
		{
			this->elements[this->highlighted]->elasticCounter = THICK_HIGHLIGHT;
			ret |= true;
		}
	}

	// highlight was modified, we need to redraw
	if (origHighlight != this->highlighted)
		ret |= true;

	ret |= ListElement::process(event);

	if (needsUpdate)
		update();

	return ret;
}

int AppList::listClipTop() const
{
	return std::max(0, std::max(category.y + category.height,
	                            sortBlurb.y + sortBlurb.height) + 8/SCALER);
}

void AppList::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// sea cual sea la via por la que se movio la lista (arrastre, inercia,
	// cursor, rueda), al soltar siempre queda dentro de los limites
	if (!this->dragging)
		clampScroll();

	// necesario para que this->xAbs/yAbs esten al dia (antes lo hacia
	// super::render() por nosotros); los hijos lo usan como base para
	// calcular su propia posicion en pantalla
	this->recalcPosition(parent);

	// draw a white background, screen dims wide
	CST_Rect dimens = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	dimens.x = this->x - 35;

  if (parent != NULL) {
    CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
    CST_FillRect(RootDisplay::renderer, &dimens);
  }

#if defined(_3DS) || defined(_3DS_MOCK)
	// 3DS no tiene sidebar ni franja inferior: se renderiza todo en un
	// solo pase, igual que antes (recalcPosition ya se hizo mas arriba)
	for (Element* subelement : elements)
		subelement->render(this);
#else
	// Las tarjetas (todo lo que NO es "chrome" fijo: botones/titulo) se
	// recortan para que nunca dibujen encima de la franja inferior al
	// scrollear. El chrome se renderiza aparte, despues, sin recorte y
	// por encima de la franja - por eso nunca queda tapado.
	// Arriba el recorte termina justo debajo del titulo de la seccion, para que
	// las tarjetas que suben al scrollear desaparezcan antes de llegar al texto.
	const int clipTop = listClipTop();
	CST_Rect clip = { dimens.x, clipTop, SCREEN_WIDTH - dimens.x, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT - clipTop };
	SDL_RenderSetClipRect(RootDisplay::renderer, &clip);
	for (Element* subelement : elements)
		if (!subelement->isAbsolute)
			subelement->render(this);
	SDL_RenderSetClipRect(RootDisplay::renderer, NULL);

	// Porcion derecha de la franja inferior compartida (de aqui hasta el
	// borde de la pantalla). Sidebar dibuja la porcion izquierda con el
	// mismo color y alto (SIDEBAR_FOOTER_HEIGHT); juntas cruzan toda la
	// pantalla sin importar si el menu lateral esta contraido o no.
	CST_Rect footerRect = { dimens.x, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT, SCREEN_WIDTH - dimens.x, SIDEBAR_FOOTER_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::sidebarFooterBg);
	CST_FillRect(RootDisplay::renderer, &footerRect);

	// linea de color en el borde superior de la barra
	CST_Rect footerLineRect = { dimens.x, SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT, SCREEN_WIDTH - dimens.x, SIDEBAR_FOOTER_LINE };
	CST_SetDrawColor(RootDisplay::renderer, FooterStyle::lineColor());
	CST_FillRect(RootDisplay::renderer, &footerLineRect);

	// chrome (titulo arriba + botones abajo): siempre visible, encima de todo
	for (Element* subelement : elements)
		if (subelement->isAbsolute)
			subelement->render(this);
#endif
}

bool AppList::sortCompare(const Package& left, const Package& right)
{
	// handle the supported sorting modes
	switch (sortMode)
	{
		case ALPHABETICAL:
			return left.getTitle().compare(right.getTitle()) < 0;
		case POPULARITY:
			return left.getDownloadCount() > right.getDownloadCount();
		case SIZE:
			return left.getDownloadSize() > right.getDownloadSize();
		case RECENT:
			break;
		default:
			break;
	}

	// RECENT sort order is the default view, so it puts updates and installed apps first
	auto statusPriority = [](int status)->int
	{
		switch (status)
		{
			case UPDATE:	return 0;
			case INSTALLED:	return 1;
			case LOCAL:		return 2;
			case GET:		return 3;
		}
		return 4;
	};
	int priorityLeft = statusPriority(left.getStatus());
	int priorityRight = statusPriority(right.getStatus());

	if (priorityLeft == priorityRight)
		return left.getUpdatedAtTimestamp() > right.getUpdatedAtTimestamp();

	return priorityLeft < priorityRight;
}

void AppList::update()
{
	if (!get)
		return;

#if defined(_3DS) || defined(_3DS_MOCK)
  R = 3;  // force 3 app cards at time
  this->x = 45; // no sidebar
#endif
	// remove elements
	super::removeAll();

	// destroy old elements
	appCards.clear();

	// the current category value from the sidebar
	std::string curCategoryValue = sidebar->currentCatValue();

	// all packages TODO: move some of this filtering logic into main get library
	// if it's a search, do a search query through get rather than using all packages
	auto packages = (sidebar->searchModeActive)
		? get->search(sidebar->searchQuery)
		: get->list();

	// sort the packages
	if (sortMode == RANDOM)
		std::shuffle(packages.begin(), packages.end(), randDevice);
	else
		std::sort(packages.begin(), packages.end(), std::bind(&AppList::sortCompare, this, std::placeholders::_1, std::placeholders::_2));

	// "Novedades" is a special category: instead of filtering by a category name,
	// it shows only the last N packages added/updated (across all categories),
	// selected by update date (descending) and, as a tiebreaker, by the package's
	// "name" field (ascending, alphabetical). This selection is independent of
	// whichever sortMode the user currently has active.
	constexpr size_t NOVEDADES_COUNT = 9;
	std::vector<Package> novedadesPackages;
	if (curCategoryValue == "_novedades")
	{
		novedadesPackages = get->list();

		// hide themes from novedades (same as "Todos los aportes"), and do it
		// before picking the top N so we don't end up showing fewer than N items
		novedadesPackages.erase(
			std::remove_if(novedadesPackages.begin(), novedadesPackages.end(), [](const Package& pkg) {
				return pkg.getCategory() == "theme";
			}),
			novedadesPackages.end()
		);

		std::sort(novedadesPackages.begin(), novedadesPackages.end(), [](const Package& left, const Package& right) {
			if (left.getUpdatedAtTimestamp() != right.getUpdatedAtTimestamp())
				return left.getUpdatedAtTimestamp() > right.getUpdatedAtTimestamp();
			return left.getPackageName() < right.getPackageName();
		});

		if (novedadesPackages.size() > NOVEDADES_COUNT)
			novedadesPackages.erase(novedadesPackages.begin() + NOVEDADES_COUNT, novedadesPackages.end());
	}

	// use the curated "Novedades" list when applicable, otherwise the regular (sorted) package list
	auto& displayPackages = (curCategoryValue == "_novedades") ? novedadesPackages : packages;

	// add AppCards for the packages belonging to the current category
	for (auto &package : displayPackages)
	{
		if (curCategoryValue == "_misc")
		{
			// if we're on misc, filter out packages belonging to some category
			if (std::find(std::begin(sidebar->cat_value), std::end(sidebar->cat_value), package.getCategory()) != std::end(sidebar->cat_value))
				continue;
		}
		else if (curCategoryValue == "_misjuegos")
		{
			// "Mis juegos": solo lo que el usuario ya tiene instalado
			// (incluye los que tienen una actualizacion pendiente)
			if (package.getStatus() != INSTALLED && package.getStatus() != UPDATE)
				continue;
		}
		else if (!sidebar->searchModeActive && curCategoryValue != "_all" && curCategoryValue != "_novedades")
		{
			// if we're in a specific category, filter out package of different categories
			if (curCategoryValue != package.getCategory())
				continue;
		}

		if (curCategoryValue == "_all" || curCategoryValue == "_novedades")
		{
			// hide themes from all / novedades
			if (package.getCategory() == "theme")
			continue;
		}

		// create and position the AppCard for the package
		appCards.emplace_back(package, this);
		AppCard& card = appCards.back();
		card.index = appCards.size() - 1;
		card.position(25 + (card.index % R) * (card.width + 9 / SCALER), 145 + (card.height + 15) * (card.index / R));
		card.update();
		super::append(&card);

		// las tarjetas que ya quedan a la vista cargan su icono ahora (asi no
		// hay un parpadeo con el icono generico); las demas esperan a estar
		// cerca de la pantalla, ver AppCard::handleIconLoad
		card.preloadIfVisible();
	}
	totalCount = appCards.size();

	// Limite inferior del scroll: la lista solo baja hasta que la ultima fila
	// de tarjetas queda completa justo encima de la barra inferior (antes la
	// barra tapaba parte de las ultimas y no habia tope). Si todo cabe en
	// pantalla no se scrollea.
	{
		int contentBottom = 0;
		if (!appCards.empty())
		{
			// el fondo real de la tarjeta es lo mas bajo entre su alto y sus
			// elementos (icono, nombre, version, estado); asi el texto de abajo
			// nunca queda mas abajo de lo que se calcula
			const AppCard& last = appCards.back();
			int cardBottom = last.height;
			for (const Element* child : last.elements)
				cardBottom = std::max(cardBottom, child->y + child->height);
			contentBottom = last.y + cardBottom + 70/SCALER; // aire extra para ver completo nombre/version
		}
		const int visibleBottom = SCREEN_HEIGHT - SIDEBAR_FOOTER_HEIGHT;
		this->lowerYScroll = std::min(0, visibleBottom - contentBottom);
		clampScroll();
	}

	// add quit button
#if defined(_3DS) || defined(_3DS_MOCK)
	quitBtn.position(SCREEN_WIDTH - quitBtn.width - 5, 20);
#else
	super::append(&quitBtn);
#endif

	// update the view for the current category
	if (sidebar->searchModeActive)
	{
		// add the keyboard
		super::append(&keyboardBtn);
		keyboard.position(372 + (3 - R) * 132, 417);
		super::append(&keyboard);

		// category text (busqueda activa)
		category.setText(std::string(i18n("listing.search") + " \"") + sidebar->searchQuery + "\"");
		category.update();
		super::append(&category);

		super::append(&backspaceBtn);
	}
	else
	{
		// add additional buttons
		super::append(&sortBtn);

#if defined(MUSIC)
		auto rootDisplay = RootDisplay::mainDisplay;

		if (rootDisplay->music) {
			super::append(&muteBtn);
			Mix_PausedMusic() ? super::append(&muteIcon) : super::append(&unmuteIcon);
			super::append(&toolsBtn);
		} else {
			super::append(&toolsBtn);
		}
#else
		super::append(&toolsBtn);
#endif

		// category text
		category.setText(sidebar->currentCatName());
		category.update();
		super::append(&category);

		// add the search type next to the category in a gray font
		sortBlurb.setText(i18n(sortingDescriptions[sortMode]).c_str());
		sortBlurb.update();
		super::append(&sortBlurb);
	}

	auto rootDisplay = RootDisplay::mainDisplay;

#if defined(MUSIC)
	updateNowPlayingText();

	// now playing text (or applet warning)
	super::append(&nowPlayingText);
#endif

	// Con todos los botones/elementos ya agregados y con su texto final
	// (el ancho del texto puede haber cambiado), se calculan sus posiciones
	// definitivas: titulo arriba fijo, botones abajo en la franja compartida.
	positionChrome();

	needsUpdate = false;
}

#if defined(MUSIC)
void AppList::tintAudioIcons()
{
	// los iconos de sonido son de un solo color (oscuro); se pintan con el
	// mismo color del texto del tema para que se lean sobre la barra
	// (ahora en gris fijo, mas discreto, para no competir con el boton "X")
	const CST_Color c = { 150, 150, 150, 255 };
	if (iconTinted)
		return;

	muteIcon.loadTinted(RAMFS "res/mute.png", c);
	unmuteIcon.loadTinted(RAMFS "res/unmute.png", c);
	muteIcon.resize(32, 32);
	unmuteIcon.resize(32, 32);

	iconTint = c;
	iconTinted = true;
}

void AppList::updateNowPlayingText()
{
	auto rootDisplay = RootDisplay::mainDisplay;

	if (rootDisplay->music) {
		if (!Mix_PausedMusic()) {
			// music is playing, get the title and artist 
			if (this->musicInfo.size() == 0)
				this->musicInfo = CST_GetMusicInfo(rootDisplay->music);
			const char* title = musicInfo[0].c_str();
			const char* artist = musicInfo[1].c_str();
			const char* album = musicInfo[2].c_str();
			
			// now playing icon, and position
			#if !defined(DEBUG_BUILD)
			nowPlayingText.setText(
				std::string("") + title +
				((artist != std::string("")) ? (std::string(" " + i18n("listing.by") + " ") + artist) : "") +
				((album != std::string("")) ? (std::string(" - ") + album) : "")
			);
			#endif
		} else {
			// no music playing, stop showing music name
			#if !defined(DEBUG_BUILD)
			nowPlayingText.setText(" ");
			#endif
		}
	}

	nowPlayingText.update();
}
#endif

void AppList::positionChrome()
{
	// --- Titulo de la seccion: arriba, fijo, no scrollea con las tarjetas ---
	const int titleX = this->x + 20/SCALER;
	const int titleY = 40/SCALER;
	category.position(titleX, titleY);
	sortBlurb.position(category.x + category.width + 15/SCALER, category.y + 12/SCALER);

#if defined(_3DS) || defined(_3DS_MOCK)
	return; // 3DS no tiene sidebar ni franja inferior (quitBtn ya se posiciono aparte)
#else
	// --- Botones de accion: abajo, en la franja compartida con Sidebar ---
	// centrados en la parte de la barra que queda debajo de la linea superior
	const int footerY = SCREEN_HEIGHT - (SIDEBAR_FOOTER_HEIGHT - SIDEBAR_FOOTER_LINE) / 2 - quitBtn.height / 2;
	const int gap = 10/SCALER;

	// botones sin recuadro: solo icono + texto sobre el fondo de la barra,
	// con colores claros u oscuros segun el fondo del tema activo
	{
		// el texto usa EXACTAMENTE el mismo color que el texto del menu lateral
		// (textPrimary, por puntero: sigue los cambios de tema); los iconos del
		// control se eligen claros u oscuros segun ese mismo color
		const bool light = FooterStyle::iconsLight();
		CST_Color* textColor = &HBAS::ThemeManager::textPrimary;
		quitBtn.setFlat(true, light, textColor);
		sortBtn.setFlat(true, light, textColor);
		keyboardBtn.setFlat(true, light, textColor);
		backspaceBtn.setFlat(true, light, textColor);
		toolsBtn.setFlat(true, light, textColor);
#if defined(MUSIC)
		muteBtn.setFlat(true, light, textColor);
		tintAudioIcons();
#endif
	}

	// Salir queda fijo en el extremo derecho de la pantalla
	quitBtn.position(SCREEN_WIDTH - quitBtn.width - 20/SCALER, footerY);

	// el resto se acomoda de izquierda a derecha, empezando justo despues
	// de la porcion del footer que dibuja Sidebar
	int nextX = sidebar->width + 20/SCALER;

	if (sidebar->searchModeActive)
	{
		keyboardBtn.position(nextX, footerY);
		nextX = keyboardBtn.x + keyboardBtn.width + gap;

		backspaceBtn.position(nextX, footerY);
	}
	else
	{
		toolsBtn.position(nextX, footerY);
		nextX = toolsBtn.x + toolsBtn.width + gap;

		sortBtn.position(nextX, footerY);
		nextX = sortBtn.x + sortBtn.width + gap;

#if defined(MUSIC)
		if (RootDisplay::mainDisplay->music) {
			muteBtn.position(nextX, footerY);
			// el icono de musica (altavoz) va DENTRO del boton, alineado a
			// su derecha, junto al icono del boton fisico (que queda a la
			// izquierda) - asi no queda "flotando" fuera del recuadro
			int iconY = muteBtn.y + (muteBtn.height - muteIcon.height) / 2;
			int iconX = muteBtn.x + muteBtn.width - muteIcon.width - 10/SCALER;
			muteIcon.position(iconX, iconY);
			unmuteIcon.position(iconX, iconY);
			nextX = muteBtn.x + muteBtn.width + gap;
		}
#endif
	}

#if defined(MUSIC)
	// "reproduciendo ahora" (o el aviso de modo applet) va despues de los
	// demas botones, sin invadir el area de Salir
	nowPlayingText.position(nextX, footerY + (quitBtn.height - nowPlayingText.height) / 2);
	nowPlayingIcon.position(nowPlayingText.x - nowPlayingIcon.width - 5/SCALER, nowPlayingText.y);
#endif
#endif
}

void AppList::reorient()
{
	// remove a highilight if it exists (TODO: extract method, we use this everywehre)
	if (this->highlighted >= 0 && this->highlighted < (int)this->elements.size() && this->elements[this->highlighted])
		this->elements[this->highlighted]->elasticCounter = NO_HIGHLIGHT;
}

void AppList::keyboardInputCallback()
{
	sidebar->searchQuery = keyboard.getTextInput();
	this->y = 0;
	needsUpdate = true;
}

void AppList::cycleSort()
{
	reorient();
	sortMode = (sortMode + 1) % TOTAL_SORTS;
	update();
}

void AppList::toggleAudio()
{
#if defined(MUSIC)

	if (Mix_PausedMusic()) {
		Mix_ResumeMusic();
	} else {
		Mix_PauseMusic();
	}

	auto isMusicAllowedByDefault = ((MainDisplay*)RootDisplay::mainDisplay)->getDefaultAudioStateForPlatform();
	auto isMusicPlayingCurrently = !Mix_PausedMusic();
	bool isSoundSettingDefaultBehavior = isMusicAllowedByDefault == isMusicPlayingCurrently; // music allowed + paused == inverted
	
	if (isSoundSettingDefaultBehavior) {
		// the default sound behavior is desired, so delete the toggle file
		if (std::filesystem::exists(SOUND_PATH)) {
			std::filesystem::remove(SOUND_PATH);
		}
	} else {
		// the sound behavior is toggled, create the toggle file
		std::ofstream soundFile(SOUND_PATH);
		soundFile.flush();
	}

	// Solo se refresca lo que cambia (icono de sonido + texto "reproduciendo").
	// Antes se llamaba a update(), que reconstruye TODAS las tarjetas de
	// componentes (ordenar, crear, posicionar...) y por eso el boton tardaba
	// en responder.
	super::remove(&muteIcon);
	super::remove(&unmuteIcon);
	if (RootDisplay::mainDisplay->music)
		Mix_PausedMusic() ? super::append(&muteIcon) : super::append(&unmuteIcon);
	updateNowPlayingText();
	positionChrome();
#endif
}

void AppList::toggleKeyboard()
{
	reorient();
	keyboard.hidden = !keyboard.hidden;

	// if it's hidden now, make sure we release our highlight
	if (keyboard.hidden)
	{
		sidebar->highlighted = -1;
		highlighted = 0;
	}

	needsRedraw = true;
}

void AppList::launchSettings(bool isCredits)
{
	(void)isCredits;
	// if (isCredits) {
		RootDisplay::switchSubscreen(new AboutScreen(this->get));
	// } else {
	// 	RootDisplay::switchSubscreen(new FeedbackCenter(this));
	// }
}
