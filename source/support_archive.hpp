#pragma once
#include "install_commit.hpp"
#include "model.hpp"
#include <zip.h>
#include <array>
#include <cstdint>
#include <dirent.h>
#include <functional>
#include <set>
#include <sstream>
#include <vector>

namespace unbound {
inline constexpr std::uint64_t SupportLogMaximumBytes=8*1024*1024;
inline constexpr std::uint64_t SupportLogsMaximumBytes=128*1024*1024;
inline constexpr std::size_t SupportLogsMaximumFiles=512;
struct SupportArchiveFile {std::string source,name;std::uint64_t bytes=0;bool console=false;};
struct SupportArchivePlan {
    std::vector<SupportArchiveFile> files;
    std::vector<std::string> notes;
    std::uint64_t bytes=0;
    unsigned consoleFiles=0,skipped=0;
    void note(const std::string& value){if(notes.size()<32)notes.push_back(value);}
};
inline bool supportLogFilename(const std::string& name){
    if(name.empty()||name.size()>180||name=="."||name=="..")return false;
    return std::all_of(name.begin(),name.end(),[](unsigned char c){return c>=32&&c<127&&c!='/'&&c!='\\'&&c!=':';});
}
inline void addSupportLog(SupportArchivePlan& plan,const std::string& source,const std::string& name,bool console){
    struct stat st{};
    if(lstat(source.c_str(),&st)!=0){if(console||errno!=ENOENT){++plan.skipped;plan.note("Could not inspect "+name);}return;}
    if(!S_ISREG(st.st_mode)){++plan.skipped;plan.note("Not a regular file: "+name);return;}
    if(st.st_size<0||static_cast<std::uint64_t>(st.st_size)>SupportLogMaximumBytes||
       plan.files.size()>=SupportLogsMaximumFiles||static_cast<std::uint64_t>(st.st_size)>SupportLogsMaximumBytes-plan.bytes){
        ++plan.skipped;plan.note("Export size/count limit: "+name);return;
    }
    plan.files.push_back({source,name,static_cast<std::uint64_t>(st.st_size),console});
    plan.bytes+=static_cast<std::uint64_t>(st.st_size);if(console)++plan.consoleFiles;
}
inline void collectConsoleSupportLogs(SupportArchivePlan& plan,const std::string& directory){
    DIR* folder=opendir(directory.c_str());
    if(!folder){plan.note("Console crash log directory unavailable: /storage_slc/sys/logs/");return;}
    std::vector<std::string> names;unsigned seen=0;errno=0;
    while(auto* entry=readdir(folder)){
        if(++seen>2048){plan.note("Console log directory entry limit reached.");++plan.skipped;break;}
        const std::string name=entry->d_name;
        if(name=="."||name=="..")continue;
        if(!supportLogFilename(name)){++plan.skipped;plan.note("Skipped an unsupported console log filename.");continue;}
        names.push_back(name);
    }
    const int scanError=errno;
    closedir(folder);if(scanError){++plan.skipped;plan.note("Console crash log directory could not be read completely.");}
    std::sort(names.begin(),names.end());
    for(const auto& name:names)addSupportLog(plan,directory+"/"+name,"console/storage_slc/sys/logs/"+name,true);
    if(!plan.consoleFiles)plan.note("No console crash log files were found.");
}
inline std::string supportArchiveSummary(const SupportArchivePlan& plan){
    std::ostringstream text;
    text<<"Unbound HQ support export\n\n"
        <<"hq/diagnostics.txt contains the selected game's current HQ report.\n"
        <<"Console crash/DSI logs were copied from /storage_slc/sys/logs/.\n"
        <<"All regular console log files are included, including .log and .dsc records.\n"
        <<"The original console logs are never modified or removed.\n\n"
        <<"Console files: "<<plan.consoleFiles<<"\nOther log files: "<<plan.files.size()-plan.consoleFiles
        <<"\nSkipped entries: "<<plan.skipped<<"\n";
    for(const auto& note:plan.notes)text<<"Note: "<<note<<'\n';
    text<<"\nIncluded source files:\n";
    for(const auto& file:plan.files)text<<file.name<<" | "<<file.bytes<<" bytes\n";
    return text.str();
}
inline bool writeSupportArchive(const std::string& destination,const std::string& report,
                                const SupportArchivePlan& plan,std::string& error,
                                const std::function<void(const std::string&)>& progress={}){
    error.clear();if(report.size()>1024*1024){error="The diagnostics report exceeds the export limit.";return false;}
    std::set<std::string> names{"hq/diagnostics.txt","export-summary.txt"};
    for(const auto& file:plan.files)if(!safeRelativePath(file.name)||!names.insert(file.name).second||file.source==destination||
       file.source==destination+".unbound-new"||file.source==destination+".unbound-replacing"||file.bytes>SupportLogMaximumBytes){
        error="The support export contains an invalid or duplicate entry.";return false;
    }
    if(!recoverInstallReplacement(destination,error))return false;
    const auto temporary=destination+".unbound-new";
    struct stat staged{};
    if(lstat(temporary.c_str(),&staged)==0&&!S_ISREG(staged.st_mode)){error="The support ZIP temporary path is not a regular file.";return false;}
    int code=0;zip_t* archive=zip_open(temporary.c_str(),ZIP_CREATE|ZIP_TRUNCATE,&code);
    if(!archive){error="Could not create the support ZIP on SD (ZIP error "+std::to_string(code)+").";return false;}
    const auto fail=[&](const std::string& message){error=message;if(archive)zip_discard(archive);archive=nullptr;std::remove(temporary.c_str());return false;};
    const auto add=[&](const std::string& name,zip_source_t* source){
        if(!source)return false;
        const auto index=zip_file_add(archive,name.c_str(),source,ZIP_FL_ENC_UTF_8);
        if(index<0){zip_source_free(source);return false;}
        return zip_set_file_compression(archive,static_cast<zip_uint64_t>(index),ZIP_CM_DEFLATE,6)==0;
    };
    const auto summary=supportArchiveSummary(plan);
    if(!add("hq/diagnostics.txt",zip_source_buffer(archive,report.data(),report.size(),0))||
       !add("export-summary.txt",zip_source_buffer(archive,summary.data(),summary.size(),0)))return fail("Could not add the diagnostics report to the support ZIP.");
    for(const auto& file:plan.files){
        if(progress)progress("Adding "+file.name);
        struct stat current{};
        if(lstat(file.source.c_str(),&current)!=0||!S_ISREG(current.st_mode)||current.st_size<0||static_cast<std::uint64_t>(current.st_size)!=file.bytes)
            return fail("A source log changed or became unavailable: "+file.name);
        if(!add(file.name,zip_source_file(archive,file.source.c_str(),0,static_cast<zip_int64_t>(file.bytes))))return fail("Could not add a source log: "+file.name);
    }
    if(progress)progress("Compressing the reports and console crash logs...");
    if(zip_close(archive)!=0)return fail("Could not finish the support ZIP: "+std::string(zip_strerror(archive)));
    archive=nullptr;
    archive=zip_open(temporary.c_str(),ZIP_RDONLY|ZIP_CHECKCONS,&code);
    if(!archive)return fail("Could not reopen the support ZIP for verification.");
    if(zip_get_num_entries(archive,0)!=static_cast<zip_int64_t>(names.size()))return fail("The support ZIP entry count did not verify.");
    std::array<char,32*1024> buffer{};
    for(const auto& name:names){
        if(progress)progress("Verifying "+name);
        zip_file_t* file=zip_fopen(archive,name.c_str(),0);
        if(!file)return fail("Could not verify a support ZIP entry: "+name);
        zip_int64_t read=0;std::uint64_t bytes=0;
        while((read=zip_fread(file,buffer.data(),buffer.size()))>0){bytes+=static_cast<std::uint64_t>(read);if(bytes>SupportLogMaximumBytes){read=-1;break;}}
        const auto closed=zip_fclose(file);
        if(read<0||closed!=0)return fail("Support ZIP data/CRC verification failed: "+name);
    }
    if(zip_close(archive)!=0)return fail("Could not close the verified support ZIP.");
    archive=nullptr;
    if(!commitInstallReplacement(temporary,destination,error))return false;
    return true;
}
}
