#include "ColorPickerScreen.hpp"
#include "ThemeColorizeScreen.hpp"
#include "../libs/chesto/src/Constraint.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

#define WHEEL_RADIUS_PADDING 4

ColorPickerScreen::ColorPickerScreen(const std::string& title, CST_Color initialColor,
                                      std::function<void(CST_Color)> onColorPicked)
	: screenTitle(title)
	, currentColor(initialColor)
	, onColorPicked(onColorPicked)
	, titleText(title, 30, &HBAS::ThemeManager::textPrimary)
	, hint("Touch o flechas: mover/ajustar   A: editar campo   Toca \"Confirmar texto\" para aplicar   B: volver", 16, &HBAS::ThemeManager::textSecondary)
	, editPreviewText("", 26, &HBAS::ThemeManager::textPrimary)
	, hexLabel("Codigo Hex", 18, &HBAS::ThemeManager::textSecondary)
	, hexValue("#000000", 22, &HBAS::ThemeManager::textPrimary)
	, redLabel("Rojo (R)", 18, &HBAS::ThemeManager::textSecondary)
	, redValue("0", 22, &HBAS::ThemeManager::textPrimary)
	, greenLabel("Verde (G)", 18, &HBAS::ThemeManager::textSecondary)
	, greenValue("0", 22, &HBAS::ThemeManager::textPrimary)
	, blueLabel("Azul (B)", 18, &HBAS::ThemeManager::textSecondary)
	, blueValue("0", 22, &HBAS::ThemeManager::textPrimary)
{
	this->width = SCREEN_WIDTH;
	this->height = SCREEN_HEIGHT;

	titleText.position(60, 30);
	this->append(&titleText);

	hint.position(60, SCREEN_HEIGHT - 40);
	this->append(&hint);

	// inicializar HSV a partir del color recibido
	updateFromRGB();

	// generar la textura de la rueda de color una sola vez
	generateWheelTexture();
	if (wheelTexture)
		this->append(wheelTexture);

	// columna derecha: campos de texto editables
	int fieldsX = 660;
	int fieldY = 90;
	int fieldGap = 60;

	hexLabel.position(fieldsX, fieldY);
	this->append(&hexLabel);
	hexValue.position(fieldsX, fieldY + 25);
	this->append(&hexValue);

	fieldY += fieldGap;
	redLabel.position(fieldsX, fieldY);
	this->append(&redLabel);
	redValue.position(fieldsX, fieldY + 25);
	this->append(&redValue);

	fieldY += fieldGap;
	greenLabel.position(fieldsX, fieldY);
	this->append(&greenLabel);
	greenValue.position(fieldsX, fieldY + 25);
	this->append(&greenValue);

	fieldY += fieldGap;
	blueLabel.position(fieldsX, fieldY);
	this->append(&blueLabel);
	blueValue.position(fieldsX, fieldY + 25);
	this->append(&blueValue);

	doneButton = new Button("Listo", 0, true, 22, 200);
	doneButton->position(fieldsX, fieldY + 80);
	doneButton->action = std::bind(&ColorPickerScreen::confirm, this);
	this->append(doneButton);

	// teclado en pantalla, oculto hasta que se edite un campo
	// Se usa el constructor sin callback para que storeOwnText=true y
	// getTextInput() acumule correctamente los caracteres escritos.
	keyboard = new EKeyboard();
	keyboard->position(190, SCREEN_HEIGHT - 360);
	keyboard->preventEnterAndTab = true;
	keyboard->updateSize();
	keyboard->hidden = true;
	this->append(keyboard);

	// boton visible para confirmar el texto escrito, ya que el teclado en
	// pantalla de chesto no tiene una tecla "Enter" navegable con D-pad
	confirmEditButton = new Button("Confirmar", 0, true, 18, 170);
	confirmEditButton->position(660, SCREEN_HEIGHT - 360 - 60);
	confirmEditButton->action = std::bind(&ColorPickerScreen::finishEditingField, this);
	confirmEditButton->hidden = true;
	this->append(confirmEditButton);

	// vista previa en tiempo real del texto que se esta escribiendo,
	// se posiciona encima del teclado, oculto hasta que se edita un campo
	editPreviewText.position(190, SCREEN_HEIGHT - 360 - 50);
	editPreviewText.hidden = true;
	this->append(&editPreviewText);

	refreshTexts();
}

