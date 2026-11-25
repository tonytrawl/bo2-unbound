#include "diagnostics.hpp"
#include "config.hpp"

#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <string>

namespace {

const std::string Download =
    "r17|download|ok=1|total_ms=132814|curl_ms=125404|write_ms=26527|hash_ms=5857|queue_wait_ms=1636|bytes=204936880|mode=pipeline";
const std::string Install =
    "r17|install|ok=1|total_ms=214960|backup_ms=119365|zip_ms=30629|write_ms=63984|close_ms=506|bytes=719721992|zip_bytes=719721992|backup_bytes=719826434|mode=worker";

void contains(const std::string& text, const std::string& wanted) {
    if (text.find(wanted) == std::string::npos) {
        std::cerr << "Missing expected report text: " << wanted << '\n';
        assert(false);
    }
}

} // namespace

int main() {
    using namespace unbound;
    const auto records = parseDiagnosticsPerformanceLog(Download + "\r\n" + Install + "\n");
    assert(records.size() == 2);
    assert(records[0].revision == 17 && records[0].operation == "download" && records[0].success);
    assert(records[0].values.at("curl_ms") == 125404);
    assert(records[1].values.at("backup_bytes") == 719826434);
    const auto preservation=parseDiagnosticsPerformanceLog("r37|preserve|ok=1|total_ms=2300|planned_bytes=83968|required_bytes=8472576|mode=worker");
    assert(preservation.size()==1&&preservation[0].operation=="preserve");
    assert(preservation[0].values.at("planned_bytes")==83968&&!preservation[0].values.count("bytes"));
    assert(parseDiagnosticsPerformanceLog(formatDiagnosticsPerformanceRecord(preservation[0])).size()==1);
    assert(parseDiagnosticsPerformanceLog("r37|preserve|ok=0|total_ms=2300|planned_bytes=-1|mode=worker").empty());
    assert(parseDiagnosticsPerformanceLog("r37|preserve|ok=0|total_ms=2300|bytes=83968|mode=worker").empty());
    assert(parseDiagnosticsPerformanceLog("r1|download|ok=1|total_ms=0|bytes=0|mode=buffered\n").size() == 1);
    for (const auto& malformed : {
            "r1|download|ok=1|ok=0|total_ms=1|bytes=1|mode=pipeline",
            "r1|download|ok=1|total_ms=1|total_ms=2|bytes=1|mode=pipeline",
            "r1|download|ok=1|total_ms=-1|bytes=1|mode=pipeline",
            "r1|download|ok=1|total_ms=1|bytes=18446744073709551616|mode=pipeline",
            "r18446744073709551616|download|ok=1|total_ms=1|bytes=1|mode=pipeline",
            "r1|download|ok=1|total_ms=1|bytes=1|mode=private-token",
            "r1|upload|ok=1|total_ms=1|bytes=1|mode=pipeline",
            "r1|download|ok=2|total_ms=1|bytes=1|mode=pipeline",
            "r1|download|ok=1|bytes=1|mode=pipeline"})
        assert(parseDiagnosticsPerformanceLog(malformed).empty());
    const auto manifest = parseDiagnosticsPerformanceLog(Download + "|probe_ms=0|size_source=manifest");
    assert(manifest.size() == 1 && manifest[0].sizeSource == "manifest");
    assert(manifest[0].values.at("probe_ms") == 0);
    contains(formatDiagnosticsPerformanceRecord(manifest[0]), "|size_source=manifest");
    for (const auto* source : {"head", "unknown"}) {
        const auto result = parseDiagnosticsPerformanceLog(Download + "|size_source=" + source);
        assert(result.size() == 1 && result[0].sizeSource == source);
    }
    for (const auto& malformed : {
            Download + "|size_source=private-token",
            Download + "|size_source=manifest|size_source=head",
            Download + "|probe_ms=0|probe_ms=1",
            Install + "|size_source=manifest",
            Install + "|pipelined_files=-1",
            Install + "|input_buffer_bytes=18446744073709551616"})
        assert(parseDiagnosticsPerformanceLog(malformed).empty());
    auto replacedSource = manifest[0];
    replacedSource.sizeSource = "private-token";
    assert(formatDiagnosticsPerformanceRecord(replacedSource).empty());
    const auto unknown = parseDiagnosticsPerformanceLog(Download + "|account_id=secret|address=192.168.0.1");
    assert(unknown.size() == 1);
    const auto raw = formatDiagnosticsPerformanceRecord(unknown[0]);
    assert(raw.find("secret") == std::string::npos && raw.find("192.168") == std::string::npos);
    assert(parseDiagnosticsPerformanceLog(raw).size() == 1);
    std::string many;
    for (unsigned index = 0; index < 200; ++index) many += Download + '\n';
    assert(parseDiagnosticsPerformanceLog(many).size() == 128);
    assert(parseDiagnosticsPerformanceLog(std::string(2049, 'x') + "\n" + Download).size() == 1);
    assert(parseDiagnosticsPerformanceLog(std::string(DiagnosticsMaximumLogBytes + 1, 'x') + "\n" + Download).empty());
    assert(diagnosticsSafeMessage("Network https://user:secret@host/a?token=secret failed 192.168.1.5") ==
           "Network [network URL omitted] failed [IP omitted]");
    assert(diagnosticsSafeMessage("TOKEN=secret\nnext") == "[private value omitted]\nnext");
    assert(diagnosticsSafeMessage("NNID: username") == "[private value omitted]");
    assert(diagnosticsSafeMessage(std::string(8192, 'a')).size() == 4096);
    assert(diagnosticsSafeMessage("version 0.0.19-beta") == "version 0.0.19-beta");

    std::map<std::string, DiagnosticsPathInfo> paths;
    std::map<std::string, std::string> texts;
    const std::string updateRoot = "storage_usb01:/usr/title/0005000e/1010cf00";
    paths["fs:/vol/external01"] = {DiagnosticsPathKind::Directory, 0};
    paths["storage_usb01:/"] = {DiagnosticsPathKind::Directory, 0};
    paths["storage_mlc01:/"] = {DiagnosticsPathKind::Unavailable, 0};
    paths[updateRoot] = {DiagnosticsPathKind::Directory, 0};
    paths[std::string(config::AppWuhbPath)] = {DiagnosticsPathKind::File, 12203652};
    paths[std::string(config::CacheRoot)] = {DiagnosticsPathKind::Directory, 0};
    paths[std::string(config::SdRoot) + "/backups"] = {DiagnosticsPathKind::Directory, 0};
    paths[std::string(config::AromaRoot)] = {DiagnosticsPathKind::Directory, 0};
    paths[std::string(config::AromaRoot) + "/modules/ContentRedirectionModule.wms"] = {DiagnosticsPathKind::File, 4000};
    paths[std::string(config::SdModsRoot)] = {DiagnosticsPathKind::Directory, 0};
    paths[updateRoot + "/content/update.txt"] = {DiagnosticsPathKind::File, 13};
    texts[updateRoot + "/content/update.txt"] = "0.0.05P2.1\n";
    texts[std::string(config::SdRoot) + "/performance.log"] = Download + "\n" + Install + "\n";
    paths[std::string(config::SettingsPath)] = {DiagnosticsPathKind::File, 100};
    texts[std::string(config::SettingsPath)] = "plugin_auto_update.gamepad_mic_redirect=on\naccount=private\n";

    WorkshopPlugin modLink;
    modLink.key = "UNBOUND_MOD_LINK";
    modLink.name = "Unbound Mod Link";
    modLink.managedPaths = {"wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps"};
    WorkshopPlugin microphone;
    microphone.key = "GAMEPAD_MIC_REDIRECT";
    microphone.name = "GamePad Mic Redirect";
    microphone.managedPaths = {"wiiu/environments/aroma/plugins/GamePadMicRedirect.wps"};
    const auto receipt = std::string(config::PluginStateRoot) + "/unbound_mod_link.txt";
    const std::string hash(64, 'a');
    texts[receipt] = "UNBOUND_MOD_LINK|4|0.2.4|https://example.com/package.zip|" + hash + "|123\n" +
        "FILE|wiiu/environments/aroma/plugins/BO2-Unbound-Redirector.wps|" + hash + "|1000\n";
    paths[receipt] = {DiagnosticsPathKind::File, texts[receipt].size()};
    paths[std::string(config::AromaRoot) + "/plugins/BO2-Unbound-Redirector.wps"] = {DiagnosticsPathKind::File, 1000};

    DiagnosticsIo io;
    unsigned inspections = 0;
    unsigned directoryCalls = 0;
    unsigned freeSpaceCalls = 0;
    io.inspectPath = [&](const std::string& path) {
        ++inspections;
        const auto found = paths.find(path);
        return found == paths.end() ? DiagnosticsPathInfo{DiagnosticsPathKind::Missing, 0} : found->second;
    };
    io.readText = [&](const std::string& path, std::string& output, std::size_t maximum) {
        if(path==config::LastInstallFailurePath)assert(maximum==DiagnosticsMaximumInstallFailureBytes);
        const auto found = texts.find(path);
        if (found == texts.end() || found->second.size() > maximum) return false;
        output = found->second;
        return true;
    };
    io.freeSpace = [&](const std::string&, std::uint64_t& value) {
        ++freeSpaceCalls;
        value = 2000000000;
        return true;
    };
    io.inspectDirectory = [&](const std::string& path, unsigned maximum) {
        ++directoryCalls;
        assert(maximum == DiagnosticsMaximumDirectoryEntries);
        assert(path == config::CacheRoot || path == std::string(config::SdRoot) + "/backups");
        return DiagnosticsDirectoryInfo{true, false, 4, 5, 0, 9000000};
    };
    DiagnosticsContext context;
    context.install.sku = &SupportedSkus[0];
    context.install.consoleRegion = Region::Japan;
    context.install.base = {Device::Disc, {}, 0, true};
    context.install.update = {Device::USB, updateRoot, 128, true};
    context.install.modVersion = "0.0.05P2.0";
    context.install.languageFolder = "english";
    context.latestUnboundVersion = "0.0.05P2.1";
    context.releaseCheckStatus = "Using current release feed";
    context.contentUpdatesAllowed = false;
    context.modLinkEnabled = true;
    context.lastError = "Failed at 192.168.1.1 https://host/path?token=secret";
    context.plugins = {modLink, microphone};
    context.coreRecoveryStatus = "Repair needed: USB storage / english";
    context.pluginRestartRequired = true;
    const auto report = collectDiagnostics(context, io).text;
    assert(report.find("Unfinished core install: Repair needed: USB storage / english") != std::string::npos);
    assert(report.find("Plugin restart required (session): Yes") != std::string::npos);
    contains(report, "Official game update: v128 - current");
    contains(report, "Install destination mode: Auto");
    contains(report, "Saved install target: auto");
    contains(report, "Install destination ready: Yes");
    contains(report, "Full language install pending: No");
    contains(report, "Official update requirement: v128");
    contains(report, "Console region: Japan");
    contains(report, "Game region: USA");
    contains(report, "Base title ID: 000500001010CF00");
    contains(report, "Game-content writes: Blocked / read-only this session");
    contains(report, "Game-content backups: Disabled; no automatic rollback");
    contains(report, "Core ZIP download limit: 4294967295 bytes");
    contains(report, "Core expanded-package limit: 5368709120 bytes");
    contains(report, "Core individual-file limit: 2147483648 bytes");
    contains(report, "Failed game-content install: Relaunch HQ and retry the same package before playing.");
    contains(report, "Version marker is written only after its package succeeds.");
    contains(report, "HQ application and Aroma plugin updater recovery: Retained");
    contains(report, "Legacy game-content backups path:");
    contains(report, "Legacy backups are left untouched; no new game-content backups are created or automatically restored.");
    assert(report.find("Backup use is automatic") == std::string::npos);
    contains(report, "Marker value (current disk): 0.0.05P2.1");
    contains(report, "Internal memory free: Unavailable");
    contains(report, "Actual update destination free: 2000.00 MB");
    contains(report, "partial count (entry limit or unreadable entry)");
    contains(report, "Subfolder contents were not scanned.");
    contains(report, "Managed receipt version: 0.2.4 / r4");
    contains(report, "receipt size matches");
    contains(report, "Startup auto-update: ON (HQ-managed requirement)");
    contains(report, "GamePad Mic Redirect (GAMEPAD_MIC_REDIRECT)\nStartup auto-update: ON");
    contains(report, "Average during transfer: 1.63 MB/s");
    contains(report, "Average over full phase: 1.54 MB/s");
    contains(report, "Original-file backup (historical measurement): 119.365 s (119365 ms)");
    contains(report, "ZIP read/unpack: 30.629 s (30629 ms)");
    contains(report, "Runtime-loaded status and plugin hashes are not checked");
    assert(report.find("secret") == std::string::npos && report.find("192.168.1.1") == std::string::npos);
    assert(report.find("account=private") == std::string::npos);
    assert(inspections < 100 && directoryCalls == 2 && freeSpaceCalls == 3);

    contains(report,"Last installation failure (saved): None recorded");
    const std::string savedFailurePath=config::LastInstallFailurePath;
    const std::string savedFailure=
        "HQ 0.0.33-beta / r33\nTarget: "+updateRoot+"\nLanguage: english\n"
        "Package 1 of 1 stopped.\nDestination folder check failed: "+updateRoot+
        "/content/english/sound/loaded\nstat: existing entry is not a directory; errno=20\n"
        "Network: https://user:password@example.com/payload?token=private-secret\n"
        "TOKEN: private-secret\nRemote address: 10.0.0.169\n";
    texts[savedFailurePath]=savedFailure;
    paths[savedFailurePath]={DiagnosticsPathKind::File,savedFailure.size()};
    auto laterSession=context;
    laterSession.lastError="Repair required before playing";
    const auto persistentFailure=collectDiagnostics(laterSession,io).text;
    contains(persistentFailure,"Last error: Repair required before playing");
    contains(persistentFailure,"Last installation failure (saved): HQ 0.0.33-beta / r33");
    contains(persistentFailure,"Destination folder check failed: "+updateRoot+"/content/english/sound/loaded");
    contains(persistentFailure,"errno=20");
    contains(persistentFailure,"Language: english");
    assert(persistentFailure.find("private-secret")==std::string::npos);
    assert(persistentFailure.find("password")==std::string::npos);
    assert(persistentFailure.find("10.0.0.169")==std::string::npos);
    contains(persistentFailure,"[network URL omitted]");
    contains(persistentFailure,"[private value omitted]");
    contains(persistentFailure,"[IP omitted]");
    laterSession.lastError.clear(); // A new app session has no previous generic error.
    contains(collectDiagnostics(laterSession,io).text,
             "Last installation failure (saved): HQ 0.0.33-beta / r33");
    const auto boundedLongFailure=std::string(5000,'x')+
        "\nFinal install failure: errno=20\nTOKEN=private-secret\n";
    texts[savedFailurePath]=boundedLongFailure;
    paths[savedFailurePath]={DiagnosticsPathKind::File,boundedLongFailure.size()};
    const auto longFailureReport=collectDiagnostics(laterSession,io).text;
    contains(longFailureReport,"Final install failure: errno=20");
    assert(longFailureReport.find("private-secret")==std::string::npos);
    paths[savedFailurePath]={DiagnosticsPathKind::File,DiagnosticsMaximumInstallFailureBytes+1};
    contains(collectDiagnostics(laterSession,io).text,
             "Last installation failure (saved): Unavailable (not a bounded readable text file)");
    paths[savedFailurePath]={DiagnosticsPathKind::Directory,0};
    contains(collectDiagnostics(laterSession,io).text,
             "Last installation failure (saved): Unavailable (not a bounded readable text file)");
    paths[savedFailurePath]={DiagnosticsPathKind::File,100};texts.erase(savedFailurePath);
    contains(collectDiagnostics(laterSession,io).text,
             "Last installation failure (saved): Unavailable (saved record could not be read)");
    texts[savedFailurePath]="\n \t\n";
    contains(collectDiagnostics(laterSession,io).text,
             "Last installation failure (saved): Saved record is empty");
    texts[savedFailurePath]=savedFailure;
    paths[savedFailurePath]={DiagnosticsPathKind::File,savedFailure.size()};

    texts[std::string(config::SdRoot) + "/performance.log"] =
        "r18|download|ok=1|total_ms=1000|curl_ms=500|bytes=2000000|resumed_bytes=1000000|network_bytes=1000000|rehash_ms=23|mode=pipeline\n";
    const auto resumed = collectDiagnostics(context, io).text;
    contains(resumed, "Average during transfer: 2.00 MB/s");
    contains(resumed, "Average over full phase: 1.00 MB/s");
    contains(resumed, "Resumed bytes: 1.00 MB");
    contains(resumed, "Received this attempt: 1.00 MB");
    contains(resumed, "Resume-prefix rehash: 0.023 s");

    texts[std::string(config::SdRoot) + "/performance.log"] =
        "r19|install|ok=1|total_ms=95000|zip_ms=31000|write_ms=63000|close_ms=1000|bytes=719721992|zip_bytes=719721992|mode=worker\n";
    const auto noBackups = collectDiagnostics(context, io).text;
    contains(noBackups, "Latest install: Succeeded / r19 / worker");
    contains(noBackups, "ZIP read/unpack: 31.000 s");
    contains(noBackups, "Destination writes: 63.000 s");
    assert(noBackups.find("Original-file backup") == std::string::npos);
    assert(noBackups.find("Backup bytes:") == std::string::npos);

    texts[std::string(config::SdRoot) + "/performance.log"] =
        "r29|download|ok=1|total_ms=1000|curl_ms=980|probe_ms=0|size_source=manifest|bytes=2000000|mode=pipeline\n"
        "r29|install|ok=1|total_ms=65000|zip_ms=31000|write_ms=63000|close_ms=1000|bytes=719721992|zip_bytes=719721992|queue_wait_ms=10000|pipelined_files=4|sync_files=7|input_buffer_bytes=262144|mode=worker\n";
    const auto pipelined = collectDiagnostics(context, io).text;
    contains(pipelined, "Package size source: Release manifest (no HEAD request)");
    contains(pipelined, "Size probe: 0.000 s (0 ms)");
    contains(pipelined, "Install queue wait: 10.000 s (10000 ms)");
    contains(pipelined, "Pipelined files: 4");
    contains(pipelined, "Direct-write files: 7");
    contains(pipelined, "ZIP input buffer: 0.26 MB (262144 bytes)");
    contains(pipelined, "ZIP read/unpack and destination writes overlap for pipelined files.");
    contains(pipelined, "Do not add these component times together.");
    contains(pipelined, "|input_buffer_bytes=262144");
    contains(pipelined, "|pipelined_files=4");
    contains(pipelined, "|sync_files=7");
    contains(pipelined, "|size_source=manifest");

    texts[std::string(config::SdRoot) + "/performance.log"] =
        "r29|download|ok=1|total_ms=1000|probe_ms=50|size_source=head|bytes=10|mode=buffered\n"
        "r29|install|ok=1|total_ms=1|zip_ms=1|write_ms=0|close_ms=0|bytes=10|pipelined_files=0|sync_files=1|mode=synchronous\n";
    const auto direct = collectDiagnostics(context, io).text;
    contains(direct, "Package size source: HTTP HEAD probe");
    contains(direct, "Pipelined files: 0");
    assert(direct.find("destination writes overlap for pipelined files") == std::string::npos);
    texts[std::string(config::SdRoot) + "/performance.log"] =
        "r29|download|ok=1|total_ms=1000|probe_ms=50|size_source=unknown|bytes=10|mode=buffered\n";
    contains(collectDiagnostics(context, io).text, "Package size source: Unknown; bounded streaming fallback");

    const auto previousGame=config::activeGame;
    for(const auto game:{Game::BlackOpsII,Game::Ghosts}){
        config::activeGame=game;
        const auto localPath=std::string(config::gameStateRoot())+"/local-package-install.txt";
        const auto source=std::string(game==Game::Ghosts?"ghosts-local-package.zip":"bo2-local-package.zip");
        texts[localPath]="UNBOUND_LOCAL_PACKAGE_V1\nfs:/vol/external01/Packages/"+source+"\n";
        const auto localReport=collectDiagnostics(context,io).text;
        contains(localReport,"LOCAL PACKAGE RECOVERY");contains(localReport,"Record: "+localPath);
        contains(localReport,source);
    }
    config::activeGame=previousGame;
    const auto empty = collectDiagnostics({}, {}).text;
    contains(empty, "Detected game SKU: Not found");
    contains(empty, "No readable measurement recorded");
    contains(empty, "No plugin catalog was provided");
    assert(empty.find("Average during transfer: 0") == std::string::npos);
    std::cout << "Diagnostics tests passed\n";
}
