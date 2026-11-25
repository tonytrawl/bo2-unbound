#include "install_discovery.hpp"
#include "game_launch.hpp"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>

using namespace unbound;

namespace {
std::string metadata(std::uint64_t titleId, const std::string& type = "hexBinary",
                     const std::string& version = "0080") {
    char id[17]{};
    std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(titleId));
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<app type=\"complex\" access=\"777\">\n"
        "<version type=\"unsignedInt\" length=\"4\">15</version>\n"
        "<title_id type=\"hexBinary\" length=\"8\">" + std::string(id) + "</title_id>\n"
        "<title_version type=\"" + type + "\" length=\"2\">" + version + "</title_version>\n"
        "<group_id type=\"hexBinary\" length=\"4\">000010CF</group_id>\n</app>\n";
}

struct FakeIo {
    std::map<std::string, InstallDiscoveryPath> states;
    std::map<std::string, std::string> text;
    std::map<std::string, std::vector<std::string>> directories;
    std::set<std::string> unreadableFiles;
    unsigned reads = 0;
    InstallDiscoveryPath pathState(const std::string& path) {
        const auto found = states.find(path);
        return found == states.end() ? InstallDiscoveryPath::Missing : found->second;
    }
    bool readText(const std::string& path, std::string& output, std::size_t maximum) {
        ++reads;
        const auto found = text.find(path);
        if (unreadableFiles.count(path) || found == text.end() || found->second.size() > maximum)
            return false;
        output = found->second;
        return true;
    }
    bool listDirectory(const std::string& path, std::vector<std::string>& names,
                       std::size_t maximum, std::string& error) {
        const auto found = directories.find(path);
        if (found == directories.end() || found->second.size() > maximum) {
            error = "Injected unreadable/oversized directory";
            return false;
        }
        names = found->second;
        return true;
    }
    std::string addTitle(Device device, std::uint64_t titleId,
                         const std::string& xml, const std::vector<std::string>& languages) {
        const std::string root = stockTitleRoot(device, titleId);
        assert(!root.empty());
        states[root] = InstallDiscoveryPath::Directory;
        states[root + "/code"] = InstallDiscoveryPath::Directory;
        states[root + "/content"] = InstallDiscoveryPath::Directory;
        states[root + "/code/app.xml"] = InstallDiscoveryPath::File;
        text[root + "/code/app.xml"] = xml;
        directories[root + "/content"] = languages;
        for (const auto& name : languages) states[root + "/content/" + name] = InstallDiscoveryPath::Directory;
        return root;
    }
};

void metadataTests() {
    const auto id = SupportedSkus[0].updateTitleId;
    std::uint16_t version = 999;
    assert(stockTitleMetadata(metadata(id), id, version) && version == 128);
    assert(stockTitleMetadata(metadata(id, "unsignedInt", "128"), id, version) && version == 128);
    const auto japanId = SupportedSkus[4].updateTitleId;
    assert(stockTitleMetadata(metadata(japanId, "hexBinary", "0060"), japanId, version) && version == 96);
    assert(!stockTitleMetadata(metadata(id), japanId, version) && version == 0);
    assert(stockTitleMetadata(metadata(id, "hexBinary", "invalid"), id, version) && version == 0);
    assert(stockTitleMetadata(metadata(id, "hexBinary", "10000"), id, version) && version == 0);
    assert(stockTitleMetadata(metadata(id, "bogus", "128"), id, version) && version == 0);
    const std::string noVersion = "<app><title_id type='hexBinary'>0005000e1010cf00</title_id></app>";
    assert(stockTitleMetadata(noVersion, id, version) && version == 0);
    assert(stockTitleMetadata("\xef\xbb\xbf" + metadata(id), id, version) && version == 128);
    assert(stockTitleMetadata("<!-- <title_id>wrong</title_id> -->" + metadata(id), id, version));
    const std::string duplicateId = "<app><title_id type='hexBinary'>0005000e1010cf00</title_id>"
        "<title_id type='hexBinary'>0005000e1010cf00</title_id></app>";
    assert(!stockTitleMetadata(duplicateId, id, version));
    const std::string duplicateVersion = noVersion.substr(0, noVersion.size() - 6) +
        "<title_version type='hexBinary'>0080</title_version>"
        "<title_version type='hexBinary'>0080</title_version></app>";
    assert(!stockTitleMetadata(duplicateVersion, id, version));
    assert(!stockTitleMetadata("<meta><title_id type='hexBinary'>0005000e1010cf00</title_id></meta>", id, version));
    assert(!stockTitleMetadata("<app><nested><title_id type='hexBinary'>0005000e1010cf00</title_id></nested></app>", id, version));
    assert(!stockTitleMetadata("<app><!-- <title_id type='hexBinary'>0005000e1010cf00</title_id> --></app>", id, version));
    assert(!stockTitleMetadata("<!DOCTYPE app [<!ENTITY x '0005000e1010cf00'>]><app><title_id type='hexBinary'>&x;</title_id></app>", id, version));
    assert(!stockTitleMetadata("<app><title_id type='hexBinary' type='hexBinary'>0005000e1010cf00</title_id></app>", id, version));
    assert(!stockTitleMetadata("<app><title_id type='hexBinary'>0005000e1010cf00</title_id></app><app/>", id, version));
    assert(!stockTitleMetadata("<app><title_id type='hexBinary'>0005000e1010cf00</title_id>", id, version));
    assert(!stockTitleMetadata(std::string(65537, 'x'), id, version));
}

