#include "disc_source.hpp"
#include <cassert>
#include <map>
#include <set>
#include <iostream>
using namespace unbound;
std::string metadata(const std::string& id,const std::string& version="0000"){
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?><app type=\"complex\">"
        "<title_id type=\"hexBinary\">"+id+"</title_id><title_version type=\"hexBinary\">"+
        version+"</title_version></app>";
}
struct Io {
    std::map<std::string,std::string> files;
    std::set<std::string> dirs;
    std::vector<unsigned> attachments;
    bool attach(unsigned volume){assert(volume<DiscSourceVolumeCount);attachments.push_back(volume);return true;}
    bool read(const std::string& path,std::string& text){if(!files.count(path))return false;text=files.at(path);return true;}
    bool directory(const std::string& path){return dirs.count(path);}
};
int main(){
    const std::uint64_t title=0x0005000010113500ULL;
    for(const std::string bad:{"/vol/storage_usb01/usr/title/00050000/10113500",
            "/vol/storage_odd_content/../storage_mlc01", "/vol/storage_odd_content/./title",
            "/vol/storage_odd_content3/usr/title/a", "/vol/storage_odd_content/a//b",
            "/vol/storage_odd033", "/vol/storage_odd03/../storage_mlc01", "/vol/storage_odd05",
            "/vol/storage_odd03/a\\code", "/vol/storage_odd01"})
        assert(discSourceCandidates(bad,title).empty());
    {
        Io io;
        const std::string root="hq_odd03:/usr/title/00050000/10113500";
        io.files[root+"/code/app.xml"]=metadata("0005000010113500");
        io.dirs.insert(root+"/content");
        std::string content;std::uint16_t version=999;
        assert(resolveDiscSource("/vol/storage_odd_content/usr/title/00050000/10113500",title,content,version,io));
        assert(content==root+"/content"&&version==0);
        io.files[root+"/code/app.xml"]=metadata("0005000010113400");
        assert(!resolveDiscSource("/vol/storage_odd_content/usr/title/00050000/10113500",title,content,version,io));
        assert(content.empty());
    }
    Io second;
    second.files["hq_odd04:/code/app.xml"]=metadata("0005000010113500","0001");
    second.dirs.insert("hq_odd04:/content");std::string content;std::uint16_t version=0;
    assert(resolveDiscSource("/vol/storage_odd_content2",title,content,version,second));
    assert(content=="hq_odd04:/content"&&version==1);
    for(auto volume:second.attachments)assert(volume==3);
    second.dirs.clear();
    assert(!resolveDiscSource("/vol/storage_odd_content2",title,content,version,second));
    Io missing;assert(!resolveDiscSource("",title,content,version,missing));
    Io french;
    french.files["hq_odd03:/usr/title/00050000/10113400/code/app.xml"]=metadata("0005000010113400");
    french.dirs.insert("hq_odd03:/usr/title/00050000/10113400/content");
    assert(resolveDiscSource("/vol/storage_odd_content/usr/title/00050000/10113400",0x0005000010113400ULL,content,version,french));
    // r41 disc-user report: MCP lists the game at the OS mount /vol/storage_odd03,
    // whose root holds code/ and content/ directly.
    Io reported;
    reported.files["hq_disc03:/code/app.xml"]=metadata("0005000010113500","0002");
    reported.dirs.insert("hq_disc03:/content");
    assert(resolveDiscSource("/vol/storage_odd03",title,content,version,reported));
    assert(content=="hq_disc03:/content"&&version==2);
    for(auto volume:reported.attachments)assert(volume==0);
    const auto odd03=discSourceCandidates("/vol/storage_odd03",title);
    assert(odd03.size()==2&&odd03[0].root=="hq_disc03:/usr/title/00050000/10113500"&&odd03[1].root=="hq_disc03:/");
    reported.files["hq_disc03:/code/app.xml"]=metadata("0005000010113400");
    assert(!resolveDiscSource("/vol/storage_odd03",title,content,version,reported)); // wrong region disc
    Io fourth;
    fourth.files["hq_disc04:/code/app.xml"]=metadata("0005000010113500");
    fourth.dirs.insert("hq_disc04:/content");
    assert(resolveDiscSource("/vol/storage_odd04",title,content,version,fourth)&&content=="hq_disc04:/content");
    for(auto volume:fourth.attachments)assert(volume==1);
    // Without an MCP path every known disc mount is tried, reported mount first.
    Io unknown;
    unknown.files["hq_odd03:/code/app.xml"]=metadata("0005000010113500");
    unknown.dirs.insert("hq_odd03:/content");
    assert(resolveDiscSource("",title,content,version,unknown)&&content=="hq_odd03:/content");
    assert(unknown.attachments.front()==0);
    std::cout<<"Disc source identity, MCP title subpaths, both partitions, wrong title, unsafe roots and missing content passed\n";
}
