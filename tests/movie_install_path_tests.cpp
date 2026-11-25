#include "movie_install_path.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>

namespace {
using namespace unbound;
void globalMovies() {
    for (const auto& [selected, native] : {
            std::pair{"english", "english"}, {"french", "french"}, {"german", "german"},
            {"spanish", "spanish"}, {"italian", "italian"}, {"japanese", "fulljap"},
            {"fulljap", "fulljap"}, {"English", "english"}, {"JAPANESE", "fulljap"}}) {
        const auto route = routeMovieInstallPath("update/content/video/zm_highrise_load.bik", selected);
        assert(route.status == MovieInstallPathStatus::Movie && route.error.empty());
        assert(route.logical == "update/content/video/zm_highrise_load.bik");
        assert(route.movieDirectory == std::string("update/content/") + native + "/video");
        assert(route.destination == route.movieDirectory + "/zm_highrise_load.bik");
        const auto directory = routeMovieInstallPath("update/content/video", selected);
        assert(directory.status == MovieInstallPathStatus::Movie);
        assert(directory.destination == route.movieDirectory && directory.movieDirectory == route.movieDirectory);
        const auto nested = routeMovieInstallPath("update/content/video/intro/chapter/Intro_01.BIK", selected);
        assert(nested.status == MovieInstallPathStatus::Movie);
        assert(nested.destination == route.movieDirectory + "/intro/chapter/Intro_01.BIK");
        assert(nested.movieDirectory == route.movieDirectory);
    }
    // Routing has no title/device/region dependency: USA/EUR/JPN use their
    // already selected native language and the same universal ZIP entry.
    assert(routeMovieInstallPath("update/content/video/a.bik", "english").destination ==
           "update/content/english/video/a.bik");
    assert(routeMovieInstallPath("update/content/video/a.bik", "fulljap").destination ==
           "update/content/fulljap/video/a.bik");
}
void explicitMovies() {
    for (const auto& [language, native] : {
            std::pair{"english", "english"}, {"french", "french"}, {"german", "german"},
            {"spanish", "spanish"}, {"italian", "italian"}, {"fulljap", "fulljap"},
            {"japanese", "fulljap"}}) {
        const std::string logical = std::string("update/content/") + language + "/video/a.bik";
        const auto route = routeMovieInstallPath(logical, "english");
        assert(route.status == MovieInstallPathStatus::Movie && route.logical == logical);
        assert(route.destination == std::string("update/content/") + native + "/video/a.bik");
        const auto directory = routeMovieInstallPath(std::string("update/content/") + language + "/video", "japanese");
        assert(directory.status == MovieInstallPathStatus::Movie);
        assert(directory.destination == std::string("update/content/") + native + "/video");
    }
}
void untouchedAndUnsafePaths() {
    for (const std::string path : {
            "update", "update/code", "update/content", "update/code/t6mp_cafef_rpl.rpl",
            "update/content/@language/map.ff", "update/content/english/map.ff", "update/content/japanese/map.ff",
            "update/content/fulljap/map.ff", "update/content/english/sound/loaded/a.sabl",
            "update/content/video2/a.bik", "update/content/english/video2/a.bik",
            "update/content/unbound_video/a.bik", "update/content/unbound/video/a.bik"}) {
        const auto route = routeMovieInstallPath(path, "invalid/language");
        assert(route.status == MovieInstallPathStatus::Unchanged && route.destination == path);
        assert(route.logical == path && route.movieDirectory.empty() && route.error.empty());
    }
    for (const std::string path : {
            "", "/update/content/video/a.bik", "update/content/video/", "update/content/video//a.bik",
            "update/content/video/../a.bik", "update/content/video/./a.bik", "update/content/video/a:bik",
            "update\\content\\video\\a.bik", "update/content/video/a.unbound-new",
            "update/content/video/a.unbound-old", "update/content/video/a.unbound-replacing",
            "update/content/video.unbound-link-original/a.bik", "update/content/video/a.UNBOUND-NEW",
            "update/content/VIDEO/a.bik", "update/content/English/video/a.bik",
            "UPDATE/content/video/a.bik", "update/Content/french/video/a.bik"}) {
        const auto route = routeMovieInstallPath(path, "english");
        assert(route.status == MovieInstallPathStatus::Invalid);
        assert(!route.error.empty() && route.destination.empty());
    }
    for (const std::string selected : {"", "../english", "english/video", "korean", "english:", "english\\"}) {
        const auto global = routeMovieInstallPath("update/content/video/a.bik", selected);
        assert(global.status == MovieInstallPathStatus::Invalid && !global.error.empty());
        const auto localized = routeMovieInstallPath("update/content/french/video/a.bik", selected);
        assert(localized.status == MovieInstallPathStatus::Invalid && !localized.error.empty());
    }
    assert(routeMovieInstallPath(std::string("update/content/video/") + std::string(256, 'a'), "english").status ==
           MovieInstallPathStatus::Invalid);
    const std::string oversized = "update/content/video/" + std::string(240, 'a') + "/" +
                                 std::string(240, 'b') + "/" + std::string(135, 'c');
    assert(oversized.size() <= 639 && oversized.size() + std::string("/english").size() > 639);
    assert(routeMovieInstallPath(oversized, "english").status == MovieInstallPathStatus::Invalid);
    const auto control = routeMovieInstallPath("update/content/video/a\nb.bik", "english");
    assert(control.status == MovieInstallPathStatus::Invalid);
}
} // namespace

int main() {
    globalMovies(); explicitMovies(); untouchedAndUnsafePaths();
    assert(movieInstallTargetFits("storage_usb01:/usr/title/0005000e/1010cf00/content/english/video/movie.bik", false));
    assert(movieInstallTargetFits("content/" + std::string(237, 'a'), false));
    assert(!movieInstallTargetFits("content/" + std::string(238, 'a'), false));
    assert(movieInstallTargetFits("content/" + std::string(255, 'a'), true));
    const std::string longParent = std::string(240, 'a') + "/" + std::string(240, 'b') + "/";
    assert(movieInstallTargetFits(longParent + std::string(139, 'c'), false));
    assert(!movieInstallTargetFits(longParent + std::string(140, 'c'), false));
    std::cout << "Movie install paths: native localized lookup, universal languages, Japanese alias, unchanged code/FF paths, strict scope, names and limits passed\n";
}
