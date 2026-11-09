#pragma once

// ---------------------------------------------------------------------------
// Resumen de la ultima descarga + extraccion, para el mensaje final.
//
// INTERRUPTOR: cambia kShowTransferSummary a false para que el usuario final
// no vea nada (el resto del codigo queda igual, no hace falta quitar nada mas).
// ---------------------------------------------------------------------------

#include <cstdio>
#include <string>

#include "../libs/get/src/TransferStats.hpp"

static constexpr bool kShowTransferSummary = true;

namespace TransferSummary
{
inline std::string fmtBytes(unsigned long long b)
{
	char buf[32];
	if (b >= (1ULL << 30))
		snprintf(buf, sizeof(buf), "%.2f GB", (double)b / (1024.0 * 1024.0 * 1024.0));
	else
		snprintf(buf, sizeof(buf), "%.0f MB", (double)b / (1024.0 * 1024.0));
	return buf;
}

inline std::string fmtTime(double secs)
{
	int s = (int)(secs + 0.5);
	char buf[24];
	snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
	return buf;
}

// Texto listo para agregar al mensaje final. Vacio si no hay nada que mostrar.
inline std::string build()
{
	if (!kShowTransferSummary)
		return "";

	const TransferStats& x = g_xfer;
	std::string out;
	char buf[160];

	if (x.dlBytes > 0 && x.dlSeconds > 0.5)
	{
		const double mbs = (double)x.dlBytes / 1048576.0 / x.dlSeconds;
		snprintf(buf, sizeof(buf), "Descarga: %s en %s = %.1f MB/s\n", fmtBytes(x.dlBytes).c_str(),
		         fmtTime(x.dlSeconds).c_str(), mbs);
		out += buf;

		if (x.dlWorkSec > 0.5)
		{
			const double avgConns = x.dlConnSec / x.dlWorkSec;
			const double fullPct = 100.0 * x.dlWindowFullSec / x.dlWorkSec;
			snprintf(buf, sizeof(buf), "Conexiones activas: %.1f de %d | ventana llena: %.0f %% | reintentos: %d\n",
			         avgConns, x.dlConnsMax, fullPct, x.dlRetries);
			out += buf;
		}
	}

	if (x.exBytes > 0 && x.exSeconds > 0.5)
	{
		const double mbs = (double)x.exBytes / 1048576.0 / x.exSeconds;
		snprintf(buf, sizeof(buf), "Extraccion: %s en %s = %.1f MB/s\n", fmtBytes(x.exBytes).c_str(),
		         fmtTime(x.exSeconds).c_str(), mbs);
		out += buf;

		const double files = x.exOpenMs + x.exCloseMs;
		const double sum = x.exInflateMs + x.exWriteMs + files;
		if (sum > 0)
		{
			snprintf(buf, sizeof(buf), "Descomprimir %.0f %% | escribir %.0f %% | archivos %.0f %%\n",
			         100.0 * x.exInflateMs / sum, 100.0 * x.exWriteMs / sum, 100.0 * files / sum);
			out += buf;
		}
	}

	while (!out.empty() && out.back() == '\n')
		out.pop_back();
	return out;
}
} // namespace TransferSummary