ColorPickerScreen::~ColorPickerScreen()
{
	if (wheelTexture) delete wheelTexture;
	if (doneButton) delete doneButton;
	if (confirmEditButton) delete confirmEditButton;
	if (keyboard) delete keyboard;
}

// ----------------------------------------------------------------------
// Genera una textura cuadrada donde cada pixel representa un punto de la
// rueda de matiz/saturacion (HSV con V fijo en 1.0 para la textura base;
// el brillo real se aplica despues con la barra de valor, oscureciendo
// la vista previa, no la rueda en si - igual que el picker de Windows).
// ----------------------------------------------------------------------
void ColorPickerScreen::generateWheelTexture()
{
	int size = wheelSize;
	int radius = size / 2;

	CST_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
	if (!surface)
		return;

	SDL_LockSurface(surface);
	uint32_t* pixels = (uint32_t*)surface->pixels;

	for (int y = 0; y < size; y++)
	{
		for (int x = 0; x < size; x++)
		{
			double dx = x - radius;
			double dy = y - radius;
			double dist = std::sqrt(dx * dx + dy * dy);

			uint8_t r, g, b, a;

			if (dist > radius - WHEEL_RADIUS_PADDING)
			{
				// fuera del circulo: transparente
				r = g = b = a = 0;
			}
			else
			{
				double angle = std::atan2(dy, dx) * 180.0 / M_PI;
				if (angle < 0) angle += 360.0;

				hsv pixelHsv;
				pixelHsv.h = angle;
				pixelHsv.s = dist / (double)radius;
				if (pixelHsv.s > 1.0) pixelHsv.s = 1.0;
				pixelHsv.v = 1.0;

				rgb pixelRgb = hsv2rgb(pixelHsv);
				r = (uint8_t)(pixelRgb.r * 255);
				g = (uint8_t)(pixelRgb.g * 255);
				b = (uint8_t)(pixelRgb.b * 255);
				a = 255;
			}

			pixels[y * size + x] = SDL_MapRGBA(surface->format, r, g, b, a);
		}
	}

	SDL_UnlockSurface(surface);

	wheelTexture = new Texture();
	wheelTexture->loadFromSurface(surface);
	wheelTexture->resize(size, size);
	wheelTexture->position(wheelX, wheelY);

	SDL_FreeSurface(surface);
}

void ColorPickerScreen::updateFromHSV()
{
	hsv currentHsv = { hue, sat, val };
	rgb result = hsv2rgb(currentHsv);
	currentColor.r = (uint8_t)(result.r * 255);
	currentColor.g = (uint8_t)(result.g * 255);
	currentColor.b = (uint8_t)(result.b * 255);
}

void ColorPickerScreen::updateFromRGB()
{
	rgb colorAsRgb = fromRGB(currentColor.r, currentColor.g, currentColor.b);
	hsv result = rgb2hsv(colorAsRgb);
	hue = std::isnan(result.h) ? 0 : result.h;
	sat = result.s;
	val = result.v;
}

void ColorPickerScreen::refreshTexts()
{
	std::stringstream hexSs;
	hexSs << "#" << std::uppercase << std::hex << std::setfill('0')
	      << std::setw(2) << (int)currentColor.r
	      << std::setw(2) << (int)currentColor.g
	      << std::setw(2) << (int)currentColor.b;
	hexValue.setText(hexSs.str());
	hexValue.update();

	redValue.setText(std::to_string((int)currentColor.r));
	redValue.update();
	greenValue.setText(std::to_string((int)currentColor.g));
	greenValue.update();
	blueValue.setText(std::to_string((int)currentColor.b));
	blueValue.update();
}

