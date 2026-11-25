// Executes the production stdio copy adapter on task-owned temporary files.
// Title/device discovery is excluded; native Wii U filesystem semantics still
// require hardware testing. Progress and free-space providers are injected.
#include "inherited_content.hpp"
#include "install_commit.hpp"
#include "io_buffer.hpp"
#include "sha256.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using namespace unbound;
using ProgressCallback=std::function<void(const std::string&,const std::string&,int)>;
struct EtaState {
    void observe(std::uint64_t,std::uint32_t){}
    bool shouldRender(int,std::uint32_t){return true;}
    std::string remaining(std::uint64_t,std::uint64_t,std::uint32_t){return "Measured test progress";}
};
std::string parentPath(const std::string& p){return p.substr(0,p.find_last_of('/'));}
std::string leafName(const std::string& p){return p.substr(p.find_last_of('/')+1);}
std::string dataAmount(std::uint64_t n){return std::to_string(n);}
struct FileIoContext {
    AlignedIoBuffer transfer,input,output;
    bool ready()const{return transfer&&input&&output;}
    bool prepareDirectory(const std::string& p){std::error_code e;std::filesystem::create_directories(p,e);return !e;}
    std::string directoryError()const{return "Test folder unavailable";}
};
unsigned long long available=64ULL*1024*1024;
std::function<void()> onSpaceCheck;
bool freeSpaceForPath(const std::string&,unsigned long long& out){
    out=available;if(onSpaceCheck){auto run=std::move(onSpaceCheck);onSpaceCheck={};run();}return true;
}
#define UNBOUND_NATIVE_INHERITED_COPY_TEST
#include "native_inherited_content.hpp"
#undef UNBOUND_NATIVE_INHERITED_COPY_TEST

void write(const std::string& path,const std::string& value){
    std::ofstream out(path,std::ios::binary);out.write(value.data(),static_cast<std::streamsize>(value.size()));out.close();assert(out.good());
}
std::string read(const std::string& path){std::ifstream in(path,std::ios::binary);assert(in.good());return {std::istreambuf_iterator<char>(in),{}};}
std::string hash(const std::string& path){std::string out;assert(sha256File(path,out));return out;}

