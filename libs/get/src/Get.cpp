#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_set>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "Get.hpp"
#include "./repos/GetRepo.hpp"
#include "./repos/LocalRepo.hpp"
#include "Utils.hpp"

#ifdef SWITCH
#include <switch.h>
#include "nspinstall/NspAutoInstall.hpp"
#endif

using namespace rapidjson;

bool debug = false;

// ---------------------------------------------------------------------------
// Evita que la consola entre en modo de espera/hibernacion mientras dura
// una descarga o instalacion. Se desactiva el auto-sleep al crear el objeto
// y se reactiva automaticamente al destruirse (sin importar por que salida
// de la funcion ocurra: exito, error temprano, o excepcion).
// ---------------------------------------------------------------------------
struct NoSleepGuard
{
	NoSleepGuard()
	{
#ifdef SWITCH
		appletSetAutoSleepDisabled(true);
#endif
	}
	~NoSleepGuard()
	{
#ifdef SWITCH
		appletSetAutoSleepDisabled(false);
#endif
	}
};

Get::Get(
	std::string_view config_dir,
	std::string_view defaultRepo,
	bool performInitialLoad,
	std::string defaultRepoType
)
	: mDefaultRepo(defaultRepo), mDefaultRepoType(defaultRepoType)
{

	// the path for the get metadata folder
	std::string config_path = std::string(config_dir);

	mPkg_path = std::string(config_path + "packages/");
	mTmp_path = std::string(config_path + "tmp/");

	my_mkdir(config_dir.data());
	my_mkdir(mPkg_path.c_str());
	my_mkdir(mTmp_path.c_str());

	this->loadRepos();

	if (performInitialLoad) {
		// load repo info
		this->update();
	}
}

int Get::install(Package& package, bool resume)
{
	// Evitar que la consola entre en reposo/hibernacion mientras dura
	// toda la descarga e instalacion (se reactiva automaticamente al
	// salir de esta funcion, sin importar el camino de salida)
	NoSleepGuard noSleepGuard;

	// calcular cuantos zips existen en total para este paquete (1 a 4)
	int zipTotal = 1;
	if (!package.zipUrl2.empty()) zipTotal++;
	if (!package.zipUrl3.empty()) zipTotal++;
	if (!package.zipUrl4.empty()) zipTotal++;

	// found package in a remote server, fetch it
	bool located = package.downloadZip(mTmp_path, nullptr, resume, 1, zipTotal);

	if (!located)
	{
		// according to the repo list, the package zip file should've been here
		// but we got a 404 and couldn't find it
		printf("--> Error retrieving remote file for [%s] (check network or 404 error?)\n", package.getPackageName().c_str());
		return false;
	}

	// install the package, (extracts manifest, etc)
	package.install(mPkg_path, mTmp_path, 1, zipTotal);

	// if a second zip exists, download and install it too
	if (!package.zipUrl2.empty())
	{
		bool located2 = package.downloadZip2(mTmp_path, nullptr, resume, 2, zipTotal);
		if (!located2)
			printf("--> Error retrieving second zip for [%s]\n", package.getPackageName().c_str());
		else
			package.install2(mPkg_path, mTmp_path, 2, zipTotal);
	}

	// if a third zip exists, download and install it too
	if (!package.zipUrl3.empty())
	{
		bool located3 = package.downloadZip3(mTmp_path, nullptr, resume, 3, zipTotal);
		if (!located3)
			printf("--> Error retrieving third zip for [%s]\n", package.getPackageName().c_str());
		else
			package.install3(mPkg_path, mTmp_path, 3, zipTotal);
	}

	// if a fourth zip exists, download and install it too
	if (!package.zipUrl4.empty())
	{
		bool located4 = package.downloadZip4(mTmp_path, nullptr, resume, 4, zipTotal);
		if (!located4)
			printf("--> Error retrieving fourth zip for [%s]\n", package.getPackageName().c_str());
		else
			package.install4(mPkg_path, mTmp_path, 4, zipTotal);
	}

	printf("--> Downloaded [%s] to sdroot/\n", package.getPackageName().c_str());

	package.runtime_install_status.clear();

#ifdef SWITCH
	// Paso 3 del flujo pedido: si (y solo si) el repo.json trae la clave
	// "instalacion", ya se descargaron y extrajeron todos los zips del
	// paquete (incluyendo multi-zip), asi que el .nsp referenciado ya
	// deberia existir en el SD. Intentamos instalarlo ahora.
	if (!package.getInstallNsp().empty())
	{
		auto nspResult = nspinstall::InstallNspIfRequested(ROOT_PATH, package.getInstallNsp(), false);
		if (!nspResult.nothing_to_do)
		{
			printf("--> Auto-instalacion de NSP [%s]: %s\n",
			       package.getInstallNsp().c_str(), nspResult.success ? "OK" : "FALLO");
			package.runtime_install_status = nspResult.message;
		}
	}
#endif

	// clear any progress callbacks before updating repo metadata
	extern libget_progress_callback_t networking_callback;
	extern void* networking_callback_data;
	networking_callback = nullptr;
	networking_callback_data = nullptr;

	// update again post-install
	update();
	return true;
}

