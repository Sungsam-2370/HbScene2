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
inline constexpr auto _OBF_META_REPO   = ObfStr("https://raw.githubusercontent.com/Sungsam-2370/Delta/main");
inline constexpr auto _OBF_SWITCH_REPO = ObfStr("https://raw.githubusercontent.com/Sungsam-2370/Delta/main");
inline constexpr auto _OBF_WIIU_REPO   = ObfStr("https://wiiu.cdn.fortheusers.org");
inline constexpr auto _OBF_3DS_REPO    = ObfStr("https://3ds.apps.fortheusers.org");
inline constexpr auto _OBF_WII_REPO    = ObfStr("https://hbb1.oscwii.org");

// Clave usada para desencriptar apoyo.json (ver SimpleCipher.hpp/.cpp y
// SupporterBenefit.cpp).
//
// Esto usa un XOR de VARIOS BYTES (una "clave" que se repite), no de un
// solo byte como URL_XOR_KEY. La diferencia importa: un XOR de un solo
// byte solo tiene 256 combinaciones — alguien con un editor hexadecimal
// y tiempo puede probarlas todas a mano. Con una clave de varios bytes
// que se repite, ya no alcanza con probar valores de un byte.
//
// IMPORTANTE: la clave NO se escribe como texto literal (ej. antes era
// "Xk9#mQ2vL7pR4wZ!..."). Motivo: decode() corre en tiempo de EJECUCION,
// asi que el compilador esta obligado a guardar la clave en el binario
// para que decode() la pueda leer — no hay forma de evitar que la clave
// exista en el binario. Pero si la escribimos como texto, ese texto queda
// grabado tal cual, legible con "strings" o un editor hexadecimal,
// justo al lado de los bytes cifrados: alguien la encuentra en segundos
// y con eso descifra todo sin necesidad de romper nada.
//
// La solucion es generar la clave con una formula (un generador
// pseudoaleatorio simple, evaluado en tiempo de compilacion) en vez de
// escribirla como palabras. El resultado son bytes que cubren todo el
// rango 0-255, asi que no forman un texto legible y "strings" no los
// separa del resto de bytes "ruido" del binario.
//
// Si necesitas cambiar la clave: cambia el numero semilla de
// APOYO_KEY_GEN (cualquier valor de 32 bits). Eso sí, si cambias la
// semilla, apoyo.json deja de coincidir con lo que ya subiste al
// repositorio — habria que volver a generarlo con encrypt_apoyo.py
// usando la MISMA clave resultante (ver APOYO_KEY mas abajo, que sigue
// siendo el string que ese script necesita; lo que cambio es como se
// protege dentro del binario, no el valor final usado para RC4).
template<size_t N>
struct GenKey {
    unsigned char bytes[N]{};

    constexpr GenKey(uint32_t seed) {
        uint32_t x = seed ? seed : 1;
        for (size_t i = 0; i < N; i++) {
            // xorshift simple, valido en contexto constexpr
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            bytes[i] = static_cast<unsigned char>(x);
        }
    }
};

// Cada clave tiene su propia semilla — no se comparten entre si, asi
// que aunque alguien recupere una (por ejemplo por texto plano conocido,
// ver comentario de BACKUPS_DIR_KEY_GEN abajo), eso NO le sirve para
// descifrar las demas cadenas.
inline constexpr GenKey<32> APOYO_KEY_GEN(0x9E3779B1u);

template<size_t N>
struct ObfStr2 {
    char data[N]{};

    constexpr ObfStr2(const char (&str)[N]) {
        constexpr size_t keyLen = 32;
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ APOYO_KEY_GEN.bytes[i % keyLen];
    }

    std::string decode() const {
        constexpr size_t keyLen = 32;
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = data[i] ^ APOYO_KEY_GEN.bytes[i % keyLen];
        return out;
    }
};

inline constexpr auto _OBF_APOYO_KEY = ObfStr2("PoCj2H4mvpSUaDMYL8k15Impv");

// ------------------------------------------------------------------
// Ofuscacion de rutas/nombres sensibles usados en SupporterBenefit.cpp
// (carpeta de backups de PRODINFO, sufijo esperado del backup, y el
// nombre del archivo de acceso alterno). Mismo esquema de arriba
// (clave generada, no escrita como texto), pero con dos claves propias:
//
// - BACKUPS_DIR_KEY_GEN: SOLO para la carpeta de backups. Esta ruta
//   ("sdmc:/atmosphere/automatic_backups") es una convencion PUBLICA de
//   Atmosphere, no es secreta — cualquiera de la escena la conoce. Eso
//   significa que si alguien la escribe en texto plano y le hace XOR
//   contra el blob cifrado, recupera la clave sin esfuerzo (ataque de
//   texto plano conocido). Por eso esta cadena tiene su PROPIA clave,
//   aislada: aunque se rompa, no sirve para descifrar nada mas.
//
// - SUPPORTER_KEY_GEN: para el sufijo del backup y el nombre del
//   archivo de acceso alterno, que no son publicos/adivinables de la
//   misma forma.
// ------------------------------------------------------------------
inline constexpr GenKey<28> BACKUPS_DIR_KEY_GEN(0x2545F491u);
inline constexpr GenKey<28> SUPPORTER_KEY_GEN(0xC2B2AE3Du);

template<size_t N>
struct ObfStr3 {
    char data[N]{};

    constexpr ObfStr3(const char (&str)[N]) {
        constexpr size_t keyLen = 28;
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ SUPPORTER_KEY_GEN.bytes[i % keyLen];
    }

    std::string decode() const {
        constexpr size_t keyLen = 28;
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = data[i] ^ SUPPORTER_KEY_GEN.bytes[i % keyLen];
        return out;
    }
};

template<size_t N>
struct ObfStr4 {
    char data[N]{};

    constexpr ObfStr4(const char (&str)[N]) {
        constexpr size_t keyLen = 28;
        for (size_t i = 0; i < N; i++)
            data[i] = str[i] ^ BACKUPS_DIR_KEY_GEN.bytes[i % keyLen];
    }

    std::string decode() const {
        constexpr size_t keyLen = 28;
        std::string out(N - 1, '\0');
        for (size_t i = 0; i < N - 1; i++)
            out[i] = data[i] ^ BACKUPS_DIR_KEY_GEN.bytes[i % keyLen];
        return out;
    }
};

inline constexpr auto _OBF_BACKUPS_DIR        = ObfStr4("sdmc:/atmosphere/automatic_backups");
inline constexpr auto _OBF_BACKUP_SUFFIX      = ObfStr3("_PRODINFO.bin");
inline constexpr auto _OBF_MANUAL_ACCESS_FILE = ObfStr3("ApoyoGrupo0042.ini");

// Macros de acceso — el resto del codigo las usa igual que antes.
// decode() se llama una sola vez por uso, el resultado es un std::string normal.
#define META_REPO           (_OBF_META_REPO.decode())
#define SWITCH_REPO          (_OBF_SWITCH_REPO.decode())
#define WIIU_REPO            (_OBF_WIIU_REPO.decode())
#define _3DS_REPO            (_OBF_3DS_REPO.decode())
#define WII_REPO             (_OBF_WII_REPO.decode())
#define APOYO_KEY            (_OBF_APOYO_KEY.decode())
#define BACKUPS_DIR          (_OBF_BACKUPS_DIR.decode())
#define BACKUP_SUFFIX        (_OBF_BACKUP_SUFFIX.decode())
#define MANUAL_ACCESS_FILE   (_OBF_MANUAL_ACCESS_FILE.decode())


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
