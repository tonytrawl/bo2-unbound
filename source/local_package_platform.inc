// Local package I/O shares the normal verified extractor and file replacement policy.
namespace {
bool localSdDirectory(const std::string& directory,std::string& error){
    if(!sdBrowsePath(directory)){error="Choose a folder on the SD card.";return false;}
    std::string current=SdBrowseRoot;
    const auto check=[&](){struct stat st{};return lstat(current.c_str(),&st)==0&&S_ISDIR(st.st_mode);};
    if(!check()){error="The SD card is unavailable.";return false;}
    std::istringstream parts(directory.size()>current.size()?directory.substr(current.size()+1):"");std::string part;
    while(std::getline(parts,part,'/')){current+="/"+part;if(!check()){error="The selected SD folder is unavailable or is a link.";return false;}}
    return true;
}
bool localPackageMapper(const std::string& name,std::string& target,std::string& logical,void* opaque){
    return coreMapper(name,target,logical,opaque);
}
}

bool listSdPackages(const std::string& directory,std::vector<SdPackageEntry>& entries,std::string& error){
    entries.clear();error.clear();if(!localSdDirectory(directory,error))return false;
    DIR* folder=opendir(directory.c_str());if(!folder){error="Could not open the SD folder.";return false;}
    while(auto* entry=readdir(folder)){
        const std::string name=entry->d_name;if(name=="."||name=="..")continue;
        const auto path=directory+"/"+name;if(!sdBrowsePath(path))continue;
        struct stat st{};if(lstat(path.c_str(),&st)!=0)continue;
        const bool isDirectory=S_ISDIR(st.st_mode);
        if(!isDirectory&&(!S_ISREG(st.st_mode)||!zipFilename(name)))continue;
        if(entries.size()>=512){closedir(folder);entries.clear();error="This folder has more than 512 folders and ZIPs. Use a smaller package folder.";return false;}
        entries.push_back({name,path,isDirectory,st.st_size>0?static_cast<std::uint64_t>(st.st_size):0});
    }
    closedir(folder);
    const auto folded=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;};
    std::sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b){return a.directory!=b.directory?a.directory>b.directory:folded(a.name)<folded(b.name);});
    if(directory!=SdBrowseRoot)entries.insert(entries.begin(),{"..",parentPath(directory),true,0});
    return true;
}

bool inspectLocalPackage(const std::string& archive,const GameInstall& install,LocalPackageInfo& info,
                         std::string& error,const ProgressCallback& progress){
    info={};error.clear();
    if(!sdBrowsePath(archive)||!zipFilename(archive)||!localSdDirectory(parentPath(archive),error)){
        if(error.empty())error="Select a ZIP from your SD card.";
        return false;
    }
    struct stat before{};
    if(lstat(archive.c_str(),&before)!=0||!S_ISREG(before.st_mode)||before.st_size<=0||
       static_cast<unsigned long long>(before.st_size)>config::MaxArchiveBytes){error="The ZIP is unavailable or exceeds the 4 GiB minus one byte limit.";return false;}
    BufferedZipReader reader;if(!reader.open(archive.c_str())){error="This file is not a readable ZIP.";return false;}
    auto* zip=reader.get();const auto count=zip_get_num_entries(zip,0);
    if(count<=0||count>static_cast<zip_int64_t>(config::MaxArchiveFiles)){error="The ZIP entry count is outside HQ's limits.";return false;}
    std::set<std::string> targets;std::uint64_t total=0;
    for(zip_uint64_t i=0;i<static_cast<zip_uint64_t>(count);++i){
        zip_stat_t item{};
        if(zip_stat_index(zip,i,0,&item)!=0||!(item.valid&ZIP_STAT_NAME)||!(item.valid&ZIP_STAT_SIZE)){
            error="Could not inspect every ZIP entry.";return false;
        }
        std::string name=item.name;std::replace(name.begin(),name.end(),'\\','/');
        const bool directory=!name.empty()&&name.back()=='/';if(directory)name.pop_back();
        if(directory&&(name=="update"||name=="update/code"||name=="update/content"||name=="sd"))continue;
        if(archiveSizeLimit(total,item.size)!=ArchiveSizeLimit::None){error="The ZIP exceeds HQ's per-file or expanded-size limit.";return false;}total+=item.size;
        const auto route=packageRoute(name,install.languageFolder);std::string target,logical;
        if(route.area==PackageArea::Invalid||!coreMapper(name,target,logical,const_cast<GameInstall*>(&install))){
            error="Unsupported package path or unavailable update destination: "+name+". Use update/code, update/content, or sd folders.";return false;
        }
        if(item.comp_method!=ZIP_CM_STORE&&item.comp_method!=ZIP_CM_DEFLATE){error="Use standard ZIP Store or Deflate compression.";return false;}
        if((item.valid&ZIP_STAT_ENCRYPTION_METHOD)&&item.encryption_method!=ZIP_EM_NONE){error="Encrypted ZIPs are not supported.";return false;}
        if(directory)continue;
        if(route.relative.empty()||route.logical=="update/code"||route.logical=="update/content"){
            error="A file occupies a package folder: "+name;return false;
        }
        const auto key=lowerValue(target);
        if(key==lowerValue(archive)||!targets.insert(key).second){error="The ZIP duplicates a destination or replaces its own source file.";return false;}
        if(route.area==PackageArea::Sd){++info.sdFiles;info.sdBytes+=item.size;}
        else{++info.updateFiles;info.updateBytes+=item.size;}
        if(progress)progress("Checking local package","File "+std::to_string(i+1)+" of "+std::to_string(count),-1);
    }
    if(!reader.close()||(!info.updateFiles&&!info.sdFiles)){error="The ZIP has no installable payload files.";return false;}
    if(progress)progress("Checking selected ZIP","Binding the review to this exact ZIP...",-1);
    if(!sha256File(archive,info.sha256)){error="Could not verify the selected ZIP.";return false;}
    struct stat after{};
    if(lstat(archive.c_str(),&after)!=0||after.st_size!=before.st_size||after.st_mtime!=before.st_mtime){error="The ZIP changed while being inspected. Select it again.";return false;}
    info.path=archive;info.compressedBytes=static_cast<std::uint64_t>(before.st_size);return true;
}

bool installLocalPackage(const LocalPackageInfo& info,const GameInstall& install,std::string& error,
                         const ProgressCallback& progress,const std::function<bool(std::string&)>& beforeMutation){
    if(!sdBrowsePath(info.path)||!localSdDirectory(parentPath(info.path),error))return false;
    if(checkDownloadedPackage(info.path,info.sha256,info.compressedBytes,error,progress)!=PackageVerificationStatus::Valid){
        error="The selected ZIP could not be verified after confirmation. "+error;return false;
    }
    if(info.updateFiles&&!revalidateInstallDestination(install,error,true))return false;
    return extractArchive(info.path,localPackageMapper,const_cast<GameInstall*>(&install),error,progress,{}, {},beforeMutation,
                          info.updateFiles?&install:nullptr);
}