void realCopiesAndPreservation(const std::string& root){
    const auto from=root+"/stock.sabl",to=root+"/restored.sabl",stage=root+"/stage.sabl";
    std::string bytes(1024*1024+37,'\0');for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>((i*131+i/257)&255);
    write(from,bytes);write(stage,std::string(bytes.size(),'x'));
    std::vector<std::pair<std::string,int>> updates;
    ProgressCallback callback=[&](const auto&,const std::string& detail,int percent){updates.emplace_back(detail,percent);};
    NativeInheritedProgress status{callback,0,bytes.size()*4,{}};
    NativeInheritedTreeIo io(status);std::string error;
    assert(io.copyInheritedFile(from,to,bytes.size(),false,error));
    assert(read(to)==bytes&&hash(to)==hash(from));
    assert(!std::filesystem::exists(to+".unbound-new"));
    assert(io.copyInheritedFile(from,stage,bytes.size(),true,error));
    assert(read(stage)==bytes&&!std::filesystem::exists(stage+".unbound-replacing"));
    write(to,"newer installed mod");
    assert(!io.copyInheritedFile(from,to,bytes.size(),false,error));
    assert(read(to)=="newer installed mod");
    assert(status.completed==bytes.size()*4); // Real copy reads + read-back checks.
    assert(status.copied==bytes.size()*2&&status.verified==bytes.size()*2);
    assert(!updates.empty());
    const auto actualBytes=std::to_string(bytes.size()*2);
    assert(updates.back().first.find("Copied "+actualBytes+" of "+actualBytes+" | Verified "+actualBytes)!=std::string::npos);
    for(const auto& update:updates){
        assert(update.first.find("copied and checked")==std::string::npos);
        assert(update.first.find(" of "+std::to_string(bytes.size()*4))==std::string::npos);
    }
    assert(updates.back().second==99);
}
void progressAccountingAndBounds(){
    std::string detail;int percent=-2;
    ProgressCallback callback=[&](const auto&,const std::string& update,int value){detail=update;percent=value;};
    NativeInheritedProgress status{callback,0,6,{}};
    status.report("Copying","/stock/bank.sabl",3);
    assert(detail.starts_with("Copying bank.sabl\nCopied 3 of 3 | Verified 0\n"));
    assert(status.completed==3&&status.copied==3&&status.verified==0&&percent==50);
    status.report("Checking","/stock/bank.sabl",3);
    assert(detail.starts_with("Checking bank.sabl\nCopied 3 of 3 | Verified 3\n"));
    assert(status.completed==6&&status.copied==3&&status.verified==3&&percent==99);
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    NativeInheritedProgress bounded{callback,0,maximum-1,{}};
    bounded.report("Copying","/stock/large.bik",maximum);
    assert(bounded.copied==(maximum-1)/2&&bounded.completed==bounded.copied&&percent==50);
    bounded.report("Checking","/stock/large.bik",maximum);
    assert(bounded.verified==(maximum-1)/2&&bounded.completed==maximum-1&&percent==99);
    bounded.report("Checking","/stock/large.bik",maximum);
    assert(bounded.verified==(maximum-1)/2&&bounded.completed==maximum-1);
    NativeInheritedProgress unknown{callback,0,0,{}};
    unknown.report("Copying","/stock/unknown.bik",maximum);
    unknown.report("Checking","/stock/unknown.bik",maximum);
    assert(unknown.completed==maximum&&unknown.copied==maximum&&unknown.verified==maximum&&percent==-1);
}
void spaceAndReferenceRefusal(const std::string& root){
    const auto from=root+"/small.stock",to=root+"/space.target",sentinel=root+"/sentinel";
    write(from,"abc");write(sentinel,"preserve me");
    ProgressCallback callback;NativeInheritedProgress status{callback,0,6,{}};
    NativeInheritedTreeIo io(status);std::string error;
    available=8ULL*1024*1024;assert(!io.copyInheritedFile(from,to,3,false,error));
    assert(!std::filesystem::exists(to)&&!std::filesystem::exists(to+".unbound-new"));
    available=64ULL*1024*1024;
    assert(symlink(sentinel.c_str(),to.c_str())==0);
    assert(!io.copyInheritedFile(from,to,3,false,error));assert(read(sentinel)=="preserve me");
    assert(unlink(to.c_str())==0);
    const auto link=root+"/source.link";assert(symlink(from.c_str(),link.c_str())==0);
    assert(!io.copyInheritedFile(link,to,3,false,error));assert(!std::filesystem::exists(to));
}
void detectedCorruptionAndSourceTruncation(const std::string& root){
    const auto from=root+"/checked.stock",to=root+"/checked.target";write(from,"abc");
    std::string error;bool tamper=true;
    ProgressCallback callback=[&](const auto&,const std::string& detail,int){
        if(tamper&&detail.starts_with("Checking")){write(to+".unbound-new","bad");tamper=false;}
    };
    NativeInheritedProgress status{callback,0,6,{}};NativeInheritedTreeIo io(status);
    assert(!io.copyInheritedFile(from,to,3,false,error));
    assert(error.find("integrity")!=std::string::npos&&!std::filesystem::exists(to));
    assert(!std::filesystem::exists(to+".unbound-new"));
    bool truncate=true;
    callback=[&](const auto&,const std::string& detail,int){if(truncate&&detail.starts_with("Copying")){write(from,"a");truncate=false;}};
    assert(!io.copyInheritedFile(from,to,3,false,error));
    assert(!std::filesystem::exists(to)&&!std::filesystem::exists(to+".unbound-new"));
    write(from,"abc");callback={};
    assert(io.copyInheritedFile(from,to,3,false,error)&&read(to)=="abc");
}
void destinationRace(const std::string& root){
    const auto from=root+"/race.stock",to=root+"/race.target";write(from,"abc");
    ProgressCallback callback;NativeInheritedProgress status{callback,0,6,{}};
    NativeInheritedTreeIo io(status);std::string error;
    onSpaceCheck=[&]{write(to,"concurrent user file");};
    assert(!io.copyInheritedFile(from,to,3,false,error));
    assert(read(to)=="concurrent user file");
}
}
int main(){
    char pattern[]="/tmp/unbound-native-inherited-XXXXXX";
    const char* made=mkdtemp(pattern);assert(made);const std::string root=made;
    assert(root.rfind("/tmp/unbound-native-inherited-",0)==0);
    progressAccountingAndBounds();realCopiesAndPreservation(root);spaceAndReferenceRefusal(root);
    detectedCorruptionAndSourceTruncation(root);destinationRace(root);
    std::filesystem::remove_all(root);
    std::cout<<"Production inherited-copy adapter: separate copy/verification progress, bounded counters, exact binary/SHA, verified stage replacement, live-file preservation, space, typed links, corruption, truncated source and target race passed\n";
}
