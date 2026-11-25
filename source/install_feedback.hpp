#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace unbound {

struct InstallFeedback {
    std::string title;
    std::string message;
};

inline InstallFeedback installProgressFeedback(const std::string& title,const std::string& detail) {
    InstallFeedback view{title,{}};
    const std::string preparing="Preparing destination folders";
    if(view.title.rfind(preparing,0)==0)
        view.title="Preparing installation"+view.title.substr(preparing.size());
    bool folderLineShown=false;
    std::size_t offset=0;
    while(offset<=detail.size()) {
        const auto end=detail.find('\n',offset);
        std::string line=detail.substr(offset,end==std::string::npos?std::string::npos:end-offset);
        const bool path=(line.find("storage_")!=std::string::npos&&line.find(":/")!=std::string::npos)||
                        line.find("fs:/vol/")!=std::string::npos||line.find("/usr/title/")!=std::string::npos;
        if(path) {
            if(!folderLineShown){line="Checking game folders...";folderLineShown=true;}
            else line.clear();
        }
        if(!line.empty()){
            if(!view.message.empty())view.message+='\n';
            view.message+=line;
        }
        if(end==std::string::npos)break;
        offset=end+1;
    }
    return view;
}

// Presentation only: callers retain the complete original error in the saved
// report. Never use these phrases to authorize cache deletion or file writes.
inline InstallFeedback installFailureFeedback(std::string detail) {
    std::transform(detail.begin(), detail.end(), detail.begin(),
        [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
    const auto has=[&](const char* text){return detail.find(text)!=std::string::npos;};
    InstallFeedback result{"Installation stopped",
        "HQ could not finish installing this package. Your download and repair progress were kept.\n\n"
        "Retry from Install / Update before launching the game."};
    if(has("release metadata")||has("manifest contains")||has("manifest is missing")||
       has("archive paths or size")||has("unsafe archive")||has("no complete compatible")||
       has("crc")||has("archive is corrupt")) {
        result={"Release needs attention",
            "This release's package or download details are not valid. Repeated downloads will not fix it.\n\n"
            "Keep the saved download and check for a corrected release."};
    } else if(has("http 404")) {
        result={"Download unavailable",
            "The release file could not be found on the server.\n\nCheck for a corrected release, then retry from Install / Update."};
    } else if(has("sha-256")||has("sha256")||has("wrong size")||has("expected size")||has("zip ended")) {
        result={"Package could not be verified",
            "The download did not pass its integrity check. No new version was marked as installed.\n\n"
            "Retry from Install / Update. HQ will replace rejected download data before downloading again."};
    } else if(has("memory")||has("allocate")) {
        result={"HQ needs to restart",
            "There was not enough working memory to finish.\n\nReturn to the Wii U Menu and relaunch HQ, then retry from Install / Update."};
    } else if(has("not enough")||has("free space")||has("exhausting reserved")) {
        result={"More storage space needed",
            "There is not enough available space to finish.\n\nFree space on the SD card or the selected game drive, then retry from Install / Update."};
    } else if(has("destination folder")||has("could not create storage_")||has("folder reference")||
              has("filesystem link")||has("not a directory")) {
        result={"Game folder needs attention",
            "HQ could not prepare a game folder on the selected drive. Your saved package was kept.\n\n"
            "Check that the game drive is connected, then retry from Install / Update. If this repeats, export a Support report."};
    } else if(has("resolve host")||has("resolve proxy")||has("timed out")||has("timeout")||
              has("network")||has("socket")||has("http ")||has("ssl")||has("connection")) {
        result={"Download interrupted",
            "HQ could not finish the connection. Any safe download progress was kept.\n\n"
            "Check your internet connection, then retry from Install / Update."};
    } else if(has("sd read")||has("sd access")||has("on sd")||has("download file")||
              has("writing the download")||has("read buffer")) {
        result={"SD card needs attention",
            "HQ could not read or write the SD card. The saved package was kept where possible.\n\n"
            "Return to the Wii U Menu before checking the card. Relaunch HQ and retry after fixing access."};
    } else if(has("destination")||has("removed or moved")||has("disconnected")||has("language")) {
        result={"Check your install location",
            "The selected game drive or language is unavailable.\n\nOpen Install / Update, rescan your locations and save a valid selection before retrying."};
    }
    result.message+="\n\nIf installation had started, finish the repair before launching the game.\nDetails: Support > Export report.";
    return result;
}

} // namespace unbound