void ColorPickerScreen::render(Element* parent)
{
	if (this->parent == NULL)
		this->parent = parent;

	// fondo de toda la pantalla
	CST_Rect dimens = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	CST_SetDrawColor(RootDisplay::renderer, HBAS::ThemeManager::background);
	CST_FillRect(RootDisplay::renderer, &dimens);

	super::render(parent);

	// --- Indicador de posicion en la rueda (circulo pequeño) ---
	int radius = wheelSize / 2;
	double angleRad = hue * M_PI / 180.0;
	double dist = sat * radius;
	int markerX = wheelX + radius + (int)(std::cos(angleRad) * dist);
	int markerY = wheelY + radius + (int)(std::sin(angleRad) * dist);

	CST_Color markerColor = {0x00, 0x00, 0x00, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, markerColor);
	for (int i = 0; i < 3; i++)
	{
		CST_Rect markerRect = { markerX - 8 - i, markerY - 8 - i, 16 + i * 2, 16 + i * 2 };
		CST_DrawRect(RootDisplay::renderer, &markerRect);
	}

	// --- Barra vertical de luminosidad/valor ---
	// gradiente simple: dibujamos franjas horizontales de color del mas
	// claro (arriba) al mas oscuro (abajo), usando el matiz/saturacion actual
	for (int i = 0; i < valueBarH; i++)
	{
		double v = 1.0 - (double)i / (double)valueBarH;
		hsv stepHsv = { hue, sat, v };
		rgb stepRgb = hsv2rgb(stepHsv);
		CST_Color stepColor = {
			(uint8_t)(stepRgb.r * 255),
			(uint8_t)(stepRgb.g * 255),
			(uint8_t)(stepRgb.b * 255),
			0xff
		};
		CST_SetDrawColor(RootDisplay::renderer, stepColor);
		CST_Rect stepRect = { valueBarX, valueBarY + i, valueBarW, 1 };
		CST_FillRect(RootDisplay::renderer, &stepRect);
	}
	// indicador de posicion en la barra de valor
	int valueMarkerY = valueBarY + (int)((1.0 - val) * valueBarH);
	CST_Color valueMarkerColor = {0xff, 0xff, 0xff, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, valueMarkerColor);
	CST_Rect valueMarkerRect = { valueBarX - 4, valueMarkerY - 3, valueBarW + 8, 6 };
	CST_FillRect(RootDisplay::renderer, &valueMarkerRect);

	// --- Cuadro de vista previa grande ---
	CST_SetDrawColor(RootDisplay::renderer, currentColor);
	CST_Rect previewRect = { previewX, previewY, previewW, previewH };
	CST_FillRect(RootDisplay::renderer, &previewRect);
	CST_Color previewBorder = {0x00, 0x00, 0x00, 0xff};
	CST_SetDrawColor(RootDisplay::renderer, previewBorder);
	CST_DrawRect(RootDisplay::renderer, &previewRect);

	// --- Marco de foco de navegacion (cuando no se esta editando texto) ---
	if (editingField == EDIT_NONE)
	{
		CST_Color focusColor = {0xff, 0xd9, 0x00, 0xff};
		CST_SetDrawColor(RootDisplay::renderer, focusColor);
		CST_Rect focusRect = { 0, 0, 0, 0 };

		switch (focusIndex)
		{
		case FOCUS_WHEEL:
			focusRect = { wheelX - 5, wheelY - 5, wheelSize + 10, wheelSize + 10 };
			break;
		case FOCUS_VALUEBAR:
			focusRect = { valueBarX - 8, valueBarY - 8, valueBarW + 16, valueBarH + 16 };
			break;
		case FOCUS_HEX:
			focusRect = { 660 - 8, 90 - 8, 220, 60 };
			break;
		case FOCUS_R:
			focusRect = { 660 - 8, 150 - 8, 220, 60 };
			break;
		case FOCUS_G:
			focusRect = { 660 - 8, 210 - 8, 220, 60 };
			break;
		case FOCUS_B:
			focusRect = { 660 - 8, 270 - 8, 220, 60 };
			break;
		case FOCUS_DONE:
			focusRect = { 660 - 8, 350 - 8, 216, 56 };
			break;
		}

		if (focusRect.w > 0)
			CST_DrawRect(RootDisplay::renderer, &focusRect);
	}
}

