#pragma once

// Correccion de la fecha/hora de la consola al iniciar.
//
// Con la fecha mal, las conexiones HTTPS fallan (el certificado "aun no es valido" o
// "ya vencio") y no carga ni la lista ni las actualizaciones. Aqui se consulta la hora
// real por NTP (UDP, no depende de HTTPS) y, solo si la consola esta claramente
// desfasada, se corrige el reloj. Todo en este header (sin .cpp) para no tocar el Makefile.
//
// Tiempo maximo si no hay respuesta: ~1 s por servidor (3 servidores).

#include <cstdint>
#include <cstdio>

#if defined(SWITCH)

#include <switch.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

namespace ClockSync {

// diferencia (segundos) a partir de la cual se considera que la hora esta mal
constexpr long long kMaxDriftSeconds = 120;

// Pregunta la hora a un servidor NTP. Devuelve true y la hora Unix (UTC) si la
// respuesta es valida.
inline bool queryNtp(const sockaddr_in& addr, int timeoutMs, uint64_t& unixOut)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
		return false;

	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	unsigned char msg[48] = {0};
	msg[0] = 0x1B; // LI = 0, version 3, modo cliente

	bool ok = false;
	if (sendto(fd, msg, sizeof(msg), 0, (const struct sockaddr*)&addr, sizeof(addr)) == (ssize_t)sizeof(msg))
	{
		unsigned char resp[48] = {0};
		struct sockaddr_in from;
		socklen_t fromLen = sizeof(from);
		ssize_t n = recvfrom(fd, resp, sizeof(resp), 0, (struct sockaddr*)&from, &fromLen);
		if (n >= (ssize_t)sizeof(resp))
		{
			const int mode = resp[0] & 7;
			const int stratum = resp[1];
			const uint32_t secs = ((uint32_t)resp[40] << 24) | ((uint32_t)resp[41] << 16) |
			                      ((uint32_t)resp[42] << 8) | (uint32_t)resp[43];
			// modo 4 = respuesta de servidor; estrato 1..15 = servidor sincronizado
			if (mode == 4 && stratum >= 1 && stratum <= 15 && secs != 0)
			{
				// NTP cuenta desde 1900. Despues de 2036 el campo da la vuelta (era 1).
				uint64_t unixT = (secs >= 2208988800u) ? ((uint64_t)secs - 2208988800u)
				                                       : ((uint64_t)secs + 2085978496u);
				if (unixT > 1735689600ull) // sanidad: posterior al 1/1/2025
				{
					unixOut = unixT;
					ok = true;
				}
			}
		}
	}
	close(fd);
	return ok;
}

// Obtiene la hora real probando varios servidores (primero por IP, sin DNS).
inline bool fetchNtpTime(uint64_t& unixOut)
{
	static const char* const kIps[] = { "162.159.200.1", "216.239.35.0" }; // Cloudflare, Google
	for (const char* ip : kIps)
	{
		struct sockaddr_in a;
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_port = htons(123);
		if (inet_pton(AF_INET, ip, &a.sin_addr) == 1 && queryNtp(a, 1000, unixOut))
			return true;
	}

	// ultimo intento: por nombre (necesita DNS)
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo("pool.ntp.org", "123", &hints, &res) == 0 && res)
	{
		bool ok = false;
		if (res->ai_addrlen >= sizeof(struct sockaddr_in))
		{
			struct sockaddr_in a;
			memcpy(&a, res->ai_addr, sizeof(a));
			a.sin_port = htons(123);
			ok = queryNtp(a, 1000, unixOut);
		}
		freeaddrinfo(res);
		return ok;
	}
	return false;
}

// Corrige el reloj de la consola si esta desfasado mas de kMaxDriftSeconds.
// Devuelve true si la hora queda correcta (ya lo estaba, o se corrigio).
// Si no hay internet o algo falla devuelve false y no cambia nada.
inline bool syncSystemClock()
{
	if (R_FAILED(timeInitialize()))
	{
		printf("[clock] no se pudo iniciar el servicio de hora\n");
		return false;
	}

	bool result = false;
	uint64_t ntp = 0;
	if (!fetchNtpTime(ntp))
	{
		printf("[clock] sin respuesta de NTP, no se cambia la hora\n");
	}
	else
	{
		uint64_t cur = 0;
		const bool haveCur = R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &cur));
		const long long drift = haveCur ? (long long)cur - (long long)ntp : (long long)1 << 40;

		if (haveCur && llabs(drift) <= kMaxDriftSeconds)
		{
			printf("[clock] hora correcta (desfase %lld s)\n", drift);
			result = true;
		}
		else
		{
			printf("[clock] hora desfasada (%lld s), corrigiendo...\n", drift);

			auto isFixed = []() {
				uint64_t now = 0;
				return R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &now)) ? now : (uint64_t)0;
			};

			Result rc = timeSetCurrentTime(TimeType_UserSystemClock, ntp);
			uint64_t after = isFixed();
			bool ok = R_SUCCEEDED(rc) && after != 0 && llabs((long long)after - (long long)ntp) <= 30;

			if (!ok)
			{
				// respaldo: reloj de red (el del usuario lo sigue si la sincronizacion
				// automatica esta activada en los ajustes de la consola)
				printf("[clock] reloj de usuario no cambio (rc=0x%x), probando reloj de red\n", (unsigned)rc);
				timeSetCurrentTime(TimeType_NetworkSystemClock, ntp);
				after = isFixed();
				ok = after != 0 && llabs((long long)after - (long long)ntp) <= 30;
			}

			printf("[clock] %s\n", ok ? "hora corregida" : "no se pudo corregir la hora");
			result = ok;
		}
	}

	timeExit();
	return result;
}

} // namespace ClockSync

#else

namespace ClockSync {
inline bool syncSystemClock() { return false; } // solo Switch
}

#endif
