#ifndef TOOLS_SCREEN_H_
#define TOOLS_SCREEN_H_

#include <vector>

#include "../libs/get/src/Get.hpp"

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "../libs/chesto/src/Button.hpp"

// Menu de "Herramientas", con botones grandes para las distintas
// pantallas/acciones de configuracion. Se abre con el boton R (que antes
// abria "Temas" directo); desde aca se llega a Temas, Creditos, y
// Limpieza de cache. El boton "Creditos" que antes estaba suelto en la
// pantalla principal ya no hace falta ahi, se movio para aca adentro.
class ToolsScreen : public Element
{
public:
	ToolsScreen(Get* get);
	~ToolsScreen();

	void render(Element* parent);
	bool process(InputEvents* event);

	void back();
	void openThemes();
	void openCredits();
	void wipeIconCache();

private:
	Get* get = NULL;

	TextElement title;
	TextElement hint;

	Button themesBtn;
	Button creditsBtn;
	Button wipeCacheBtn;

	// solo para navegar con el D-pad/cursor -- apuntan a los botones de
	// arriba, no son dueños de la memoria (los botones son miembros, no
	// se allocan con new)
	std::vector<Button*> buttons;
	int highlighted = 0;
};

#endif
