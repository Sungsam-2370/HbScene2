#pragma once

#include <string>
#include <functional>
#include "Texture.hpp"
#include "DownloadQueue.hpp"

class NetImageElement : public Texture
{
public:
	/// Creates a new image element, downloading the image from url
	/// If the url is not cached, getImageFallback will be called to
	/// get a Texture to be shown while downloading the correct image;
	/// the provided Texture is free'd when the download is complete
	/// or the destructor is called
	/// If immediateLoad is set to false, the loading won't begin until
	/// load() is called
	/// diskCachePath (opcional): si se pasa, ANTES de intentar bajar la
	/// imagen por red se busca este archivo en disco -- si existe, se
	/// carga directo de ahi y no se hace ningun pedido de red. Si no
	/// existe todavia, se baja normalmente por red y, una vez lista, se
	/// guarda en este path (como PNG) para que la proxima vez ya este
	/// cacheada. Pensado para listas grandes de iconos que no cambian
	/// seguido (ver AppCard), asi en el siguiente inicio de la app no
	/// hace falta re-descargar cada icono.
	NetImageElement(const char *url, std::function<Texture *(void)> getImageFallback = NULL, bool immediateLoad = true, const std::string& diskCachePath = std::string());
	~NetImageElement();

	/// Start downloading the image (called in the constructor unless immediateLoad is false)
	void fetch();

	/// Render the image
	void render(Element* parent);
	bool loaded = false;
	bool updateSizeAfterLoad = false;

private:
	void imgDownloadComplete(DownloadOperation *download);

	DownloadOperation *imgDownload = nullptr;
	Texture *imgFallback = nullptr;
	bool downloadStarted = false;
	std::string diskCachePath;
};
