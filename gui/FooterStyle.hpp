#pragma once

// Estilo de la barra inferior compartida (Sidebar + AppList): altura de la
// linea superior, color de la linea y color del contenido (texto/iconos).
// Todo en este header (sin .cpp) para no tocar el Makefile.

#include "ThemeManager.hpp"

#include <algorithm>
#include <cstdlib>

// grosor de la linea que separa la barra inferior del area de componentes
#define SIDEBAR_FOOTER_LINE (3/SCALER)

namespace FooterStyle {

// brillo percibido (0..255) de un color
inline int luminance(const CST_Color& c)
{
	return (299 * c.r + 587 * c.g + 114 * c.b) / 1000;
}

// true si el fondo de la barra es oscuro -> el texto/iconos deben ser claros
inline bool contentIsLight()
{
	return luminance(HBAS::ThemeManager::sidebarFooterBg) < 140;
}

// Color de la linea superior: el del fondo del titulo (nombre + version del
// programa). Si el tema usa el MISMO color en la barra y en el titulo la linea
// no se veria, asi que en ese caso se aclara/oscurece un poco para que siga
// separando.
inline CST_Color lineColor()
{
	CST_Color line = HBAS::ThemeManager::sidebarTitleBg;
	const CST_Color& bg = HBAS::ThemeManager::sidebarFooterBg;

	const int diff = std::abs((int)line.r - (int)bg.r)
	               + std::abs((int)line.g - (int)bg.g)
	               + std::abs((int)line.b - (int)bg.b);
	if (diff < 60)
	{
		const int delta = contentIsLight() ? 0x40 : -0x40;
		auto shift = [delta](int v) { return (Uint8)std::clamp(v + delta, 0, 255); };
		line.r = shift(line.r);
		line.g = shift(line.g);
		line.b = shift(line.b);
	}
	line.a = 0xff;
	return line;
}

}