int Get::remove(Package& package)
{
	package.remove(mPkg_path);
	printf("--> Uninstalled [%s] package\n", package.getPackageName().c_str());
	update();

	return true;
}

int Get::toggleRepo(Repo& repo)
{
	repo.setEnabled(!repo.isEnabled());
	update();
	return true;
}

void Get::addLocalRepo()
{
	repos.push_back(std::make_unique<LocalRepo>(mPkg_path));
	update();
}

void Get::addAndRemoveReposByURL(
	const std::unordered_map<std::string, std::string>& reposToAdd,
	const std::unordered_set<std::string>& reposToRemove
)
{
	size_t reposLen = repos.size();

	bool madeChanges = false;

	repos.erase(std::remove_if(repos.begin(), repos.end(), 
		[reposToRemove](auto curRepo) {
			std::string curUrl = curRepo->getUrl();
			return reposToRemove.find(curUrl) != reposToRemove.end();
		}), repos.end()
	);

	madeChanges = reposLen != repos.size();

	std::unordered_set<std::string> currentUrls;
	for (auto& curRepo : repos) {
		currentUrls.insert(curRepo->getUrl());
	}
	
	for (auto& entry : reposToAdd) {
		auto url = entry.first;
		auto curType = entry.second;
		if (currentUrls.find(url) == currentUrls.end()) {
			// extract domain from url string
			std::string nameSummary;
			size_t start = url.find("//");

			if (start != std::string::npos) {
				nameSummary = url.substr(start + 2);
				size_t end = nameSummary.find("/");
				if (end != std::string::npos) {
					nameSummary = nameSummary.substr(0, end);
				}
			}

			// if nameSummary is still empty, provide a fallback
			if (nameSummary.empty()) {
				nameSummary = "Auto-added from Meta";
			}
			auto newRepo = Repo::createRepo(nameSummary, url, true, curType, "");
			repos.push_back(std::shared_ptr<Repo>(std::move(newRepo)));
		}
	}

	madeChanges = madeChanges || reposLen != repos.size();

	// save the repos to disk, if we've made any changes
	if (madeChanges) {
		saveRepos();
		loadRepos();
	}
}

// Repo persistence is disabled — the default repo URL is hardcoded in the binary.
void Get::saveRepos() {
	// intentionally empty: repos are not written to disk
}

/**
Load any repos from a config file into the repos vector.
**/
void Get::loadRepos()
{
	repos.clear();

	// Repo URL is hardcoded in the binary — no repos.json is read or written.
#if defined(WII) || defined(_3DS) || defined(WII_MOCK)
	auto defaultRepo = GetRepo::createRepo("Default Repo", this->mDefaultRepo, true, this->mDefaultRepoType, mPkg_path);
#else
	auto defaultRepo = std::make_unique<GetRepo>("Default Repo", this->mDefaultRepo, true);
#endif

	repos.push_back(std::move(defaultRepo));
	printf("--> Loaded default repo from binary (no repos.json)\n");
}

