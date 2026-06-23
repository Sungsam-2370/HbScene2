#pragma once
#include "../libs/chesto/src/DrawUtils.hpp"
#include <string>
#include <vector>

namespace HBAS::ThemeManager
{
    // Misc variables
    inline bool isDarkMode = false;

    // ------------------------------------------------------------------
    // Identificador de los temas disponibles
    // ------------------------------------------------------------------
    enum ThemeId
    {
        THEME_SWITCH_SCENE = 0,
        THEME_ESHOP1_LIGHT = 1,
        THEME_ESHOP1_DARK  = 2,
        THEME_ESHOP2_LIGHT = 3,
        THEME_ESHOP2_DARK  = 4,
        THEME_CUSTOM       = 5,
        THEME_TOTAL        = 6
    };

    // Nombres de los temas, en el mismo orden que ThemeId
    inline const char* themeNames[THEME_TOTAL] = {
        "Switch Scene",
        "Eshop 1 Claro",
        "Eshop 1 Oscuro",
        "Eshop 2 Claro",
        "Eshop 2 Oscuro",
        "Personalizado"
    };

    // El tema actualmente activo
    inline int currentTheme = THEME_SWITCH_SCENE;

    // ------------------------------------------------------------------
    // Colores activos (los que usa toda la aplicacion al renderizar)
    // ------------------------------------------------------------------
    inline CST_Color background     = {0xff, 0xff, 0xff, 0xff};
    inline CST_Color sidebarColor   = {0x03, 0x1D, 0x3A, 0xff};
    inline CST_Color textPrimary    = {0x00, 0x00, 0x00, 0xff};
    inline CST_Color textSecondary  = {0x50, 0x50, 0x50, 0xff};
    inline CST_Color textCard       = {0x00, 0x00, 0x00, 0xff}; // texto nombre en tarjetas del menu principal
    inline CST_Color textDescription= {0x20, 0x20, 0x20, 0xff}; // texto de descripcion al entrar a un componente

    // Las 3 secciones del sidebar izquierdo
    inline CST_Color sidebarTitleBg    = {0x03, 0x1D, 0x3A, 0xff}; // seccion superior (titulo/logo)
    inline CST_Color sidebarCategoryBg = {0x03, 0x1D, 0x3A, 0xff}; // seccion media (categorias)
    inline CST_Color sidebarFooterBg   = {0x03, 0x1D, 0x3A, 0xff}; // seccion inferior (expandir/contraer)

    // Color de la categoria resaltada/seleccionada en el sidebar
    inline CST_Color categoryHighlight = {0x0E, 0x40, 0x73, 0xff};

    // Color del recuadro que se mueve al arrastrar (drag) en el sidebar
    inline CST_Color dragHighlight = {0x10, 0xD9, 0xD9, 0x40};

    // ------------------------------------------------------------------
    // Estructura que agrupa todos los colores de un tema, para poder
    // definir los 5 presets y el personalizado de forma compacta
    // ------------------------------------------------------------------
    struct ThemeColors
    {
        CST_Color background;
        CST_Color sidebarColor;
        CST_Color textPrimary;
        CST_Color textSecondary;
        CST_Color textCard;
        CST_Color textDescription;
        CST_Color sidebarTitleBg;
        CST_Color sidebarCategoryBg;
        CST_Color sidebarFooterBg;
        CST_Color categoryHighlight;
        CST_Color dragHighlight;
    };

    // Colores guardados para el tema personalizado (se cargan/guardan en disco)
    inline ThemeColors customColors = {
        {0xff, 0xff, 0xff, 0xff}, // background
        {0x03, 0x1D, 0x3A, 0xff}, // sidebarColor
        {0x00, 0x00, 0x00, 0xff}, // textPrimary
        {0x50, 0x50, 0x50, 0xff}, // textSecondary
        {0x00, 0x00, 0x00, 0xff}, // textCard
        {0x20, 0x20, 0x20, 0xff}, // textDescription
        {0x03, 0x1D, 0x3A, 0xff}, // sidebarTitleBg
        {0x03, 0x1D, 0x3A, 0xff}, // sidebarCategoryBg
        {0x03, 0x1D, 0x3A, 0xff}, // sidebarFooterBg
        {0x0E, 0x40, 0x73, 0xff}, // categoryHighlight
        {0x10, 0xD9, 0xD9, 0x40}, // dragHighlight
    };

    // Devuelve los colores predefinidos de un tema (no aplica al personalizado)
    ThemeColors getPresetColors(int themeId);

    // Aplica un tema (predefinido o personalizado) a las variables activas de arriba
    void applyTheme(int themeId);

    // Guarda en disco el tema actualmente seleccionado y, si es personalizado,
    // tambien los colores elegidos por el usuario
    void saveThemePreference();

    // Carga desde disco el tema guardado (se llama una vez al iniciar el programa)
    void loadThemePreference();

    void themeManagerInit();
}
