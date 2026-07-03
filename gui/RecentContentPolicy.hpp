#ifndef RECENT_CONTENT_POLICY_H_
#define RECENT_CONTENT_POLICY_H_

#include <ctime>

// ---------------------------------------------------------------------------
// Dias de espera para contenido reciente
// ---------------------------------------------------------------------------
// Un componente se considera "reciente" mientras hayan pasado MENOS de
// este numero de dias desde su campo "updated" en repo.json.
//
// Mientras un componente es "reciente", solo los usuarios beneficiarios
// (ver SupporterBenefit.hpp) pueden descargarlo. Los usuarios normales
// deben esperar a que pase este numero de dias desde la ultima
// actualizacion; despues de eso, el componente queda disponible para
// cualquiera sin restriccion.
//
// Esta restriccion es independiente de la proteccion por categoria /
// validacion de hash de package3 (ver ProtectedCategories.hpp): un usuario
// beneficiario evita la espera por antiguedad, pero si esta descargando
// de una categoria protegida sigue necesitando pasar esa otra validacion.
//
// Para cambiar cuantos dias hay que esperar, solo edita el valor de abajo.
// ---------------------------------------------------------------------------
static const int kRecentContentRestrictionDays = 7;

// Devuelve true si, dado el timestamp unix de actualizacion de un paquete
// (Package::getUpdatedAtTimestamp()), ese paquete todavia se considera
// "reciente" (dentro de la ventana de espera) en este momento.
inline bool isPackageRecentlyUpdated(int updatedTimestamp)
{
	if (updatedTimestamp <= 0)
		return false; // sin fecha valida conocida, no se restringe por antiguedad

	time_t now = time(nullptr);
	double diffSeconds = difftime(now, static_cast<time_t>(updatedTimestamp));
	double diffDays = diffSeconds / (60.0 * 60.0 * 24.0);

	return diffDays < static_cast<double>(kRecentContentRestrictionDays);
}

#endif
