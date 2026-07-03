#ifndef SUPPORTER_BENEFIT_H_
#define SUPPORTER_BENEFIT_H_

#include <string>

// ---------------------------------------------------------------------------
// Beneficio para usuarios que apoyan (donadores)
// ---------------------------------------------------------------------------
// El programa busca en sdmc:/atmosphere/automatic_backups/ archivos con
// el patron "**************_PRODINFO.bin" (14 caracteres alfanumericos
// seguidos de "_PRODINFO.bin"), toma esos 14 caracteres como identificador
// de la consola, y los compara contra la lista publicada en "apoyo.json"
// dentro del repositorio (mismo repositorio que valido.json/repo.json).
//
// Formato esperado de apoyo.json:
//   { "ids": [ "AAAAAAAAAAAAAA", "BBBBBBBBBBBBBB", ... ] }
//
// Si algun identificador encontrado en la consola aparece en esa lista,
// el usuario se considera "beneficiario" y no tiene restriccion por
// antiguedad de contenido (ver RecentContentPolicy.hpp). Si no aparece,
// se trata como usuario normal.
// ---------------------------------------------------------------------------

// true si la consola actual fue identificada como beneficiaria (apoyo).
// Se calcula una sola vez al iniciar el programa (ver main.cpp).
extern bool gIsSupporter;

// Realiza la busqueda local + descarga/comparacion contra apoyo.json.
// Debe llamarse despues de init_networking() (usa la misma infraestructura
// de descarga que checkAtmosphereHash()).
bool checkSupporterStatus();

#endif
