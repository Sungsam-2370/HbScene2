#ifndef FEEDBACKSCREEN_H_
#define FEEDBACKSCREEN_H_

#include "../libs/get/src/Get.hpp"

#include "../libs/chesto/src/Button.hpp"
#include "../libs/chesto/src/ImageElement.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "../libs/chesto/src/NetImageElement.hpp"

class Feedback : public Element
{
public:
	Feedback(Package& package);

	void render(Element* parent);
	bool process(InputEvents* event);
	bool needsRefresh = false;

	Package* package = NULL;

	void submit();
	void back();
	void edit(); // abre el teclado del sistema para escribir / editar el mensaje

private:
	std::string message; // texto del mensaje (lo escribe el teclado del sistema)

	TextElement title;
	NetImageElement icon;
	Button quit;
	Button send;
	Button writeBtn;
	TextElement response;
	TextElement feedback;
};

#endif