void titleProbeTests() {
    const auto id = SupportedSkus[0].updateTitleId;
    FakeIo io;
    const std::string mlc = io.addTitle(Device::MLC, id, metadata(id, "hexBinary", "0010"), {"english"});
    const std::string usb = io.addTitle(Device::USB, id, metadata(id), {"english", "french"});
    InstallLocation location;
    std::string error;
    assert(discoverStockTitle(Device::MLC, id, location, error, io) == TitleProbe::Valid);
    assert(location.root == mlc && location.device == Device::MLC && location.titleVersion == 16);
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Valid);
    assert(location.root == usb && location.device == Device::USB && location.titleVersion == 128);
    assert(discoverStockTitle(Device::USB, SupportedSkus[1].updateTitleId, location, error, io) == TitleProbe::Missing);
    assert(!location.present && error.empty());
    assert(discoverStockTitle(Device::Disc, id, location, error, io) == TitleProbe::Invalid);
    assert(discoverStockTitle(Device::USB, SupportedSkus[0].aocTitleId, location, error, io) == TitleProbe::Invalid);
    assert(stockTitleRoot(Device::USB, 0x0005000e12345678ULL).empty());

    io.states[usb] = InstallDiscoveryPath::Unreadable;
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid && !location.present);
    io.states[usb] = InstallDiscoveryPath::Directory;
    io.states[usb + "/code"] = InstallDiscoveryPath::Missing;
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid);
    io.states[usb + "/code"] = InstallDiscoveryPath::Directory;
    io.states[usb + "/content"] = InstallDiscoveryPath::File;
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid);
    io.states[usb + "/content"] = InstallDiscoveryPath::Directory;
    io.unreadableFiles.insert(usb + "/code/app.xml");
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid);
    io.unreadableFiles.clear();
    io.text[usb + "/code/app.xml"] = metadata(SupportedSkus[0].baseTitleId);
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid);
    io.text[usb + "/code/app.xml"] = std::string(65537, 'x');
    assert(discoverStockTitle(Device::USB, id, location, error, io) == TitleProbe::Invalid);
}

