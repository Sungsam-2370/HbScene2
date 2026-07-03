#ifndef SIMPLE_CIPHER_H_
#define SIMPLE_CIPHER_H_

#include <string>

// ---------------------------------------------------------------------------
// Cifrado simetrico simple (RC4) + base64. Se usa UNICAMENTE para que
// apoyo.json no se pueda leer en texto plano si alguien entra al
// repositorio (ver SupporterBenefit.cpp).
//
// QUE SI PROTEGE:
//   Evita que alguien que navega el repositorio de GitHub casualmente
//   vea la lista de seriales parciales en texto plano.
//
// QUE NO PROTEGE:
//   No es seguridad real contra alguien que se toma el trabajo de
//   analizar el .nro compilado y extraer la clave (esta ofuscada en el
//   binario, no oculta de verdad). Cualquier cifrado del lado del
//   cliente tiene esta misma limitacion de fondo — es la misma logica
//   que ya se usa para ofuscar las URLs del repositorio en main.hpp.
// ---------------------------------------------------------------------------

// Decodifica un string en base64 a los bytes originales. El resultado es
// binario y puede contener bytes 0x00, por eso se maneja como std::string
// con tamaño explicito (nunca hay que usarlo con funciones que dependan
// de un caracter nulo de terminacion, como c_str() para longitud).
std::string base64Decode(const std::string& input);

// Cifra o descifra con RC4 (es simetrico: la misma funcion sirve para
// ambas direcciones, solo cambia si a "data" le pasas texto plano o
// texto cifrado).
std::string rc4(const std::string& data, const std::string& key);

#endif
