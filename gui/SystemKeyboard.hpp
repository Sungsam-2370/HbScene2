#pragma once

// Teclado del sistema de la Switch (software keyboard applet de libnx).
// Un solo punto de entrada para todas las pantallas que piden texto.
//
// ask(): abre el teclado y espera. Devuelve true si el usuario confirmo (el texto queda
// en "out", puede venir vacio) y false si cancelo o si la plataforma no lo soporta.
// La app no se dibuja mientras esta abierto; al volver se descartan las pulsaciones
// acumuladas para que no se disparen acciones por accidente.

#include <string>
#include "../libs/chesto/src/InputEvents.hpp"

#if defined(SWITCH)
#include <switch.h>
#endif

namespace SystemKeyboard
{
inline bool ask(const std::string& header, const std::string& guide, const std::string& initial,
				int maxLen, std::string& out, bool numbersOnly = false, bool multiline = false)
{
#if defined(SWITCH)
	SwkbdConfig kbd;
	if (R_FAILED(swkbdCreate(&kbd, 0)))
		return false;

	swkbdConfigMakePresetDefault(&kbd);
	if (numbersOnly)
		swkbdConfigSetType(&kbd, SwkbdType_NumPad);
	if (multiline)
		swkbdConfigSetReturnButtonFlag(&kbd, 1);
	swkbdConfigSetHeaderText(&kbd, header.c_str());
	swkbdConfigSetGuideText(&kbd, guide.c_str());
	swkbdConfigSetInitialText(&kbd, initial.c_str());
	if (maxLen < 1) maxLen = 1;
	if (maxLen > 500) maxLen = 500;
	swkbdConfigSetStringLenMax(&kbd, (u32)maxLen);

	static char buf[4096];
	buf[0] = 0;
	Result rc = swkbdShow(&kbd, buf, sizeof(buf));
	swkbdClose(&kbd);

	// descartar pulsaciones acumuladas mientras estaba abierto el teclado
	{
		InputEvents flush;
		while (flush.update()) { /* descartar */ }
	}

	if (R_FAILED(rc))
		return false;

	out = buf;
	return true;
#else
	(void)header; (void)guide; (void)initial; (void)maxLen; (void)out; (void)numbersOnly; (void)multiline;
	return false;
#endif
}
}
