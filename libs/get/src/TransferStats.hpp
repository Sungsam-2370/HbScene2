#pragma once

// ---------------------------------------------------------------------------
// Estadisticas de la ultima operacion (descarga + extraccion), solo en memoria.
// libget las llena; la UI las lee al terminar para mostrar un resumen.
// Todo inline en este header (sin .cpp) para no tocar el Makefile.
// ---------------------------------------------------------------------------

struct TransferStats
{
	// --- descarga por tramos ---
	unsigned long long dlBytes = 0;   // bytes descargados
	double dlSeconds = 0;             // tiempo total de descarga
	int dlConnsMax = 0;               // conexiones configuradas (maximo usado)
	double dlConnSec = 0;             // suma de (conexiones activas x segundos) con trabajo pendiente
	double dlWorkSec = 0;             // segundos con trabajo pendiente (base del promedio)
	double dlWindowFullSec = 0;       // segundos con conexiones libres esperando a la escritura en la SD
	int dlRetries = 0;                // trozos que hubo que pedir de nuevo

	// --- extraccion ---
	unsigned long long exFiles = 0;
	unsigned long long exBytes = 0;
	double exSeconds = 0;             // tiempo con el zip abierto
	double exOpenMs = 0;              // abrir/crear archivos
	double exInflateMs = 0;           // leer + descomprimir
	double exWriteMs = 0;             // escribir (espera que no se solapo)
	double exCloseMs = 0;             // cerrar

	void reset() { *this = TransferStats(); }
};

inline TransferStats g_xfer;
