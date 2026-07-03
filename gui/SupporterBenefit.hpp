#ifndef SUPPORTER_BENEFIT_H_
#define SUPPORTER_BENEFIT_H_

#include <string>

// ---------------------------------------------------------------------------
// Beneficio para usuarios que apoyan (donadores)
// ---------------------------------------------------------------------------
// El programa busca en sdmc:/atmosphere/automatic_backups/ archivos con
// el patron "**************_PRODINFO.bin" (14 caracteres alfanumericos
// seguidos de "_PRODINFO.bin"). El nombre completo se usa solo para
// validar el patron del archivo; por privacidad, SOLO se conservan en
// memoria y se comparan los ULTIMOS 7 caracteres de esos 14 (informacion
// parcial, nunca el identificador completo de la consola).
//
// Esos 7 caracteres se comparan contra la lista publicada en "apoyo.json"
// dentro del repositorio (mismo repositorio que valido.json/repo.json).
//
// Formato esperado de apoyo.json (ids de 7 caracteres):
//   { "ids": [ "AAAAAAA", "BBBBBBB", ... ] }
//
// Si algun identificador parcial encontrado en la consola aparece en esa
// lista, el usuario se considera "beneficiario" y no tiene restriccion por
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