void languageTests() {
    FakeIo io;
    const auto id = SupportedSkus[0].updateTitleId;
    const auto root = io.addTitle(Device::USB, id, metadata(id),
        {"French", "English", "Spanish", "german", "italian", "japanese", "fulljap",
         "mods", "sound", "video", "../english", "english/../../code"});
    InstallLocation location{Device::USB, root, 128, true};
    std::vector<std::string> languages;
    std::string error;
    const InstallLocation unsupported{Device::USB, "storage_usb01:/usr/title/arbitrary", 128, true};
    assert(!discoverInstallLanguages(unsupported, languages, error, io));
    assert(discoverInstallLanguages(location, languages, error, io));
    assert(languages == std::vector<std::string>({"English", "French", "fulljap", "german", "italian", "japanese", "Spanish"}));
    io.states[root + "/content/fulljap"] = InstallDiscoveryPath::File;
    assert(discoverInstallLanguages(location, languages, error, io) && languages.size() == 6);
    io.states[root + "/content/English"] = InstallDiscoveryPath::Unreadable;
    assert(!discoverInstallLanguages(location, languages, error, io));
    io.states[root + "/content/English"] = InstallDiscoveryPath::Directory;
    io.directories[root + "/content"].push_back("english");
    io.states[root + "/content/english"] = InstallDiscoveryPath::Directory;
    assert(!discoverInstallLanguages(location, languages, error, io));
    io.directories[root + "/content"] = {"mods", "sound", "video"};
    assert(discoverInstallLanguages(location, languages, error, io) && languages.empty());
    io.directories[root + "/content"] = std::vector<std::string>(4097, "mods");
    assert(!discoverInstallLanguages(location, languages, error, io));
    io.directories.erase(root + "/content");
    assert(!discoverInstallLanguages(location, languages, error, io));
}
void ghostsTitleTests() {
    const auto skus=gameSkus(Game::Ghosts);
    const std::string ids[]={"10146800","10156100","10157600","10157900"};
    assert(skus.size()==std::size(ids));
    for (std::size_t i=0;i<skus.size();++i) {
        const auto& sku=skus[i];
        assert(findSkuByTitle(sku.baseTitleId)==&sku);
        assert(findSkuByTitle(sku.updateTitleId)==&sku);
        assert(findSkuByTitle(sku.aocTitleId)==&sku);
        assert(requiredOfficialUpdateVersion(&sku)==112);
        for (const auto device : {Device::MLC, Device::USB}) {
            const std::string root=device==Device::USB?"storage_usb01:":"storage_mlc01:";
            assert(stockTitleRoot(device,sku.baseTitleId)==root+"/usr/title/00050000/"+ids[i]);
            FakeIo io;
            const auto path=io.addTitle(device,sku.updateTitleId,
                metadata(sku.updateTitleId,"hexBinary","0070"),{"english"});
            assert(path==root+"/usr/title/0005000e/"+ids[i]);
            InstallLocation location;
            std::string error;
            assert(discoverStockTitle(device,sku.updateTitleId,location,error,io)==TitleProbe::Valid);
            assert(location.titleVersion==112);
            std::vector<std::string> languages;
            assert(discoverInstallLanguages(location,languages,error,io)&&languages.size()==1);
            io.text[path+"/code/app.xml"]=metadata(SupportedSkus[0].updateTitleId);
            assert(discoverStockTitle(device,sku.updateTitleId,location,error,io)==TitleProbe::Invalid);
        }
    }
    assert(languageFolderForSku(&skus[1],"de-DE")=="german");
    assert(languageFolderForSku(&skus[1],"fr-FR")=="french");
    assert(languageFolderForSku(&skus[2],"it-IT")=="italian");
    assert(languageFolderForSku(&skus[3],"en-US")=="japanese");
}
void gameLaunchTests() {
    using LaunchSource = std::pair<std::uint64_t,Device>;
    const auto& ghostUs=GhostsSkus[0];
    const auto& ghostEu=GhostsSkus[1];
    const auto& bo2=SupportedSkus[0];
    std::set<LaunchSource> available{
        {ghostUs.baseTitleId,Device::USB},
        {ghostUs.baseTitleId,Device::MLC},
        {ghostEu.baseTitleId,Device::Disc},
        {bo2.baseTitleId,Device::Disc},
    };
    const auto lookup=[&](const GameSku& sku,Device device) -> GameLaunchTarget {
        if (!available.contains({sku.baseTitleId,device})) return {};
        return {sku.baseTitleId,device,"validated path"};
    };
    assert(chooseGameLaunch(Game::Ghosts,&ghostUs,lookup).titleId==ghostEu.baseTitleId);
    available.erase({ghostEu.baseTitleId,Device::Disc});
    assert(chooseGameLaunch(Game::Ghosts,&ghostUs,lookup).device==Device::USB);
    available.erase({ghostUs.baseTitleId,Device::USB});
    assert(chooseGameLaunch(Game::Ghosts,&ghostUs,lookup).device==Device::MLC);
    available.erase({ghostUs.baseTitleId,Device::MLC});
    available.insert({ghostUs.updateTitleId,Device::USB});
    assert(!chooseGameLaunch(Game::Ghosts,&ghostUs,lookup).titleId);
    assert(chooseGameLaunch(Game::BlackOpsII,&bo2,lookup).titleId==bo2.baseTitleId);
}
} // namespace

int main() {
    metadataTests();
    titleProbeTests();
    languageTests();
    ghostsTitleTests();
    gameLaunchTests();
    std::cout << "Install discovery tests passed: exact roots, XML metadata, physical storage, and existing languages.\n";
}
