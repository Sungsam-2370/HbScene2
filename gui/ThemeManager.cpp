#include "ThemeManager.hpp"
#include "main.hpp"
#include <ctime>
#include <fstream>
#include <sstream>
#ifdef SWITCH
#include <switch.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#define THEME_CONFIG_PATH "./.theme_config"

namespace HBAS::ThemeManager
{
    ThemeColors getPresetColors(int themeId)
    {
        switch (themeId)
        {
        case THEME_SWITCH_SCENE:
            // Colores actuales de la aplicacion (tema original)
            return {
                {0x0E, 0x40, 0x73, 0xff}, // background
                {0x03, 0x1D, 0x3A, 0xff}, // sidebarColor
                {0xff, 0xff, 0xff, 0xff}, // textPrimary
                {0xd0, 0xd0, 0xd0, 0xff}, // textSecondary
                {0x03, 0x1D, 0x3A, 0xff}, // sidebarTitleBg
                {0x03, 0x1D, 0x3A, 0xff}, // sidebarCategoryBg
                {0x03, 0x1D, 0x3A, 0xff}, // sidebarFooterBg
                {0x10, 0x5A, 0x9C, 0xff}, // categoryHighlight
                {0x10, 0xD9, 0xD9, 0x40}, // dragHighlight
            };

        case THEME_ESHOP1_LIGHT:
            // Estilo clasico Nintendo eShop, fondo claro
            return {
                {0xF4, 0xF4, 0xF4, 0xff}, // background
                {0xE6, 0x00, 0x12, 0xff}, // sidebarColor (rojo Nintendo)
                {0x1A, 0x1A, 0x1A, 0xff}, // textPrimary
                {0x60, 0x60, 0x60, 0xff}, // textSecondary
                {0xE6, 0x00, 0x12, 0xff}, // sidebarTitleBg
                {0xE6, 0x00, 0x12, 0xff}, // sidebarCategoryBg
                {0xC4, 0x00, 0x0F, 0xff}, // sidebarFooterBg
                {0xFF, 0x4D, 0x57, 0xff}, // categoryHighlight
                {0xFF, 0xFF, 0xFF, 0x40}, // dragHighlight
            };

        case THEME_ESHOP1_DARK:
            // Estilo clasico Nintendo eShop, fondo oscuro
            return {
                {0x1C, 0x1C, 0x1C, 0xff}, // background
                {0x8B, 0x00, 0x0C, 0xff}, // sidebarColor (rojo oscuro)
                {0xFF, 0xFF, 0xFF, 0xff}, // textPrimary
                {0xB0, 0xB0, 0xB0, 0xff}, // textSecondary
                {0x8B, 0x00, 0x0C, 0xff}, // sidebarTitleBg
                {0x8B, 0x00, 0x0C, 0xff}, // sidebarCategoryBg
                {0x6E, 0x00, 0x0A, 0xff}, // sidebarFooterBg
                {0xE6, 0x00, 0x12, 0xff}, // categoryHighlight
                {0xFF, 0xFF, 0xFF, 0x30}, // dragHighlight
            };

        case THEME_ESHOP2_LIGHT:
            // Estilo eShop moderno, fondo claro, acentos azules
            return {
                {0xFF, 0xFF, 0xFF, 0xff}, // background
                {0x00, 0x59, 0xC8, 0xff}, // sidebarColor (azul Switch)
                {0x10, 0x10, 0x10, 0xff}, // textPrimary
                {0x55, 0x55, 0x55, 0xff}, // textSecondary
                {0x00, 0x59, 0xC8, 0xff}, // sidebarTitleBg
                {0x00, 0x59, 0xC8, 0xff}, // sidebarCategoryBg
                {0x00, 0x40, 0x9C, 0xff}, // sidebarFooterBg
                {0x33, 0x8C, 0xFF, 0xff}, // categoryHighlight
                {0x00, 0x59, 0xC8, 0x40}, // dragHighlight
            };

        case THEME_ESHOP2_DARK:
            // Estilo eShop moderno, fondo oscuro, acentos azules
            return {
                {0x12, 0x16, 0x1F, 0xff}, // background
                {0x00, 0x2B, 0x66, 0xff}, // sidebarColor
                {0xF0, 0xF0, 0xF0, 0xff}, // textPrimary
                {0x9A, 0x9A, 0xA5, 0xff}, // textSecondary
                {0x00, 0x2B, 0x66, 0xff}, // sidebarTitleBg
                {0x00, 0x2B, 0x66, 0xff}, // sidebarCategoryBg
                {0x00, 0x1C, 0x42, 0xff}, // sidebarFooterBg
                {0x00, 0x59, 0xC8, 0xff}, // categoryHighlight
                {0x33, 0x8C, 0xFF, 0x40}, // dragHighlight
            };

        default:
            // THEME_CUSTOM u otro valor invalido: devolver el switch scene como respaldo
            return getPresetColors(THEME_SWITCH_SCENE);
        }
    }

    void applyTheme(int themeId)
    {
        if (themeId < 0 || themeId >= THEME_TOTAL)
            themeId = THEME_SWITCH_SCENE;

        currentTheme = themeId;

        ThemeColors colors = (themeId == THEME_CUSTOM)
            ? customColors
            : getPresetColors(themeId);

        background          = colors.background;
        sidebarColor         = colors.sidebarColor;
        textPrimary          = colors.textPrimary;
        textSecondary        = colors.textSecondary;
        sidebarTitleBg       = colors.sidebarTitleBg;
        sidebarCategoryBg    = colors.sidebarCategoryBg;
        sidebarFooterBg      = colors.sidebarFooterBg;
        categoryHighlight    = colors.categoryHighlight;
        dragHighlight        = colors.dragHighlight;
    }