void Get::update()
{
	printf("--> Updating package list\n");
	
	// clear current packages
	packages.clear();

	// fetch recent package list from enabled repos
	int i = 0;
	for (const auto& repo : repos)
	{
		printf("--> Checking repo %s\n", repo->getName().c_str());
		if (repo->isLoaded() && repo->isEnabled())
		{
			printf("--> Repo %s is loaded and enabled\n", repo->getName().c_str());
			if (libget_status_callback != nullptr)
			{
				libget_status_callback(STATUS_RELOADING, i + 1, (int32_t)repos.size());
			}

			for (auto& element : repo->loadPackages())
			{
				element->mRepo = repo;
				packages.push_back(std::move(element));
			}
		}
		i++;
	}

	if (libget_status_callback != nullptr)
	{
		libget_status_callback(STATUS_UPDATING_STATUS, 1, 1);
	}

	// remove duplicates, prioritizing later packages over earlier ones
	this->removeDuplicates();

	// check for any installed packages to update their status
	for (const auto& package : packages) {
		package->updateStatus(mPkg_path);
	}
	
	printf("--> Get::update() complete, final package count: %zu\n", packages.size());

	// sort the packages by name
	// TODO: apply other sort orders here, and potentially search filters
	// std::sort(packages.begin(), packages.end(), [](const std::shared_ptr<Package>& a, const std::shared_ptr<Package>& b) {
	// 	return a->getPackageName() < b->getPackageName();
	// });
}

int Get::validateRepos() const
{
	if (repos.empty())
	{
		printf("--> There are no repos configured!\n");
		return ERR_NO_REPOS;
	}

	return 0;
}

std::vector<Package> Get::list()
{
	// packages is a vector of shared_ptrs, so we need to dereference them
	std::vector<Package> ret;
	for (auto& cur : packages) {
		if (cur != nullptr)
			ret.emplace_back(*cur);
	}
	return ret;
}

std::vector<Package> Get::search(const std::string& query)
{
	// TODO: replace with inverted index for speed
	// https://vgmoose.com/blog/implementing-a-static-blog-search-clientside-in-js-6629164446/

	std::vector<Package> results;
	std::string lower_query = toLower(query);

	for (auto& cur : packages)
	{
		if (cur != nullptr && (toLower(cur->getTitle()).find(lower_query) != std::string::npos || toLower(cur->getAuthor()).find(lower_query) != std::string::npos || toLower(cur->getShortDescription()).find(lower_query) != std::string::npos || toLower(cur->getLongDescription()).find(lower_query) != std::string::npos))
		{
			// matches, add to return vector, and continue
			results.emplace_back(*cur); // add copy to result;
			continue;
		}
	}

	return results;
}

std::optional<Package> Get::lookup(const std::string& pkg_name)
{
	for (auto& cur : packages)
	{
		if (cur && cur->getPackageName() == pkg_name)
		{
			// return copy!
			return *cur;
		}
	}
	return std::nullopt;
}

void Get::removeDuplicates()
{
	std::unordered_set<std::string> packageSet;
	std::unordered_set<std::shared_ptr<Package>> removalSet;

	// going backards, fill out our sets
	// (prioritizes later repo packages over earlier ones, regardless of versioning)
	// TODO: semantic versioning or have a versionCode int that increments every update
	for (int32_t x = (int32_t)packages.size() - 1; x >= 0; x--)
	{
		auto& name = packages[x]->getPackageName();
		if (packageSet.find(name) == packageSet.end())
			packageSet.insert(name);
		else
			removalSet.insert(packages[x]);
	}

	// remove them, if they are in the removal set
	packages.erase(std::remove_if(packages.begin(), packages.end(), [removalSet](auto& p)
					   { return removalSet.find(p) != removalSet.end(); }),
		packages.end());
}

void info(const char* format, ...)
{
	if (!debug) return;
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);
	va_end(args);
}
