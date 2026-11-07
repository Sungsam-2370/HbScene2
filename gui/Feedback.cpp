#include "Feedback.hpp"
#include "MainDisplay.hpp"
#include "main.hpp"
#include "SystemKeyboard.hpp"

#include "../libs/chesto/src/RootDisplay.hpp"

#ifndef NETWORK_MOCK
#include <curl/curl.h>
#include <curl/easy.h>
#endif

Feedback::Feedback(Package& package)
	: package(&package)
	, title((std::string(i18n("feedback.leaving") + " \"") + package.getTitle() + "\""), 25)
	, icon(package.getIconUrl().c_str(), []{ return new ImageElement(RAMFS "res/default.png"); })
	, quit(i18n("feedback.discard"), Y_BUTTON, false, 20)
	, send(i18n("feedback.submit"), X_BUTTON, false, 20)
	, writeBtn("Escribir mensaje", A_BUTTON, false, 20)
	, response(i18n("feedback.help"), 20, NULL, false, 460)
{
	title.position(50, 30);
	super::append(&title);

	icon.setScaleMode(SCALE_PROPORTIONAL_WITH_BG);
	icon.position(50, 100);
#if defined(_3DS) || defined(_3DS_MOCK)
  icon.resize(ICON_SIZE, ICON_SIZE);
#else
	icon.resize(256/SCALER, ICON_SIZE/SCALER);
#endif
	super::append(&icon);

	quit.position(895, 240);
	quit.action = std::bind(&Feedback::back, this);
	super::append(&quit);

	send.position(quit.x + quit.width + 15, quit.y);
	send.action = std::bind(&Feedback::submit, this);
	super::append(&send);

	// escribir el mensaje con el teclado del sistema (A)
	writeBtn.position(quit.x - 15 - writeBtn.width, send.y);
	writeBtn.action = std::bind(&Feedback::edit, this);
	super::append(&writeBtn);

	response.position(860, 20);
	super::append(&response);

	feedback.setSize(23);
	feedback.setWrappedWidth(730);
	feedback.position(390, 100);
	super::append(&feedback);
}

void Feedback::render(Element* parent)
{
	// draw a white background, 870 wiz
	CST_Color white = { 0xff, 0xff, 0xff, 0xff };

	  if (parent != NULL) {
    CST_SetDrawColor(RootDisplay::renderer, white);
  }

	return super::render(parent);
}

bool Feedback::process(InputEvents* event)
{
	bool ret = super::process(event);

	if (needsRefresh)
	{
		feedback.setText(message.empty() ? std::string(" ") : message);
		feedback.update();
		needsRefresh = false;
	}

	return ret;
}

void Feedback::edit()
{
	std::string typed;
	if (SystemKeyboard::ask("Mensaje", "Escribe tu mensaje", message, 500, typed, false, true))
	{
		message = typed;
		needsRefresh = true;
	}
}

void Feedback::submit()
{
#ifndef NETWORK_MOCK
	CURL* curl;
	CURLcode res;

#if defined(__WIIU__)
	const char* userKey = "wiiu_user";
#elif defined(WII)
	const char* userKey = "wii_user";
#else
	const char* userKey = "switch_user";
#endif

	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, "http://switchbru.com/appstore/feedback");
		// el mensaje puede traer acentos, & o saltos de linea: se codifica para el POST
		char* escaped = curl_easy_escape(curl, message.c_str(), (int)message.size());
		std::string fields = std::string("name=") + userKey + "&package=" + package->getPackageName() + "&message=" + (escaped ? escaped : "");
		if (escaped)
			curl_free(escaped);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fields.c_str());

		res = curl_easy_perform(curl);
		(void)res;

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
#endif

	// close this window
	this->back();
}

void Feedback::back()
{
	RootDisplay::switchSubscreen(nullptr);
}
