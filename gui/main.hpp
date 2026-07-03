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

// Clave usada para desencriptar apoyo.json (ver SimpleCipher.hpp/.cpp y
// SupporterBenefit.cpp).
//
// Esto usa un XOR de VARIOS BYTES (una "clave" de texto que se repite),
// no de un solo byte como URL_XOR_KEY. La diferencia importa: un XOR de
// un solo byte solo tiene 256 combinaciones — alguien con un editor
// hexadecimal y tiempo puede probarlas todas a mano hasta que el
// resultado se vea como texto legible. Con una clave de varios bytes que
// se repite, ya no alcanza con probar valores de un byte: primero hay
// que notar que se repite, deducir cuantos bytes de largo tiene (analisis
// mas involucrado, no algo que se hace "a ojo" con un editor hex), y
// recien despues intentar recuperar el contenido. Entre mas larga la
// clave, mas dificil.
//
// CAMBIA APOYO_XOR_KEY por la tuya (largo libre, mientras mas larga
// mejor, evita palabras obvias). Tambien cambia el texto de
// _OBF_APOYO_KEY. Ambos deben coincidir EXACTO con lo que uses en el
// script que encripta apoyo.json antes de subirlo al repositorio (ver
// herramientas/encrypt_apoyo.py).
inline constexpr char APOYO_XOR_KEY[] = "Xk9#mQ2vL7pR4wZ!nB6tY3sD8fH1cJ5g";

template<size_t N>
struct ObfStr2 {
    char data[N]{};

    constexpr ObfStr2(const char (&str)[N]) {
        constexpr size_t keyLen = sizeof(APOYO_XOR_KEY) - 1;
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ APOYO_XOR_KEY[i % keyLen];
    }

    std::string decode() const {
        constexpr size_t keyLen = sizeof(APOYO_XOR_KEY) - 1;
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = data[i] ^ APOYO_XOR_KEY[i % keyLen];
        return out;
    }
};

inline constexpr auto _OBF_APOYO_KEY = ObfStr2("CambiaEstaClaveHbScene2026");

// Macros de acceso — el resto del codigo las usa igual que antes.
// decode() se llama una sola vez por uso, el resultado es un std::string normal.
#define META_REPO   (_OBF_META_REPO.decode())
#define SWITCH_REPO (_OBF_SWITCH_REPO.decode())
#define WIIU_REPO   (_OBF_WIIU_REPO.decode())
#define _3DS_REPO   (_OBF_3DS_REPO.decode())
#define WII_REPO    (_OBF_WII_REPO.decode())
#define APOYO_KEY   (_OBF_APOYO_KEY.decode())

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
