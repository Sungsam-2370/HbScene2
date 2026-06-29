#pragma once
#include <string>
#include <cstdint>

// ============================================================
//  Ofuscacion de URLs en compilacion (compile-time XOR)
//  Las URLs no aparecen en texto plano en el binario.
//  Para cambiar una URL: edita el string dentro de ObfStr()
//  y recompila. No se necesita ningun script externo.
// ============================================================

// Clave XOR — puedes cambiarla a cualquier valor entre 0x01 y 0xFE
static constexpr uint8_t URL_XOR_KEY = 0x5A;

template<size_t N>
struct ObfStr {
    char data[N]{};

    constexpr ObfStr(const char (&str)[N]) {
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ URL_XOR_KEY;
    }

    std::string decode() const {
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = data[i] ^ URL_XOR_KEY;
        return out;
    }
};

// URLs ofuscadas — inline constexpr garantiza una sola instancia en el binario
// (a diferencia de static constexpr, que crea una copia por cada .cpp que incluye este header)
inline constexpr auto _OBF_META_REPO   = ObfStr("https://raw.githubusercontent.com/Sungsam-2370/Alpha/main");
inline constexpr auto _OBF_SWITCH_REPO = ObfStr("https://raw.githubusercontent.com/Sungsam-2370/Alpha/main");
inline constexpr auto _OBF_WIIU_REPO   = ObfStr("https://wiiu.cdn.fortheusers.org");
inline constexpr auto _OBF_3DS_REPO    = ObfStr("https://3ds.apps.fortheusers.org");
inline constexpr auto _OBF_WII_REPO    = ObfStr("https://hbb1.oscwii.org");

// Macros de acceso — el resto del codigo las usa igual que antes.
// decode() se llama una sola vez por uso, el resultado es un std::string normal.
#define META_REPO   (_OBF_META_REPO.decode())
#define SWITCH_REPO (_OBF_SWITCH_REPO.decode())
#define WIIU_REPO   (_OBF_WIIU_REPO.decode())
#define _3DS_REPO   (_OBF_3DS_REPO.decode())
#define WII_REPO    (_OBF_WII_REPO.decode())

// DEFAULT_REPO segun plataforma (igual que antes)
#if defined(SWITCH)
#define DEFAULT_REPO SWITCH_REPO
#elif defined(WII)
#define DEFAULT_REPO WII_REPO
#elif defined(_3DS)
#define DEFAULT_REPO _3DS_REPO
#else
#define DEFAULT_REPO WIIU_REPO
#endif

// preference paths (no son sensibles, se dejan como estan)
#define SOUND_PATH       "./.toggle_sound"
#define DEFAULT_GET_HOME "./.get/"

// self-update: JSON con la version mas reciente publicada
// SELF_UPDATE_URL concatena META_REPO (std::string) con el sufijo
#define SELF_UPDATE_URL (META_REPO + "/version.json")

// ruta absoluta del .nro en la SD (donde se reemplaza al actualizar)
#define APP_NRO_PATH "sdmc:/switch/scene_eshop/scene_eshop.nro"
// ruta temporal durante la descarga (se renombra al .nro solo si termina bien)
#define APP_NRO_TMP  "sdmc:/switch/scene_eshop/scene_eshop.nro.tmp"

// Switch Scene validation
// true  = carpeta sdmc:/config/switch-scene/Overlays existe → acceso completo
// false = carpeta no encontrada → navegacion libre, descargas bloqueadas
extern bool gSwitchSceneValid;

// Atmosphere hash validation
// true  = sdmc:/atmosphere/package3 existe y su SHA-256 coincide con el esperado
// false = archivo no encontrado o hash incorrecto → descargas bloqueadas
extern bool gAtmosphereValid;
extern std::string gAtmosphereDebugMsg;
