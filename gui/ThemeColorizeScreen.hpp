#ifndef THEME_COLORIZE_SCREEN_H_
#define THEME_COLORIZE_SCREEN_H_

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "../libs/chesto/src/Button.hpp"
#include "ThemeManager.hpp"

// Pantalla de personalizacion de colores del tema "Personalizado".
// Muestra solo la lista de categorias editables junto con un cuadro
// de vista previa de cada color. Al presionar A sobre una fila, se
// abre ColorPickerScreen para editar ese color especificamente.
class ThemeColorizeScreen : public Element
{
public:
	ThemeColorizeScreen();
	~ThemeColorizeScreen();

	void render(Element* parent);
	bool process(InputEvents* event);

	void back();
	void applyAndSave();
	void openPickerForRow(int row);

	// si es true, al volver (B o aplicar) regresa a una nueva instancia de
	// ThemeScreen en vez de cerrar todo hacia el menu principal
	bool returnToThemeScreen = true;

private:
	// referencia a cada CST_Color editable, junto con su nombre para mostrar
	struct ColorEntry
	{
		CST_Color* color;
		std::string label;
	};

	std::vector<ColorEntry> entries;

	// fila actualmente resaltada por el cursor de navegacion
	int selectedRow = 0;

	TextElement title;
	TextElement hint;
	std::vector<TextElement*> rowLabels;

	Button* applyButton = nullptr;

	// vista previa del recuadro de color a la derecha de cada fila
	void drawColorPreview(int row, int rowY);
};

#endif
