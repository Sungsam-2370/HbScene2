#include "ThemeColorizeScreen.hpp"
#include "ThemeScreen.hpp"
#include "../libs/chesto/src/Constraint.hpp"
#include <sstream>

#define ROW_HEIGHT 50
#define ROW_START_Y 110
#define ROW_X 60
#define VALUE_X 600
#define PREVIEW_X 780

ThemeColorizeScreen::ThemeColorizeScreen()
	: title("Personalizar colores", 32, &HBAS::ThemeManager::textPrimary)
	, hint("Izq/Der: ajustar valor   Arriba/Abajo: cambiar fila   L/R: cambiar color (R,G,B,A)   A: aplicar   B: volver", 16, &HBAS::ThemeManager::textSecondary)
{
	this->width = SCREEN_WIDTH;
	this->height = SCREEN_HEIGHT;

	title.position(60, 30);
	this->append(&title);

	hint.position(60, SCREEN_HEIGHT - 40);
	this->append(&hint);

	// Lista de todos los colores editables del tema personalizado
	entries = {
		{ &HBAS::ThemeManager::customColors.background,       "Fondo general" },
		{ &HBAS::ThemeManager::customColors.textPrimary,      "Texto principal" },
		{ &HBAS::ThemeManager::customColors.textSecondary,    "Texto secundario" },
		{ &HBAS::ThemeManager::customColors.sidebarColor,     "Fondo de menus laterales" },
		{ &HBAS::ThemeManager::customColors.sidebarTitleBg,   "Sidebar: seccion titulo" },
		{ &HBAS::ThemeManager::customColors.sidebarCategoryBg,"Sidebar: seccion categorias" },
		{ &HBAS::ThemeManager::customColors.sidebarFooterBg,  "Sidebar: seccion inferior" },
		{ &HBAS::ThemeManager::customColors.categoryHighlight,"Categoria seleccionada" },
		{ &HBAS::ThemeManager::customColors.dragHighlight,    "Recuadro de movimiento" },
	};

	for (size_t i = 0; i < entries.size(); i++)
	{
		int y = ROW_START_Y + (int)i * ROW_HEIGHT;

		TextElement* label = new TextElement(entries[i].label.c_str(), 20, &HBAS::ThemeManager::textPrimary, false, 480);
		label->position(ROW_X, y);
		this->append(label);
		rowLabels.push_back(label);

		TextElement* value = new TextElement("", 20, &HBAS::ThemeManager::textPrimary, false, 160);
		value->position(VALUE_X, y);
		this->append(value);
		rowValues.push_back(value);
	}

	applyButton = new Button("Aplicar y guardar", A_BUTTON, true, 22, 280);
	applyButton->position(60, ROW_START_Y + (int)entries.size() * ROW_HEIGHT + 30);
	applyButton->action = std::bind(&ThemeColorizeScreen::applyAndSave, this);
	this->append(applyButton);

	for (size_t i = 0; i < entries.size(); i++)
		refreshRowText((int)i);
}

ThemeColorizeScreen::~ThemeColorizeScreen()
{
	for (auto label : rowLabels) delete label;
	for (auto value : rowValues) delete value;
	if (applyButton) delete applyButton;
}

void ThemeColorizeScreen::refreshRowText(int row)
{
	CST_Color* c = entries[row].color;

	std::stringstream ss;
	ss << "R:" << (int)c->r << " G:" << (int)c->g << " B:" << (int)c->b << " A:" << (int)c->a;
	rowValues[row]->setText(ss.str());
	rowValues[row]->update();
}

