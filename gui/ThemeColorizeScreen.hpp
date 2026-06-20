#ifndef THEME_COLORIZE_SCREEN_H_
#define THEME_COLORIZE_SCREEN_H_

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "../libs/chesto/src/Button.hpp"
#include "ThemeManager.hpp"

// Pantalla de personalizacion de colores del tema "Personalizado"
// Permite ajustar cada componente R/G/B de cada color disponible
// usando izquierda/derecha para cambiar el valor y arriba/abajo
// para moverse entre los colores y sus componentes.
class ThemeColorizeScreen : public Element
{
public:
	ThemeColorizeScreen();
	~ThemeColorizeScreen();

	void render(Element* parent);
	bool process(InputEvents* event);

	void back();
	void applyAndSave();

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

	// fila seleccionada (0..entries.size()-1) y componente seleccionado (0=R,1=G,2=B,3=A)
	int selectedRow = 0;
	int selectedComponent = 0;

	TextElement title;
	TextElement hint;
	std::vector<TextElement*> rowLabels;
	std::vector<TextElement*> rowValues;

	Button* applyButton = nullptr;

	void refreshRowText(int row);
	void adjustValue(int delta);

	// vista previa del recuadro de color a la derecha de cada fila
	void drawColorPreview(int row, int rowY);
};

#endif