    // Convierte un CST_Color a una linea de texto "r,g,b,a"
    static std::string colorToLine(const CST_Color& c)
    {
        std::stringstream ss;
        ss << (int)c.r << "," << (int)c.g << "," << (int)c.b << "," << (int)c.a;
        return ss.str();
    }

    // Lee una linea de texto "r,g,b,a" y la convierte a CST_Color
    static CST_Color lineToColor(const std::string& line, CST_Color fallback)
    {
        int r, g, b, a;
        char comma;
        std::stringstream ss(line);
        if (ss >> r >> comma >> g >> comma >> b >> comma >> a)
        {
            return { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
        }
        return fallback;
    }

    void saveThemePreference()
    {
        std::ofstream file(THEME_CONFIG_PATH);
        if (!file.is_open())
            return;

        file << currentTheme << "\n";

        // si el tema activo es el personalizado, tambien guardamos sus colores
        if (currentTheme == THEME_CUSTOM)
        {
            file << colorToLine(customColors.background) << "\n";
            file << colorToLine(customColors.sidebarColor) << "\n";
            file << colorToLine(customColors.textPrimary) << "\n";
            file << colorToLine(customColors.textSecondary) << "\n";
            file << colorToLine(customColors.sidebarTitleBg) << "\n";
            file << colorToLine(customColors.sidebarCategoryBg) << "\n";
            file << colorToLine(customColors.sidebarFooterBg) << "\n";
            file << colorToLine(customColors.categoryHighlight) << "\n";
            file << colorToLine(customColors.dragHighlight) << "\n";
        }

        file.close();
    }

    void loadThemePreference()
    {
        std::ifstream file(THEME_CONFIG_PATH);
        if (!file.is_open())
        {
            // no hay preferencia guardada, usar el tema por defecto
            applyTheme(THEME_SWITCH_SCENE);
            return;
        }

        std::string line;
        int themeId = THEME_SWITCH_SCENE;

        if (std::getline(file, line))
        {
            try { themeId = std::stoi(line); }
            catch (...) { themeId = THEME_SWITCH_SCENE; }
        }

        if (themeId == THEME_CUSTOM)
        {
            ThemeColors loaded = customColors; // valores de respaldo

            if (std::getline(file, line)) loaded.background       = lineToColor(line, loaded.background);
            if (std::getline(file, line)) loaded.sidebarColor      = lineToColor(line, loaded.sidebarColor);
            if (std::getline(file, line)) loaded.textPrimary       = lineToColor(line, loaded.textPrimary);
            if (std::getline(file, line)) loaded.textSecondary     = lineToColor(line, loaded.textSecondary);
            if (std::getline(file, line)) loaded.sidebarTitleBg    = lineToColor(line, loaded.sidebarTitleBg);
            if (std::getline(file, line)) loaded.sidebarCategoryBg = lineToColor(line, loaded.sidebarCategoryBg);
            if (std::getline(file, line)) loaded.sidebarFooterBg   = lineToColor(line, loaded.sidebarFooterBg);
            if (std::getline(file, line)) loaded.categoryHighlight = lineToColor(line, loaded.categoryHighlight);
            if (std::getline(file, line)) loaded.dragHighlight     = lineToColor(line, loaded.dragHighlight);

            customColors = loaded;
        }

        file.close();
        applyTheme(themeId);
    }

    void themeManagerInit()
    {
        bool canDetectDarkMode = false;

        // Detect if Switch is using dark theme
#ifdef SWITCH
        setsysInitialize();
        static ColorSetId sysTheme = ColorSetId_Light;
        setsysGetColorSetId(&sysTheme);
        isDarkMode = (sysTheme == ColorSetId_Dark);
        setsysExit();
        canDetectDarkMode = true;
#endif

#ifdef __WIIU__
        // TODO: Check if a custom dark theme is being used
#endif

#ifdef __APPLE__
        if (system("defaults read -g AppleInterfaceStyle 2>/dev/null") == 0) {
            isDarkMode = true;
        }
        canDetectDarkMode = true;
#endif

#ifdef _WIN32
        HKEY hKey;
        DWORD dwRegValue, dwRegType, dwRegSize = sizeof(DWORD);
        if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            if (RegQueryValueEx(hKey, TEXT("AppsUseLightTheme"), NULL, &dwRegType, (LPBYTE)&dwRegValue, &dwRegSize) == ERROR_SUCCESS)
            {
                isDarkMode = !dwRegValue;
            }
            RegCloseKey(hKey);
        }
        canDetectDarkMode = true;
#endif

        if (!canDetectDarkMode) {
            // we can't detect dark mode on this platform, so let's check the time of day
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            isDarkMode = (tm->tm_hour < 5 || tm->tm_hour > 20);
        }

        // Cargar el tema guardado por el usuario (si existe). Si no hay nada
        // guardado, loadThemePreference aplica THEME_SWITCH_SCENE por defecto,
        // que mantiene el comportamiento original de la aplicacion.
        loadThemePreference();
    }
}
