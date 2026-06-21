#ifndef COLOR_PICKER_SCREEN_H_
#define COLOR_PICKER_SCREEN_H_

#include "../libs/chesto/src/RootDisplay.hpp"
#include "../libs/chesto/src/TextElement.hpp"
#include "../libs/chesto/src/Button.hpp"
#include "../libs/chesto/src/EKeyboard.hpp"
#include "../libs/chesto/src/Texture.hpp"
#include "ThemeManager.hpp"
#include <functional>

// Pantalla de seleccion de un solo color, al estilo del selector de
// Windows: rueda de color (matiz/saturacion), barra de luminosidad,
// campos editables de R/G/B y codigo hexadecimal, con vista previa.
//
// Al terminar de editar (boton "Listo"), llama a onColorPicked con el
// color final, y luego vuelve a la pantalla anterior.
class ColorPickerScreen : public Element
{
public:
	ColorPickerScreen(const std::string& title, CST_Color initialColor,
	                   std::function<void(CST_Color)> onColorPicked);
	~ColorPickerScreen();

	void render(Element* parent);
	bool process(InputEvents* event);

	void back();
	void confirm();

private:
	std::string screenTitle;
	CST_Color currentColor;
	std::function<void(CST_Color)> onColorPicked;

	// HSV del color actual (h: 0-360, s: 0-1, v: 0-1)
	double hue = 0;
	double sat = 0;
	double val = 1;

	// textura de la rueda de color (matiz/saturacion), generada una sola vez
	Texture* wheelTexture = nullptr;
	int wheelX = 60, wheelY = 90, wheelSize = 300;

	// barra vertical de luminosidad/valor, a la derecha de la rueda
	int valueBarX = 400, valueBarY = 90, valueBarW = 40, valueBarH = 300;

	// cuadro grande de vista previa
	int previewX = 480, previewY = 90, previewW = 140, previewH = 140;

	// campos de texto editables: hex, R, G, B
	TextElement titleText;
	TextElement hint;

	TextElement hexLabel;
	TextElement hexValue;
	TextElement redLabel;
	TextElement redValue;
	TextElement greenLabel;
	TextElement greenValue;
	TextElement blueLabel;
	TextElement blueValue;

	Button* doneButton = nullptr;

	// cual campo esta actualmente en edicion (-1 = ninguno, la rueda/barra
	// tienen el foco de navegacion normal)
	enum EditField { EDIT_NONE = -1, EDIT_HEX = 0, EDIT_R = 1, EDIT_G = 2, EDIT_B = 3 };
	int editingField = EDIT_NONE;
	EKeyboard* keyboard = nullptr;

	// foco de navegacion cuando no se esta editando un campo de texto:
	// 0 = rueda de color, 1 = barra de valor, 2..5 = campos hex/r/g/b, 6 = boton Listo
	int focusIndex = 0;
	static const int FOCUS_WHEEL = 0;
	static const int FOCUS_VALUEBAR = 1;
	static const int FOCUS_HEX = 2;
	static const int FOCUS_R = 3;
	static const int FOCUS_G = 4;
	static const int FOCUS_B = 5;
	static const int FOCUS_DONE = 6;

	void generateWheelTexture();
	void updateFromHSV();           // recalcula currentColor a partir de h/s/v
	void updateFromRGB();           // recalcula h/s/v a partir de currentColor
	void refreshTexts();            // actualiza los TextElement con los valores actuales
	void startEditingField(int field);
	void finishEditingField();
	void keyboardInputCallback();
};

#endif
