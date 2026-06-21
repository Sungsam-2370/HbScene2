#include "ThemeScreen.hpp"
#include "ThemeColorizeScreen.hpp"
#include "../libs/chesto/src/Constraint.hpp"

#define CARD_WIDTH  280
#define CARD_HEIGHT 160
#define CARD_GAP_X  30
#define CARD_GAP_Y  30
#define CARDS_PER_ROW 3

// ----------------------------------------------------------------------
// ThemeCard
// ----------------------------------------------------------------------
ThemeCard::ThemeCard(int themeId)
	: themeId(themeId)
	, nameText(HBAS::ThemeManager::themeNames[themeId], 24, &HBAS::ThemeManager::textPrimary, false, CARD_WIDTH - 20)
	, selectedText("Seleccionado", 18, &HBAS::ThemeManager::textPrimary, false, CARD_WIDTH - 20)
{
	this->width = CARD_WIDTH;
	this->height = CARD_HEIGHT;
	this->touchable = true;
	this->cornerRadius = 15;
	this->hasBackground = true;

	nameText.position(15, 15);
	this->append(&nameText);

	selectedText.position(15, CARD_HEIGHT - 35);
	selectedText.hidden = true;
	this->append(&selectedText);
}

void ThemeCard::render(Element* parent)
{
	// usar los colores de PREVIEW del tema que representa esta tarjeta,
	// no los colores actualmente activos en la app, para que el usuario
	// pueda ver como se ve cada tema antes de aplicarlo
	HBAS::ThemeManager::ThemeColors preview = (themeId == HBAS::ThemeManager::THEME_CUSTOM)
		? HBAS::ThemeManager::customColors
		: HBAS::ThemeManager::getPresetColors(themeId);

	this->backgroundColor = fromRGB(preview.sidebarColor.r, preview.sidebarColor.g, preview.sidebarColor.b);

	// El texto "Seleccionado" solo se muestra en el tema activo del sistema,
	// independientemente de donde este el cursor de navegacion
	selectedText.hidden = !selected;

	// El texto del nombre del tema usa un color de contraste fijo (blanco)
	// para que sea legible sin importar el color de fondo de la tarjeta
	static CST_Color white = {0xff, 0xff, 0xff, 0xff};
	nameText.setColor(white);
	selectedText.setColor(white);

	super::render(parent);

	// Borde verde fino: marca permanente del tema activo
	if (selected)
	{
		CST_Color activeBorder = {0x00, 0xff, 0x80, 0xff};
		CST_SetDrawColor(RootDisplay::renderer, activeBorder);
		CST_Rect activeBorderRect = { this->x - 2, this->y - 2, this->width + 4, this->height + 4 };
		CST_DrawRect(RootDisplay::renderer, &activeBorderRect);
	}

	// Borde blanco grueso: posicion actual del cursor de navegacion
	if (cursorHere)
	{
		CST_Color cursorBorder = {0xff, 0xff, 0xff, 0xff};
		CST_SetDrawColor(RootDisplay::renderer, cursorBorder);
		for (int i = 0; i < 3; i++)
		{
			CST_Rect cursorRect = { this->x - i - 4, this->y - i - 4, this->width + (i + 4) * 2, this->height + (i + 4) * 2 };
			CST_DrawRect(RootDisplay::renderer, &cursorRect);
		}
	}
}

bool ThemeCard::process(InputEvents* event)
{
	if (event->touchIn(this->x, this->y, this->width, this->height) && event->isTouchUp())
	{
		if (this->action) this->action();
		return true;
	}
	return super::process(event);
}

// ----------------------------------------------------------------------
// ThemeScreen
// ----------------------------------------------------------------------
ThemeScreen::ThemeScreen()
	: title("Personalizar tema", 32, &HBAS::ThemeManager::textPrimary)
	, hint("A: Seleccionar    B: Volver", 18, &HBAS::ThemeManager::textSecondary)
{
	this->width = SCREEN_WIDTH;
	this->height = SCREEN_HEIGHT;

	title.position(60, 40);
	this->append(&title);

	hint.position(60, SCREEN_HEIGHT - 50);
	this->append(&hint);

	int startX = 60;
	int startY = 110;

	for (int i = 0; i < HBAS::ThemeManager::THEME_TOTAL; i++)
	{
		ThemeCard* card = new ThemeCard(i);

		int col = i % CARDS_PER_ROW;
		int row = i / CARDS_PER_ROW;

		card->position(
			startX + col * (CARD_WIDTH + CARD_GAP_X),
			startY + row * (CARD_HEIGHT + CARD_GAP_Y)
		);

		card->action = [this, i]() {
			this->selectCard(i);
		};

		this->append(card);
		cards.push_back(card);
	}

	// marcar la tarjeta del tema actualmente activo (permanente, no cambia con el cursor)
	// y posicionar el cursor de navegacion sobre esa misma tarjeta al iniciar
	highlighted = HBAS::ThemeManager::currentTheme;
	if (highlighted < 0 || highlighted >= (int)cards.size())
		highlighted = 0;
	cards[highlighted]->selected = true;
	cards[highlighted]->cursorHere = true;
}

ThemeScreen::~ThemeScreen()
{
	for (auto card : cards)
		delete card;
}

void ThemeScreen::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// fondo de toda la pantalla
	CST_Rect dimens = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
	CST_FillRect(RootDisplay::renderer, &dimens);

	super::render(parent);
}

bool ThemeScreen::process(InputEvents* event)
{
	if (event->pressed(B_BUTTON))
	{
		this->back();
		return true;
	}

	if (event->pressed(A_BUTTON))
	{
		this->selectCard(highlighted);
		return true;
	}

	int prevHighlighted = highlighted;

	if (event->pressed(RIGHT_BUTTON))
		highlighted = (highlighted + 1) % cards.size();
	else if (event->pressed(LEFT_BUTTON))
		highlighted = (highlighted - 1 + cards.size()) % cards.size();
	else if (event->pressed(DOWN_BUTTON))
		highlighted = (highlighted + CARDS_PER_ROW) % cards.size();
	else if (event->pressed(UP_BUTTON))
		highlighted = (highlighted - CARDS_PER_ROW + cards.size()) % cards.size();

	if (prevHighlighted != highlighted)
	{
		cards[prevHighlighted]->cursorHere = false;
		cards[highlighted]->cursorHere = true;
	}

	return super::process(event);
}

void ThemeScreen::selectCard(int index)
{
	if (index < 0 || index >= (int)cards.size())
		return;

	if (index == HBAS::ThemeManager::THEME_CUSTOM)
	{
		// abrir la pantalla de personalizacion de colores
		RootDisplay::switchSubscreen(new ThemeColorizeScreen());
		return;
	}

	// aplicar el tema predefinido seleccionado y guardar la preferencia
	HBAS::ThemeManager::applyTheme(index);
	HBAS::ThemeManager::saveThemePreference();

	for (auto card : cards)
		card->selected = false;
	cards[index]->selected = true;
}

void ThemeScreen::back()
{
	RootDisplay::switchSubscreen(nullptr);
}
