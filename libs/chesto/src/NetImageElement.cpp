#include "NetImageElement.hpp"
#include <sys/stat.h>

// existe con ese nombre exacto y es un archivo (no probamos mas que eso,
// si esta corrupto el IMG_Load de mas abajo va a fallar y seguimos con
// la descarga por red como si no hubiera cache)
static bool fileExistsOnDisk(const std::string& path)
{
	struct stat st;
	return !path.empty() && stat(path.c_str(), &st) == 0;
}

NetImageElement::NetImageElement(const char *url, std::function<Texture *(void)> getImageFallback, bool immediateLoad, const std::string& diskCachePath)
	: diskCachePath(diskCachePath)
{
	std::string key = std::string(url);
	// printf("Key: %s\n", key.c_str());
	if (loadFromCache(key)) {
		loaded = true;

		// if we're using the cache, we can update the size now
		// printf("The size of the image is %d x %d\n", texW, texH);
		width = texW;
		height = texH;
	}
	else if (fileExistsOnDisk(diskCachePath) && [&]{
		// intento cargar directo del cache en disco, sin tocar la red.
		// Si el archivo esta corrupto/no es una imagen valida, seguimos
		// de largo y caemos a la descarga por red como si no existiera.
		CST_Surface *surface = IMG_Load(diskCachePath.c_str());
		bool ok = loadFromSurfaceSaveToCache(key, surface);
		if (surface) CST_FreeSurface(surface);
		return ok;
	}())
	{
		loaded = true;
		width = texW;
		height = texH;
	}
	else {
		// setup a temporary image fallback
		if (getImageFallback)
			imgFallback = getImageFallback();

		// start downloading the correct image
		imgDownload = new DownloadOperation();
		imgDownload->url = std::string(url);
		imgDownload->cb = std::bind(&NetImageElement::imgDownloadComplete, this, std::placeholders::_1);

		// load immediately
		if (immediateLoad)
			fetch();
	}
}

NetImageElement::~NetImageElement()
{
	if (imgFallback)
		delete imgFallback;

	if (imgDownload)
	{
		DownloadQueue::downloadQueue->downloadCancel(imgDownload);
		delete imgDownload;
	}
}

void NetImageElement::fetch()
{
	if (!downloadStarted && imgDownload) {
		DownloadQueue::downloadQueue->downloadAdd(imgDownload);
		downloadStarted = true;
	}
}

void NetImageElement::imgDownloadComplete(DownloadOperation *download)
{
	bool success = false;

	if (download->status == DownloadStatus::COMPLETE)
	{
		CST_Surface *surface = IMG_Load_RW(SDL_RWFromMem((void*)download->buffer.c_str(), download->buffer.size()), 1);
		success = loadFromSurfaceSaveToCache(download->url, surface);
		CST_FreeSurface(surface);
	}

	if (success)
	{
		this->needsRedraw = true;
		loaded = true;

		delete imgFallback;
		imgFallback = nullptr;

		if (updateSizeAfterLoad) {
			width = texW;
			height = texH;
		}

		// guardamos una copia en disco (PNG, no JPG -- IMG_SaveJPG dio
		// problemas reales en este proyecto, ver Texture::saveTo) para
		// que la proxima vez que se muestre esta imagen no haga falta
		// pedirla de nuevo por red. Si el directorio del cache no existe
		// todavia esto simplemente falla en silencio (no es critico,
		// solo se pierde el cacheo de esta imagen puntual).
		//
		// maxDim=256: el icono nunca se muestra mas grande que eso (ver
		// AppCard, icon.resize(256, ICON_SIZE)), asi que guardar la
		// resolucion original completa (a veces bastante mas grande)
		// solo desperdicia espacio en la SD sin ganancia visual.
		if (!diskCachePath.empty())
			this->saveTo(diskCachePath, 256);
	}

	delete imgDownload;
	imgDownload = nullptr;
}


void NetImageElement::render(Element* parent)
{
	// if we're hidden, don't render
	if (hidden) return;

	if (mTexture)
	{
		Texture::render(parent);
	}
	else if (imgFallback)
	{
		imgFallback->x = x;
		imgFallback->y = y;
		imgFallback->width = width;
		imgFallback->height = height;
		imgFallback->render(parent);
	}
}
