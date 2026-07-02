#ifndef PROTECTED_CATEGORIES_H_
#define PROTECTED_CATEGORIES_H_

#include <string>
#include <array>
#include <algorithm>

// ---------------------------------------------------------------------------
// Categorias protegidas por la validacion de hash
// ---------------------------------------------------------------------------
// Esta es la UNICA lista que hay que tocar para agregar o quitar una
// categoria de la validacion.
//
// Cualquier categoria que aparezca aqui (el valor debe coincidir EXACTO
// con el "cat_value" usado en Sidebar.hpp / con el campo "category" del
// repo.json) quedara bloqueada para descargar si la validacion del hash
// de sdmc:/atmosphere/package3 (ver checkAtmosphereHash() en main.cpp)
// no fue exitosa.
//
// Cualquier categoria que NO este en esta lista siempre permite descargar,
// sin importar el resultado de la validacion.
//
// Para agregar una nueva categoria protegida en el futuro, solo agrega
// una linea aqui, por ejemplo:
//
//     "PkUnico",
//     "OtraCategoriaProtegida",
//
// No hace falta tocar ningun otro archivo.
// ---------------------------------------------------------------------------
static const std::array<std::string, 1> kProtectedCategories = {
	"PkUnico",
};

// Devuelve true si la categoria dada requiere pasar la validacion de hash
// para poder descargarse.
inline bool isCategoryProtected(const std::string& category)
{
	return std::find(kProtectedCategories.begin(), kProtectedCategories.end(), category) != kProtectedCategories.end();
}

#endif