bool ColorPickerScreen::process(InputEvents* event)
{
	// Si estamos editando un campo de texto, el teclado tiene el control total
	if (editingField != EDIT_NONE)
	{
		// Tocar el boton "Confirmar texto" aplica el valor escrito.
		if (event->isTouchUp() && event->touchIn(660, SCREEN_HEIGHT - 360 - 60, 170, 50))
		{
			finishEditingField();
			return true;
		}

		// X como atajo adicional, por si hay teclado fisico conectado
		if (event->pressed(X_BUTTON))
		{
			finishEditingField();
			return true;
		}
		// B cancela la edicion del campo sin aplicar el cambio
		if (event->pressed(B_BUTTON))
		{
			editingField = EDIT_NONE;
			keyboard->hidden = true;
			confirmEditButton->hidden = true;
			editPreviewText.hidden = true;
			return true;
		}

		// procesar tecla y actualizar el preview en vivo con lo que se va escribiendo
		bool handled = keyboard->process(event);
		std::string typed = keyboard->textInput;
		editPreviewText.setText(typed.empty() ? "(escribe un valor 0-255)" : typed);
		editPreviewText.update();
		return handled;
	}

	if (event->pressed(B_BUTTON))
	{
		this->back();
		return true;
	}

	if (event->pressed(A_BUTTON))
	{
		if (focusIndex == FOCUS_DONE)
		{
			this->confirm();
		}
		else if (focusIndex >= FOCUS_HEX && focusIndex <= FOCUS_B)
		{
			startEditingField(focusIndex - FOCUS_HEX);
		}
		return true;
	}

	// Navegacion en cuadricula: fila 0 = [rueda, barra de valor] (uno al lado
	// del otro), filas 1 a 5 = [hex, R, G, B, Listo] en una sola columna.
	// Arriba/Abajo siempre cambia de fila. Izquierda/Derecha solo cambia
	// entre rueda y barra cuando estamos en la fila 0; en las demas filas
	// se usa como ajuste fino (no aplica aqui, son campos de texto).
	if (event->pressed(DOWN_BUTTON))
	{
		if (focusIndex == FOCUS_WHEEL || focusIndex == FOCUS_VALUEBAR)
			focusIndex = FOCUS_HEX;
		else
			focusIndex = (focusIndex + 1 > FOCUS_DONE) ? FOCUS_WHEEL : focusIndex + 1;
		return true;
	}
	if (event->pressed(UP_BUTTON))
	{
		if (focusIndex == FOCUS_WHEEL || focusIndex == FOCUS_VALUEBAR)
			focusIndex = FOCUS_DONE;
		else if (focusIndex == FOCUS_HEX)
			focusIndex = FOCUS_WHEEL;
		else
			focusIndex = focusIndex - 1;
		return true;
	}

	// Izquierda/Derecha: en la fila de la rueda, cambia el foco entre
	// rueda y barra de valor. En cualquiera de los dos, tambien ajusta
	// el valor correspondiente (igual que antes).
	if (focusIndex == FOCUS_WHEEL || focusIndex == FOCUS_VALUEBAR)
	{
		if (event->pressed(RIGHT_BUTTON) && focusIndex == FOCUS_WHEEL)
		{
			focusIndex = FOCUS_VALUEBAR;
			return true;
		}
		if (event->pressed(LEFT_BUTTON) && focusIndex == FOCUS_VALUEBAR)
		{
			focusIndex = FOCUS_WHEEL;
			return true;
		}
	}

	if (focusIndex == FOCUS_WHEEL)
	{
		bool changed = false;
		if (event->held(RIGHT_BUTTON)) { hue += 2; changed = true; }
		if (event->held(LEFT_BUTTON))  { hue -= 2; changed = true; }
		if (hue < 0) hue += 360;
		if (hue >= 360) hue -= 360;

		if (changed)
		{
			updateFromHSV();
			refreshTexts();
			return true;
		}
	}
	else if (focusIndex == FOCUS_VALUEBAR)
	{
		bool changed = false;
		if (event->held(RIGHT_BUTTON)) { val += 0.02; changed = true; }
		if (event->held(LEFT_BUTTON))  { val -= 0.02; changed = true; }
		if (val < 0) val = 0;
		if (val > 1) val = 1;

		if (changed)
		{
			updateFromHSV();
			refreshTexts();
			return true;
		}
	}

	// --- Touch: tocar la rueda mueve el matiz/saturacion directamente ---
	if ((event->isTouchDrag() || event->isTouchDown()) &&
	    event->touchIn(wheelX, wheelY, wheelSize, wheelSize))
	{
		focusIndex = FOCUS_WHEEL;

		int radius = wheelSize / 2;
		double dx = event->xPos - (wheelX + radius);
		double dy = event->yPos - (wheelY + radius);
		double dist = std::sqrt(dx * dx + dy * dy);

		double angle = std::atan2(dy, dx) * 180.0 / M_PI;
		if (angle < 0) angle += 360.0;

		hue = angle;
		sat = dist / (double)radius;
		if (sat > 1.0) sat = 1.0;

		updateFromHSV();
		refreshTexts();
		return true;
	}

	// --- Touch: tocar/arrastrar en la barra de valor ajusta la luminosidad ---
	if ((event->isTouchDrag() || event->isTouchDown()) &&
	    event->touchIn(valueBarX - 10, valueBarY, valueBarW + 20, valueBarH))
	{
		focusIndex = FOCUS_VALUEBAR;

		double relativeY = event->yPos - valueBarY;
		val = 1.0 - (relativeY / (double)valueBarH);
		if (val < 0) val = 0;
		if (val > 1) val = 1;

		updateFromHSV();
		refreshTexts();
		return true;
	}

	// --- Touch: tocar un campo de texto lo enfoca y abre su edicion ---
	if (event->isTouchUp())
	{
		struct TouchTarget { int focus; int x, y, w, h; };
		TouchTarget targets[] = {
			{ FOCUS_HEX, 660, 90,  220, 60 },
			{ FOCUS_R,   660, 150, 220, 60 },
			{ FOCUS_G,   660, 210, 220, 60 },
			{ FOCUS_B,   660, 270, 220, 60 },
		};

		for (auto& t : targets)
		{
			if (event->touchIn(t.x, t.y, t.w, t.h))
			{
				focusIndex = t.focus;
				startEditingField(t.focus - FOCUS_HEX);
				return true;
			}
		}

		// tocar el boton Listo lo activa directamente
		if (event->touchIn(660, 350, 216, 56))
		{
			focusIndex = FOCUS_DONE;
			this->confirm();
			return true;
		}
	}

	return super::process(event);
}

