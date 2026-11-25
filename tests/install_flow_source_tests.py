"""Limited source-routing guardrails for the native entry point.

The portable integration tests exercise real integrity/recovery helpers. These
assertions additionally guard their actual main.cpp wiring without pretending
to execute a Wii U GUI, network operation, or installed-title filesystem.
"""
from pathlib import Path


def between(text: str, first: str, last: str) -> str:
    start = text.index(first)
    return text[start:text.index(last, start + len(first))]


def verify(project: Path) -> None:
    main = (project / "source/main.cpp").read_text(encoding="utf-8")
    platform = (project / "source/platform.cpp").read_text(encoding="utf-8")
    package = between(main, "bool getPackage(", "std::string packageBytes(")
    fresh_prompt = package.index('"Download needs replacing"')
    reset = package.index("resetCorePackageDownloadCache(hash,error)")
    network = package.index("downloadFile(url,path,error")
    assert fresh_prompt < reset < network
    assert package.index("path!=expectedPath") < reset
    assert "if(!resetCorePackageDownloadCache(hash,error))return false;" in package
    for protected in ("Cancelled", "MetadataMismatch", "ReadFailure"):
        start = package.index("checked==PackageVerificationStatus::" + protected)
        assert start < fresh_prompt
        assert "return false;" in package[start:fresh_prompt]
    assert package.index("checked==PackageVerificationStatus::Valid") < fresh_prompt
    assert "if(!downloadFile(" in package and "return false;" in package[network:]

    verification = between(platform, "PackageVerificationStatus checkDownloadedPackage(",
                           "bool resetCorePackageDownloadCache(")
    assert "options.expectedBytes=0;" in verification
    assert "classifyPackageIntegrity(result,actual,lower,expectedBytes)" in verification
    assert "PackageVerificationStatus::MetadataMismatch" in verification
    assert "if(status==PackageVerificationStatus::Valid&&downloadCancellationRequested(control))" in verification
    assert "control->markCancelled();status=PackageVerificationStatus::Cancelled;" in verification

    download = between(platform, "bool downloadFile(", "bool installApplicationUpdate(")
    assert "classifyPackageIntegrity(streamed,completeHash.finish(),expectedHash,expectedBytes)" in download
    assert 'retainedMetadataMismatch=destination==std::string(config::gameCacheRoot())+"/core-"+expectedHash+".zip";' in download
    assert "logResult(!retainedMetadataMismatch);" in download
    assert "return !retainedMetadataMismatch;" in download
    # Retaining a fully hashed core ZIP on a feed-size mismatch still returns
    # failure. It never silently authorizes installation with inconsistent data.
    assert download.index("if(ok&&expectedSha256)") < download.index("if (!ok) {")

    menu = between(main, "InstallMenuResult runInstallMenu(", "#ifdef UNBOUND_DIAGNOSTIC_SD")
    success = between(menu, 'notice="Core "+install.modVersion+',
                      "return InstallMenuResult::Completed;")
    assert 'waitModal(ui,"Unbound is ready"' in success
    assert "lastSessionError.clear();" in success
    assert "rescan()" not in success
    assert "if(downloadWasCancelled)" in menu
    assert "installFailureFeedback(installError)" in menu
    assert "lastSessionError=installError" in menu
    assert "refreshRecovery();notice=unfinishedSummary();" in menu
    # Startup, switching games and the Home action all use the unified content
    # menu. Completion returns home; backing out returns to Install / Update.
    for entrypoint in (between(main,"if(reviewInstallOnStartup){","const auto switchGame="),
                       between(main,"const auto switchGame=","while(WHBProcIsRunning()"),
                       between(main,"else if(homeAction==1)","else if(homeAction==2)")):
        assert "const auto result=openContentMenu(" in entrypoint
        assert "dashboard.returnToMenu(result==InstallMenuResult::Completed?0:1)" in entrypoint
    assert "inheritedRepairGate.blocked()||recoveryStatus" in main
    assert "Open Install / Update before launching the game" in main
    repair = between(main, "InheritedRepairOutcome ensureInheritedRepair(", "enum class AppUpdateOutcome")
    assert "if(info.requiredBytes>info.freeBytes)" in repair
    assert "repairInheritedContent(install,repairError,progress)" in repair
    assert "downloadFile(" not in repair and "getPackage(" not in repair
    assert "writeTextFileAtomic(config::gameInstallFailurePath()" in repair
    assert "refreshInheritedRepair(ui,install)||inheritedRepairGate.blocked()" in repair
    install_repair = menu.index("ensureInheritedRepair(ui,install,contentAllowed)")
    assert install_repair < menu.index("installCorePlan(ui,install,")
    assert "if(inherited==InheritedRepairOutcome::Blocked){notice=unfinishedSummary();continue;}" in menu
    assert main.index("refreshInheritedRepair(ui,install);",main.index("const auto loadGame=")) < main.index("bool reviewInstallOnStartup=false;")
    # Movie payload uses the game's existing native localized lookup. Keep the
    # original archive logical path for validation, but detect physical target
    # collisions AFTER routing. Check all localized parents before generic
    # stock-folder conversion can handle unrelated sound/FF paths.
    mapper = between(platform, "bool coreMapper(const std::string& name", "struct WorkshopContext")
    assert "packageRoute(name,install.languageFolder)" in mapper
    assert "packageDestination(route,install,config::activeGame)" in mapper
    routing = (project / "source/package_layout.hpp").read_text(encoding="utf-8")
    assert "routeMovieInstallPath(route.logical,install.languageFolder)" in routing
    assert "movie.destination.substr" in routing
    assert "MovieInstallPathStatus::Invalid" in routing
    assert "routeConsoleVideoArchivePath" not in mapper
    folders = between(platform, "bool prepareCoreContentPatchReferences(", "bool extractArchiveImpl(")
    assert "if(!entry.consoleVideo&&!addContentFolderLinkPrefixes" in folders
    assert "if(isMovieContentPath(install,*it))it=existingOwners.erase(it);" in folders
    extraction = between(platform, "bool extractArchiveImpl(", "bool extractArchive(")
    assert "routeMovieInstallPath(logical," in extraction
    assert "movieRoute.status==MovieInstallPathStatus::Movie" in extraction
    assert "movieRoute.destination==movieRoute.movieDirectory" in extraction
    assert "movieInstallTargetFits(target,directory)" in extraction
    assert extraction.index("!mapper(name,target,logical,context)") < extraction.index("mappedFiles.insert(duplicateKey)")
    assert extraction.index("prepareLocalizedMovieDirectories(") < extraction.index("prepareCoreContentPatchReferences(")
    assert extraction.index("prepareLocalizedMovieDirectories(") < extraction.index("beforeWrite(error)")
    assert extraction.index("beforeWrite(error)") < extraction.index("extractCurrent(zip,")
    movie_folders = between(platform, "bool prepareLocalizedMovieDirectories(", "struct CoreContentFolderLinkIo")
    assert "lstat(path.c_str(), &info)" in movie_folders
    assert "prepareDirectoryPrefixes(" in movie_folders
    for forbidden in ("materializeTitleFolderLink", "FSAOpen", "folders.directories.insert", "std::rename", "std::remove"):
        assert forbidden not in movie_folders
    assert "preflightNativeVideoInstall(" not in platform + main
    assert "native_video_probe.hpp" not in platform and "native_video_directory.hpp" not in platform
    assert "consoleVideoRuntimeReady" not in main and "content/unbound/video" not in mapper
    assert "stockTitleRoot(install.update.device,install.sku->updateTitleId)" in routing
    # A missing original disc must fail source planning before the mutation
    # journal is armed; arm before the first stock-reference conversion and
    # again (idempotently) before payload writes if no conversion was needed.
    assert folders.index("batch.plan(paths,live,error)") < folders.index("beforeMutation(error)")
    assert folders.index("beforeMutation(error)") < folders.index("materializeTitleFolderLink(")
    assert extraction.index("beforeMutation(error)") < extraction.index("extractCurrent(zip,")
    step = between(main, "bool installCoreStep(", "bool installCorePlan(")
    assert "if(armed)return true;" in step
    assert "installCoreArchive(archive,install,release.version,error,progress,beforeMutation)" in step
    disc = (project / "source/native_inherited_content.hpp").read_text(encoding="utf-8")
    assert "matchingDiscTitle(install.sku->baseTitleId,disc)" in disc
    assert "resolveDiscSource(path,install.sku->baseTitleId,content,version,io)" in disc
    assert "Mocha_MountFS(source.alias,nullptr,source.root)" in disc
    # A null device attaches to the OS disc mount; HQ never mounts the drive itself.
    assert "Mocha_MountFS(source.alias,nullptr" in disc and "/dev/odd" not in disc
    # Converted shared movie folders from r35-r37 get their retained reference
    # back (renames only) before any stock copy; other movie owners stay skipped.
    repair_op = between(platform, "bool inheritedRepairOperation(", "bool prepareCoreContentPatchReferences(")
    assert "if(*it==sharedMovies){movieOwner=true;it=owners.erase(it);}" in repair_op
    assert repair_op.index("executeRetainedReferenceRestore(") < repair_op.index("batch.checkSpace(error)")
    assert '"\\.unbound-link-retired"' not in platform and 'ending==".unbound-link-retired"' in platform
    restore = between(platform, "struct RetainedReferenceIo {", "bool inheritedRepairOperation(")
    assert "routeMovieInstallPath(\"update/content/video\",install.languageFolder)" in restore
    assert "copy.st_mtime<=receipt.st_mtime" in restore
    # FSMakeLink stays unreachable until its argument contract is calibrated.
    link = (project / "source/native_link.hpp").read_text(encoding="utf-8")
    assert "inline constexpr bool NativeLinkArgumentsVerified = false;" in link
    assert "FSMakeLink(" not in platform and "FSAMakeLink" not in platform
    print("Native install source routing, cache retention, fresh reset and HQ-return guardrails passed")


if __name__ == "__main__":
    verify(Path(__file__).resolve().parents[1])
