// Portable integration of the production integrity, cache, replacement and
// repair-record helpers. This does not emulate Wii U native links or claim a
// hardware/full-UI test; those require the native build and a console run.
#include "core_recovery.hpp"
#include "file_hash_stream.hpp"
#include "install_commit.hpp"
#include "package_cache_reset.hpp"
#include "package_verification_policy.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {
using namespace unbound;

struct Reader {
    std::string data;
    std::size_t position = 0;
    unsigned closes = 0;
    bool cancel = false;
    file_hash_detail::ReadResult read(void* output, std::size_t maximum) {
        const auto count = std::min(maximum, data.size() - position);
        std::memcpy(output, data.data() + position, count);
        position += count;
        return {count, 0, position == data.size()};
    }
    file_hash_detail::SizeResult size() { return {data.size(), 0, true}; }
    int close() { ++closes; return 0; }
};

struct Storage {
    std::map<std::string, std::string> files;
    std::set<std::string> failRenameTarget;
    unsigned removes = 0;
    InstallPathState inspect(const std::string& path) const {
        return files.count(path) ? InstallPathState::RegularFile : InstallPathState::Missing;
    }
    bool renameFile(const std::string& from, const std::string& to) {
        if (!files.count(from) || files.count(to) || failRenameTarget.count(to)) return false;
        files[to] = files.at(from); files.erase(from); return true;
    }
    bool removeFile(const std::string& path) { ++removes; return files.erase(path) == 1; }
    CoreRecoveryCacheFile inspectCache(const std::string& path) const {
        return files.count(path) ? CoreRecoveryCacheFile::Regular : CoreRecoveryCacheFile::Missing;
    }
    bool removeCache(const std::string& path, std::string&) { return removeFile(path); }
};

std::string digest(const std::string& bytes) {
    Sha256 hash; hash.update(bytes.data(), bytes.size()); return hash.finish();
}

PackageIntegrityDecision check(const std::string& bytes, const Release& release,
                                bool cancel = false) {
    Reader reader{bytes};
    std::array<char, 7> buffer{};
    FileHashOptions options;
    // Production policy separates the feed length from the full read: a
    // matching SHA and wrong feed length must not authorize cache deletion.
    options.cancelled = [cancel] { return cancel; };
    std::string actual;
    const auto result = file_hash_detail::hashStream(reader, buffer.data(), buffer.size(),
                                                     bytes.size(), options, actual);
    assert(reader.closes == 1);
    return classifyPackageIntegrity(result, actual, release.sha256, release.compressedBytes);
}

GameInstall installTarget() {
    GameInstall install;
    install.sku = &SupportedSkus[0];
    install.languageFolder = "english";
    install.modVersion = "Not installed";
    CoreRecoveryRecord address;
    address.device = Device::USB; address.titleId = install.sku->updateTitleId;
    install.update = {Device::USB, coreRecoveryTargetRoot(address), 128, true};
    install.destinationReady = true;
    return install;
}

void save(const CoreRecoveryRecord& record, std::string& journal) {
    std::string error;
    assert(serializeCoreRecovery(record, journal, error));
}
CoreRecoveryRecord reload(const std::string& journal) {
    CoreRecoveryRecord record; std::string error;
    assert(parseCoreRecovery(journal, record, error));
    return record;
}