void ThemeColorizeScreen::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// fondo de toda la pantalla, usando el color de fondo activo actualmente
	// (que mientras se edita, es el del tema personalizado en vivo)
	CST_Rect dimens = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
	CST_FillRect(RootDisplay::renderer, &dimens);

	super::render(parent);

	// resaltar la fila seleccionada con un marco
	int y = ROW_START_Y + selectedRow * ROW_HEIGHT - 5;
	CST_Color highlightColor = {0xff, 0xd9, 0x00, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, highlightColor);
	CST_Rect rowRect = { ROW_X - 10, y, 900, 35 };
	CST_DrawRect(RootDisplay::renderer, &rowRect);

	// resaltar el componente seleccionado (R/G/B/A) dentro del texto de valor
	// dibujando un pequeño indicador encima de la letra correspondiente
	int compX = VALUE_X + selectedComponent * 40;
	CST_Rect compRect = { compX, y, 36, 30 };
	CST_DrawRect(RootDisplay::renderer, &compRect);

	// dibujar la vista previa de color de cada fila
	for (size_t i = 0; i < entries.size(); i++)
		drawColorPreview((int)i, ROW_START_Y + (int)i * ROW_HEIGHT);
}

void ThemeColorizeScreen::drawColorPreview(int row, int rowY)
{
	CST_Color* c = entries[row].color;
	CST_SetDrawColor(RootDisplay::renderer, *c);
	CST_Rect previewRect = { PREVIEW_X, rowY, 60, 30 };
	CST_FillRect(RootDisplay::renderer, &previewRect);

	CST_Color border = {0x00, 0x00, 0x00, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, border);
	CST_DrawRect(RootDisplay::renderer, &previewRect);
}

bool ThemeColorizeScreen::process(InputEvents* event)
{
	if (event->pressed(B_BUTTON))
	{
		this->back();
		return true;
	}

	if (event->pressed(DOWN_BUTTON))
	{
		selectedRow = (selectedRow + 1) % (int)entries.size();
		return true;
	}
	if (event->pressed(UP_BUTTON))
	{
		selectedRow = (selectedRow - 1 + (int)entries.size()) % (int)entries.size();
		return true;
	}

	// L/R cambian entre los 4 componentes: R, G, B, A
	if (event->pressed(R_BUTTON))
	{
		selectedComponent = (selectedComponent + 1) % 4;
		return true;
	}
	if (event->pressed(L_BUTTON))
	{
		selectedComponent = (selectedComponent - 1 + 4) % 4;
		return true;
	}

	if (event->pressed(RIGHT_BUTTON))
	{
		adjustValue(5);
		return true;
	}
	if (event->pressed(LEFT_BUTTON))
	{
		adjustValue(-5);
		return true;
	}

	return super::process(event);
}

void ThemeColorizeScreen::adjustValue(int delta)
{
	CST_Color* c = entries[selectedRow].color;

	int current = 0;
	switch (selectedComponent)
	{
	case 0: current = c->r; break;
	case 1: current = c->g; break;
	case 2: current = c->b; break;
	case 3: current = c->a; break;
	}

	current += delta;
	if (current < 0) current = 0;
	if (current > 255) current = 255;

	switch (selectedComponent)
	{
	case 0: c->r = (uint8_t)current; break;
	case 1: c->g = (uint8_t)current; break;
	case 2: c->b = (uint8_t)current; break;
	case 3: c->a = (uint8_t)current; break;
	}

	refreshRowText(selectedRow);

	// aplicar en vivo para que el usuario vea el resultado inmediatamente
	// en el fondo de esta misma pantalla y en el resto de la app
	HBAS::ThemeManager::currentTheme = HBAS::ThemeManager::THEME_CUSTOM;
	HBAS::ThemeManager::applyTheme(HBAS::ThemeManager::THEME_CUSTOM);
}

void ThemeColorizeScreen::applyAndSave()
{
	HBAS::ThemeManager::currentTheme = HBAS::ThemeManager::THEME_CUSTOM;
	HBAS::ThemeManager::applyTheme(HBAS::ThemeManager::THEME_CUSTOM);
	HBAS::ThemeManager::saveThemePreference();
	this->back();
}

void ThemeColorizeScreen::back()
{
	if (returnToThemeScreen)
	{
		// volver a la pantalla de seleccion de temas (nueva instancia,
		// ya que la original fue destruida cuando se abrio esta pantalla)
		RootDisplay::switchSubscreen(new ThemeScreen());
	}
	else
	{
		RootDisplay::switchSubscreen(nullptr);
	}
}