void ColorPickerScreen::startEditingField(int field)
{
	editingField = field;
	keyboard->hidden = false;
	confirmEditButton->hidden = false;

	// mostrar el valor actual como referencia visual, pero NO pre-cargarlo
	// en textInput: el usuario escribe el numero nuevo desde cero, sin
	// necesitar borrar lo existente (que era el bug que causaba el 255)
	std::string currentVal;
	switch (field)
	{
	case EDIT_HEX: currentVal = hexValue.getText();                     break;
	case EDIT_R:   currentVal = std::to_string((int)currentColor.r);    break;
	case EDIT_G:   currentVal = std::to_string((int)currentColor.g);    break;
	case EDIT_B:   currentVal = std::to_string((int)currentColor.b);    break;
	}

	// empezar con campo vacio para que el usuario escriba sin conflicto
	keyboard->textInput.clear();

	// mostrar "Valor actual: XXX" encima del teclado como referencia
	editPreviewText.setText("Valor actual: " + currentVal + "   |   Escribe el nuevo valor:");
	editPreviewText.update();
	editPreviewText.hidden = false;
}

void ColorPickerScreen::finishEditingField()
{
	std::string text = keyboard->getTextInput();

	switch (editingField)
	{
	case EDIT_HEX:
	{
		// aceptar con o sin '#' al inicio
		std::string hex = text;
		if (!hex.empty() && hex[0] == '#')
			hex = hex.substr(1);

		if (hex.size() == 6)
		{
			try
			{
				int r = std::stoi(hex.substr(0, 2), nullptr, 16);
				int g = std::stoi(hex.substr(2, 2), nullptr, 16);
				int b = std::stoi(hex.substr(4, 2), nullptr, 16);
				currentColor.r = (uint8_t)r;
				currentColor.g = (uint8_t)g;
				currentColor.b = (uint8_t)b;
				updateFromRGB();
			}
			catch (...) { /* texto invalido, ignorar */ }
		}
		break;
	}
	case EDIT_R:
	case EDIT_G:
	case EDIT_B:
	{
		try
		{
			int value = std::stoi(text);
			if (value < 0) value = 0;
			if (value > 255) value = 255;

			if (editingField == EDIT_R) currentColor.r = (uint8_t)value;
			if (editingField == EDIT_G) currentColor.g = (uint8_t)value;
			if (editingField == EDIT_B) currentColor.b = (uint8_t)value;

			updateFromRGB();
		}
		catch (...) { /* texto invalido, ignorar */ }
		break;
	}
	}

	refreshTexts();
	editingField = EDIT_NONE;
	keyboard->hidden = true;
	confirmEditButton->hidden = true;
	editPreviewText.hidden = true;
}

void ColorPickerScreen::keyboardInputCallback()
{
	// no se necesita hacer nada en cada tecla individual, el valor final
	// se procesa en finishEditingField cuando el usuario confirma
}

void ColorPickerScreen::confirm()
{
	if (onColorPicked)
		onColorPicked(currentColor);
	this->back();
}

void ColorPickerScreen::back()
{
	// volver a la pantalla de categorias de colores (nueva instancia,
	// ya que la original fue destruida cuando se abrio este picker)
	RootDisplay::switchSubscreen(new ThemeColorizeScreen());
}
