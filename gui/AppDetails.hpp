#ifndef APPDETAILS_H_
#define APPDETAILS_H_

#include "AppDetailsContent.hpp"
#include "AppCard.hpp"
#include "DimOverlay.hpp"

class AppList;

class AppDetails : public Element
{
public:
	AppDetails(Package& package, AppList* appList, AppCard* appCard = NULL);
	~AppDetails();

	std::string getPackageDetails(Package* package);
	std::string getAction(Package* package);
	bool process(InputEvents* event);
	void render(Element* parent);
	bool launchFile(char* path, char* context);
	bool themeInstall(char* installerPath);
	void getSupported();

	CST_Color red = { 0xFF, 0x00, 0x00, 0xff };
	CST_Color gray = { 0x50, 0x50, 0x50, 0xff };
	CST_Color black = { 0x00, 0x00, 0x00, 0xff };
	CST_Color white = { 0xFF, 0xFF, 0xFF, 0xff };
	CST_Color sdGood = { 0x5C, 0xE0, 0x7A, 0xff }; // espacio suficiente (verde)
	CST_Color sdBad = { 0xFF, 0x5A, 0x5A, 0xff };  // espacio insuficiente (rojo)

	bool operating = false;
	Package* package = NULL;
	Get* get = NULL;
	AppList* appList = NULL;
	AppCard* appCard = NULL;
	ProgressBar* pbar = NULL;
	int highlighted = -1;

	bool canLaunch = false;

	// the callback method to update the currently displayed pop up (and variables it needs)
	static int updateCurrentlyDisplayedPopup(void* clientp, double dlnow);
	static int updatePopupStatus(int status, int num = 1, int num_total = 1);
	static int lastFrameTime;

	// cancelacion: mantener B 5 segundos mientras se descarga/extrae/instala.
	// pollCancel revisa la tecla (y atiende los eventos pendientes); force =
	// true la ignora el limitador de frecuencia. pollCancelHook es la version
	// sin argumentos que se engancha a libget (g_libget_poll).
	static void pollCancel(bool force);
	static void pollCancelHook();

	void proceed();
	void removePackage();
	void back();
	void launch();

	void moreByAuthor();
	void leaveFeedback();

	void preInstallHook();
	void postInstallHook();

	ProgressBar downloadProgress;

	// on some platform + package combinations, we need to quit after installing
	bool quitAfterInstall = false;

private:
	// true si la operacion en curso es borrar (boton Borrar, o componente ya instalado)
	bool isUninstall() const;
	// acomoda los botones en la barra inferior
	void layoutFooter();
	bool forceUninstall = false;
	bool hasRemoveBtn = false;

	Button* start = nullptr;
	TextElement* errorText = nullptr;
	Button download;
	Button removeBtn; // "Borrar" cuando hay una actualizacion pendiente
	Button cancel;
	TextElement details;
	TextElement sdFree;    // titulo: espacio libre en la SD (siempre blanco)
	TextElement sdFreeVal; // cantidad (verde / rojo)
	TextElement sdNeed;    // titulo: espacio recomendado (siempre blanco)
	TextElement sdNeedVal; // cantidad (verde / rojo)
	AppDetailsContent content;
	TextElement downloadStatus;
	TextElement downloadPercent;
	TextElement cancelHint; // cuenta regresiva / "Cancelando..." (el aviso normal es hintLeft + icono + hintRight)
	TextElement downloadSpeed; // "12.4 MB/s  |  8 conexiones" (solo mientras se descarga)
	bool showSpeed = false;    // true cuando la fase actual es la descarga

	// aviso normal de cancelacion: "Mantén [B] 5 segundos para cancelar" (texto + icono + texto)
	TextElement hintLeft;
	TextElement hintRight;
	ImageElement hintIcon;
	int hintY = 0;
	void showHintIdle();                       // muestra el aviso normal con el icono de B
	void showHintText(const std::string& text); // muestra solo un texto (cuenta regresiva)

	// pantalla de descanso (OLED): "Pulsa [+] para apagar la pantalla" al pie y, al pulsar +,
	// pantalla negra con el porcentaje y la velocidad moviendose despacio. Cualquier boton
	// o toque la despierta; al terminar la operacion vuelve sola.
	TextElement plusLeft;
	TextElement plusRight;
	TextElement plusFallback;
	ImageElement plusIcon;
	DimOverlay restDim;
	CST_Color restColor = { 0x78, 0x78, 0x78, 0xFF }; // gris tenue
	TextElement restPercent;
	TextElement restSpeed;
	std::string restPercentStr = "0%";  // texto vigente (lo actualiza el callback de progreso)
	std::string restSpeedStr = " ";
	std::string restPercentShown;       // el que ya esta dibujado (para no regenerar texturas)
	std::string restSpeedShown;
	bool restAllowed = false;           // solo en descargas/instalaciones, no al borrar
	void showPlusHint();
	void renderRestFrame();
};

#endif
