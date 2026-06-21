#ifndef THEME_SCREEN_H_
#define THEME_SCREEN_H_

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "ThemeManager.hpp"

class ThemeColorizeScreen; // forward declaration, definida en ThemeColorizeScreen.hpp

// Una sola tarjeta de tema (igual de tamaño que las tarjetas de componentes)
class ThemeCard : public Element
{
public:
	ThemeCard(int themeId);

	void render(Element* parent);
	bool process(InputEvents* event);

	int themeId;
	bool selected = false;   // true solo si este es el tema activo en el sistema
	bool cursorHere = false; // true si el cursor de navegacion esta sobre esta tarjeta

private:
	TextElement nameText;
	TextElement selectedText;
};

// Pantalla principal con las 6 tarjetas de temas, se abre con el boton R
class ThemeScreen : public Element
{
public:
	ThemeScreen();
	~ThemeScreen();

	void render(Element* parent);
	bool process(InputEvents* event);

	void selectCard(int index);
	void back();

private:
	TextElement title;
	TextElement hint;
	std::vector<ThemeCard*> cards;
	int highlighted = 0;
};

#endif
