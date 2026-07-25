#pragma once

#include <cstdint>

#include "../libs/chesto/src/Element.hpp"
#include "../libs/chesto/src/RootDisplay.hpp"

// Rectangulo negro semitransparente de pantalla completa -- el mismo efecto
// que ya usa ProgressBar cuando dimBg = true (ver libs/chesto/src/ProgressBar.cpp),
// pero factorizado aparte para poder oscurecer pantallas que no necesariamente
// tienen una barra de progreso al lado (por ejemplo, un dialogo de
// confirmacion con solo texto suelto y botones, sin caja flotante).
class DimOverlay : public Element
{
public:
	// opacidad 0-255 (0xbb = mismo valor que usa ProgressBar::dimBg)
	std::uint8_t opacity = 0xbb;

	void render(Element* parent) override
	{
		if (hidden) return;

		auto renderer = getRenderer();
		CST_Rect dim = { 0, 0, RootDisplay::screenWidth, RootDisplay::screenHeight };

		CST_SetDrawBlend(renderer, true);
		CST_SetDrawColorRGBA(renderer, 0x00, 0x00, 0x00, opacity);
		CST_FillRect(renderer, &dim);
	}
};
