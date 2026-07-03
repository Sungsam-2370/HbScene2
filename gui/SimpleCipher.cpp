#include "SimpleCipher.hpp"

#include <array>
#include <cstdint>
#include <utility>

std::string base64Decode(const std::string& input)
{
	static const std::string chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::array<int, 256> lookup{};
	lookup.fill(-1);
	for (size_t i = 0; i < chars.size(); i++)
		lookup[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);

	std::string out;
	out.reserve(input.size() / 4 * 3);

	int val = 0;
	int bits = -8;
	for (unsigned char c : input)
	{
		if (c == '=')
			break; // relleno de base64, fin de los datos utiles

		if (lookup[c] == -1)
			continue; // ignorar saltos de linea / espacios / caracteres invalidos

		val = (val << 6) + lookup[c];
		bits += 6;

		if (bits >= 0)
		{
			out.push_back(static_cast<char>((val >> bits) & 0xFF));
			bits -= 8;
		}
	}

	return out;
}

std::string rc4(const std::string& data, const std::string& key)
{
	if (key.empty())
		return data; // sin clave no hay nada que hacer (evita division por cero)

	// 1. Inicializar el arreglo de permutacion S con la clave (KSA)
	uint8_t s[256];
	for (int i = 0; i < 256; i++)
		s[i] = static_cast<uint8_t>(i);

	int j = 0;
	for (int i = 0; i < 256; i++)
	{
		j = (j + s[i] + static_cast<uint8_t>(key[i % key.size()])) & 0xFF;
		std::swap(s[i], s[j]);
	}

	// 2. Generar el keystream y aplicar XOR byte a byte (PRGA)
	std::string out(data.size(), '\0');
	int i = 0;
	j = 0;
	for (size_t n = 0; n < data.size(); n++)
	{
		i = (i + 1) & 0xFF;
		j = (j + s[i]) & 0xFF;
		std::swap(s[i], s[j]);

		uint8_t k = s[(s[i] + s[j]) & 0xFF];
		out[n] = static_cast<char>(static_cast<uint8_t>(data[n]) ^ k);
	}

	return out;
}
