#include "ThemeColorizeScreen.hpp"
#include "ThemeScreen.hpp"
#include "ColorPickerScreen.hpp"
#include "../libs/chesto/src/Constraint.hpp"
#include <sstream>

#define ROW_HEIGHT 55
#define ROW_START_Y 110
#define ROW_X 60
#define PREVIEW_X 700

ThemeColorizeScreen::ThemeColorizeScreen()
	: title("Personalizar colores", 32, &HBAS::ThemeManager::textPrimary)
	, hint("Arriba/Abajo: cambiar fila   A o Touch: editar color   B: volver", 18, &HBAS::ThemeManager::textSecondary)
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

		TextElement* label = new TextElement(entries[i].label.c_str(), 22, &HBAS::ThemeManager::textPrimary, false, 580);
		label->position(ROW_X, y);
		this->append(label);
		rowLabels.push_back(label);
	}

	applyButton = new Button("Aplicar y guardar", 0, true, 22, 280);
	applyButton->position(60, ROW_START_Y + (int)entries.size() * ROW_HEIGHT + 30);
	applyButton->action = std::bind(&ThemeColorizeScreen::applyAndSave, this);
	this->append(applyButton);
}

ThemeColorizeScreen::~ThemeColorizeScreen()
{
	for (auto label : rowLabels) delete label;
	if (applyButton) delete applyButton;
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

	// resaltar la fila o el boton "Aplicar y guardar" segun el foco actual
	CST_Color highlightColor = {0xff, 0xd9, 0x00, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, highlightColor);

	if (selectedRow == (int)entries.size())
	{
		CST_Rect buttonRect = { 60 - 8, ROW_START_Y + (int)entries.size() * ROW_HEIGHT + 30 - 8, 296, 56 };
		CST_DrawRect(RootDisplay::renderer, &buttonRect);
	}
	else
	{
		int y = ROW_START_Y + selectedRow * ROW_HEIGHT - 8;
		CST_Rect rowRect = { ROW_X - 10, y, 900, 42 };
		CST_DrawRect(RootDisplay::renderer, &rowRect);
	}

	// dibujar la vista previa de color de cada fila (solo el recuadro,
	// sin texto de valores RGB - eso ahora vive en ColorPickerScreen)
	for (size_t i = 0; i < entries.size(); i++)
		drawColorPreview((int)i, ROW_START_Y + (int)i * ROW_HEIGHT);
}

void ThemeColorizeScreen::drawColorPreview(int row, int rowY)
{
	CST_Color* c = entries[row].color;
	CST_SetDrawColor(RootDisplay::renderer, *c);
	CST_Rect previewRect = { PREVIEW_X, rowY - 5, 120, 38 };
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

	int totalFocusable = (int)entries.size() + 1; // +1 por el boton "Aplicar y guardar"

	if (event->pressed(A_BUTTON))
	{
		if (selectedRow == (int)entries.size())
		{
			this->applyAndSave();
		}
		else
		{
			this->openPickerForRow(selectedRow);
		}
		return true;
	}

	if (event->pressed(DOWN_BUTTON))
	{
		selectedRow = (selectedRow + 1) % totalFocusable;
		return true;
	}
	if (event->pressed(UP_BUTTON))
	{
		selectedRow = (selectedRow - 1 + totalFocusable) % totalFocusable;
		return true;
	}

	// Touch: tocar una fila la selecciona y abre el editor directamente,
	// igual que presionar A despues de navegar hasta ahi
	if (event->isTouchUp())
	{
		for (size_t i = 0; i < entries.size(); i++)
		{
			int rowTop = ROW_START_Y + (int)i * ROW_HEIGHT - 8;
			if (event->touchIn(ROW_X - 10, rowTop, 900, 42))
			{
				selectedRow = (int)i;
				this->openPickerForRow((int)i);
				return true;
			}
		}
	}

	return super::process(event);
}

void ThemeColorizeScreen::openPickerForRow(int row)
{
	if (row < 0 || row >= (int)entries.size())
		return;

	CST_Color* targetColor = entries[row].color;
	std::string label = entries[row].label;

	RootDisplay::switchSubscreen(new ColorPickerScreen(
		label,
		*targetColor,
		[targetColor](CST_Color picked) {
			// guardar el color elegido directamente en customColors
			*targetColor = picked;

			// aplicar en vivo para que se vea reflejado de inmediato
			HBAS::ThemeManager::currentTheme = HBAS::ThemeManager::THEME_CUSTOM;
			HBAS::ThemeManager::applyTheme(HBAS::ThemeManager::THEME_CUSTOM);
		}
	));
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
		RootDisplay::switchSubscreen(new ThemeScreen());
	}
	else
	{
		RootDisplay::switchSubscreen(nullptr);
	}
}