void corruptedCacheFreshDownloadThenRepairAndPatch() {
    const std::string fullBytes = "complete full release bytes";
    const std::string patchBytes = "complete patch bytes";
    const Release full{"UNIVERSAL", ReleaseKind::Full, "", "0.2.0",
        "https://github.com/example/unbound/releases/download/v0.2.0/full.zip",
        digest(fullBytes), fullBytes.size()};
    const Release patch{"UNIVERSAL", ReleaseKind::Patch, "0.2.0", "0.2.0P1.0",
        "https://github.com/example/unbound/releases/download/v0.2.0P1.0/patch.zip",
        digest(patchBytes), patchBytes.size()};
    auto install = installTarget();
    CoreRecoveryRecord record; std::string error, journal;
    assert(beginCoreRecovery(install, {&full, &patch}, false, record, error));
    save(record, journal);
    Storage storage;
    const auto cache = coreRecoveryArchivePath(full);
    for (const auto& suffix : {"", ".part", ".part.resume", ".part.resume.unbound-new", ".part.resume.unbound-old"})
        storage.files[cache + suffix] = "corrupt old bytes";
    const auto foreign = std::string(config::CacheRoot) + "/core-" + std::string(64, 'b') + ".zip";
    storage.files[foreign] = "unrelated download";
    assert(check(storage.files.at(cache), full) == PackageIntegrityDecision::InvalidContent);
    const auto recorded = journal;
    assert(clearCorePackageCacheWithIo(full.sha256, storage, error));
    assert(storage.files.size() == 1 && storage.files.at(foreign) == "unrelated download");
    assert(journal == recorded); // A fresh download cannot erase the repair plan.
    assert(storage.removes == 5);
    storage.files[cache] = fullBytes; // The network transfer is outside this portable test.
    assert(check(storage.files.at(cache), full) == PackageIntegrityDecision::Valid);
    assert(markCoreRecoveryInstalling(record, error)); save(record, journal);
    const auto root = install.update.root + "/content/";
    const auto first = root + "first.ff", second = root + "second.ff";
    storage.files[first] = "stock first"; storage.files[second] = "stock second";
    storage.files[first + ".unbound-new"] = "new first";
    storage.files[second + ".unbound-new"] = "new second";
    assert(commitInstallReplacement(first + ".unbound-new", first, error, storage));
    storage.failRenameTarget.insert(second);
    assert(!commitInstallReplacement(second + ".unbound-new", second, error, storage));
    assert(storage.files.at(first) == "new first");
    assert(storage.files.at(second + ".unbound-replacing") == "stock second");
    assert(install.modVersion == "Not installed");
    assert(storage.files.at(cache) == fullBytes);
    record = reload(journal);
    assert(coreRecoveryNeedsRepair(record));
    assert(reconcileCoreRecovery(record, install.modVersion, error) == CoreRecoveryReconcile::RetryRequired);
    assert(check(storage.files.at(cache), full) == PackageIntegrityDecision::Valid);
    storage.failRenameTarget.clear();
    assert(recoverInstallReplacement(second, error, storage));
    assert(storage.files.at(second) == "stock second");
    assert(commitInstallReplacement(second + ".unbound-new", second, error, storage));
    install.modVersion = full.version; // File/CRC completion precedes this marker in production.
    // Power loss after the marker but before saving the advanced journal must
    // resume the patch, without installing/downloading the base again.
    record = reload(journal);
    assert(reconcileCoreRecovery(record, install.modVersion, error) == CoreRecoveryReconcile::Advanced);
    assert(record.currentStep == 1 && record.plan.size() == 2);
    assert(record.phase == CoreRecoveryPhase::BeforeDownload);
    // This is a first install, not a language replay: the committed base is a
    // valid playable version while its newer patch is still being downloaded.
    assert(!coreRecoveryNeedsRepair(record));
    assert(!canDiscardCoreRecoveryDownload(record));
    save(record, journal);
    const auto patchCache = coreRecoveryArchivePath(patch);
    storage.files[patchCache] = patchBytes;
    assert(check(storage.files.at(patchCache), patch, true) == PackageIntegrityDecision::Cancelled);
    assert(reload(journal).currentStep == 1 && storage.files.at(patchCache) == patchBytes);
    assert(check(storage.files.at(patchCache), patch) == PackageIntegrityDecision::Valid);
    assert(markCoreRecoveryInstalling(record, error)); save(record, journal);
    storage.files[first + ".unbound-new"] = "patched first";
    assert(commitInstallReplacement(first + ".unbound-new", first, error, storage));
    install.modVersion = patch.version;
    assert(advanceCoreRecovery(record, install.modVersion, error)); save(record, journal);
    assert(record.phase == CoreRecoveryPhase::Completed);
    assert(!coreRecoveryNeedsRepair(reload(journal)));
    assert(coreRecoveryProtectedCachePaths(record).empty());
    assert(storage.files.at(first) == "patched first" && storage.files.at(second) == "new second");
    assert(storage.files.at(foreign) == "unrelated download");
}

void validBytesWrongFeedSizeNeverRequireFreshDownload() {
    const std::string bytes = "valid complete package";
    const Release release{"UNIVERSAL", ReleaseKind::Full, "", "0.2.0",
        "https://github.com/example/unbound/releases/download/v0.2.0/full.zip",
        digest(bytes), bytes.size() + 1};
    const auto decision = check(bytes, release);
    assert(decision == PackageIntegrityDecision::MetadataMismatch);
    assert(decision != PackageIntegrityDecision::InvalidContent);
    // Correcting only the manifest size makes these same saved bytes usable.
    auto corrected = release; corrected.compressedBytes = bytes.size();
    assert(check(bytes, corrected) == PackageIntegrityDecision::Valid);
}
} // namespace

int main() {
    corruptedCacheFreshDownloadThenRepairAndPatch();
    validBytesWrongFeedSizeNeverRequireFreshDownload();
    std::cout << "Install-flow integrity/cache/partial-replacement/repair/patch integration tests passed\n";
}
