#include "support_archive.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
namespace fs=std::filesystem;
using namespace unbound;
static void save(const fs::path& path,const std::string& value){std::ofstream f(path,std::ios::binary);f.write(value.data(),value.size());assert(f.good());}
static std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
static std::map<std::string,std::string> unzip(const fs::path& path){
    int error=0;auto* zip=zip_open(path.string().c_str(),ZIP_RDONLY|ZIP_CHECKCONS,&error);assert(zip);
    std::map<std::string,std::string> files;
    for(zip_uint64_t i=0;i<static_cast<zip_uint64_t>(zip_get_num_entries(zip,0));++i){
        zip_stat_t st{};assert(zip_stat_index(zip,i,0,&st)==0&&st.comp_method==ZIP_CM_DEFLATE);
        auto* file=zip_fopen_index(zip,i,0);assert(file);std::string value(st.size,'\0');
        assert(zip_fread(file,value.data(),value.size())==static_cast<zip_int64_t>(value.size()));
        char extra=0;assert(zip_fread(file,&extra,1)==0&&zip_fclose(file)==0);files[st.name]=value;
    }
    assert(zip_close(zip)==0);return files;
}
int main(){
    fs::create_directories("r2-test");const auto root=fs::absolute("r2-test/support-export-"+std::to_string(getpid()));
    assert(!fs::exists(root)&&root.parent_path()==fs::absolute("r2-test"));fs::create_directory(root);
    const auto logs=root/"console";fs::create_directory(logs);std::map<std::string,std::string> originals;
    for(int i=0;i<100;++i)for(const auto* extension:{".log",".dsc"}){
        const auto name=std::to_string(i)+extension;const auto data="DSI/exception fixture "+name+std::string("\0\1\2\n",4);
        save(logs/name,data);originals[name]=data;
    }
    save(logs/"state.bin",std::string("\0\4\0\xff",4));originals["state.bin"]=read(logs/"state.bin");
    const auto performance=root/"performance.log";save(performance,"r66|install|ok=1|total_ms=2000\n");
    SupportArchivePlan plan;collectConsoleSupportLogs(plan,logs.string());addSupportLog(plan,performance.string(),"hq/performance.log",false);
    assert(plan.consoleFiles==201&&plan.files.size()==202&&plan.skipped==0&&plan.notes.empty());
    const auto output=root/"support-report.zip";std::string error;unsigned pulses=0;
    assert(writeSupportArchive(output.string(),"HQ diagnostic report",plan,error,[&](const std::string&){++pulses;}));
    assert(pulses>200);const auto archive=unzip(output);assert(archive.size()==204);
    assert(archive.at("hq/diagnostics.txt")=="HQ diagnostic report");
    assert(archive.at("export-summary.txt").find("Console files: 201")!=std::string::npos);
    for(const auto& [name,data]:originals){assert(archive.at("console/storage_slc/sys/logs/"+name)==data);assert(read(logs/name)==data);}
    assert(archive.at("hq/performance.log")==read(performance));const auto previous=read(output);
    fs::remove(performance);assert(!writeSupportArchive(output.string(),"New report",plan,error));
    assert(read(output)==previous&&!fs::exists(output.string()+".unbound-new"));
    auto invalid=plan;invalid.files[0].name="../outside.log";assert(!writeSupportArchive(output.string(),"Bad report",invalid,error)&&read(output)==previous);
    invalid=plan;invalid.files[0].name="hq/diagnostics.txt";assert(!writeSupportArchive(output.string(),"Duplicate",invalid,error)&&read(output)==previous);
    SupportArchivePlan missing;collectConsoleSupportLogs(missing,(root/"missing").string());
    assert(missing.consoleFiles==0&&!missing.notes.empty());assert(writeSupportArchive(output.string(),"HQ only",missing,error));
    assert(unzip(output).at("export-summary.txt").find("directory unavailable")!=std::string::npos);
    const auto empty=root/"empty";fs::create_directory(empty);SupportArchivePlan none;collectConsoleSupportLogs(none,empty.string());
    assert(none.consoleFiles==0&&supportArchiveSummary(none).find("No console crash log files")!=std::string::npos);
    const auto large=root/"oversized.log";{std::ofstream f(large,std::ios::binary);f.seekp(SupportLogMaximumBytes);f.put('x');}
    SupportArchivePlan bounded;addSupportLog(bounded,large.string(),"oversized.log",true);assert(bounded.files.empty()&&bounded.skipped==1);
    fs::create_directory(logs/"subfolder");SupportArchivePlan shallow;collectConsoleSupportLogs(shallow,logs.string());
    assert(shallow.consoleFiles==201&&shallow.skipped==1);
    assert(supportLogFilename("0.log")&&supportLogFilename("1.dsc")&&!supportLogFilename("../escape")&&!supportLogFilename("bad\\name")&&!supportLogFilename("a\nlog"));
    assert(root.parent_path()==fs::absolute("r2-test"));fs::remove_all(root);
    std::cout<<"Support ZIP: 201 console logs/descriptors/binary files, HQ report, Deflate/CRC readback, unchanged sources, missing/empty logs, bounded/shallow scans and atomic failure retention passed\n";
}
