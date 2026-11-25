#include "core_recovery.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <vector>

namespace {

using namespace unbound;

const std::string DigestA(64, 'a');
const std::string DigestB(64, 'b');
const std::string DigestC(64, 'c');

Release full() {
    return {"UNIVERSAL", ReleaseKind::Full, "", "0.0.05",
            "https://github.com/publisher/mod/releases/download/v5/full.zip", DigestA, 200000000};
}
Release patchOne() {
    return {"UNIVERSAL", ReleaseKind::Patch, "0.0.05", "0.0.05P2.0",
            "https://github.com/publisher/mod/releases/download/v5-p2/patch.zip", DigestB, 8000000};
}
Release patchTwo() {
    return {"UNIVERSAL", ReleaseKind::Patch, "0.0.05P2.0", "0.0.05P2.1",
            "https://github.com/publisher/mod/releases/download/v5-p2-1/patch.zip", DigestC, 8100000};
}

GameInstall target(Device device = Device::USB, std::size_t skuIndex = 0) {
    GameInstall install;
    install.sku = &SupportedSkus[skuIndex];
    install.languageFolder = "English";
    install.modVersion = "Not installed";
    CoreRecoveryRecord address;
    address.device = device;
    address.titleId = install.sku->updateTitleId;
    install.update = {device, coreRecoveryTargetRoot(address), 128, true};
    return install;
}

CoreRecoveryRecord record() {
    const auto base = full();
    const auto first = patchOne();
    const auto second = patchTwo();
    CoreRecoveryRecord result;
    std::string error;
    assert(beginCoreRecovery(target(), {&base, &first, &second}, false, result, error));
    return result;
}

void roundTrip(const CoreRecoveryRecord& expected) {
    std::string text, error, decodedText;
    CoreRecoveryRecord decoded;
    assert(serializeCoreRecovery(expected, text, error));
    assert(text.size() < 2000);
    assert(parseCoreRecovery(text, decoded, error));
    assert(serializeCoreRecovery(decoded, decodedText, error));
    assert(text == decodedText);
    assert(decoded.language == "English");
}

void rejectText(const std::string& text) {
    CoreRecoveryRecord decoded = record();
    std::string error;
    assert(!parseCoreRecovery(text, decoded, error));
    assert(!error.empty());
    assert(decoded.plan.empty());
}

std::string replaceText(std::string text, const std::string& before, const std::string& after) {
    const auto index = text.find(before);
    assert(index != std::string::npos);
    text.replace(index, before.size(), after);
    return text;
}

void freshRepairTests() {
    const auto base = full();
    const auto first = patchOne();
    const auto second = patchTwo();
    const std::vector<const Release*> sameChain{&base, &first, &second};
    auto previous = record();
    std::string error, previousText, actualText;
    assert(markCoreRecoveryInstalling(previous, error));
    assert(serializeCoreRecovery(previous, previousText, error));
    auto install = target();
    install.modVersion = "0.0.05P2.0"; // External change conflicted with the old record.
    assert(reconcileCoreRecovery(previous, install.modVersion, error) == CoreRecoveryReconcile::Conflict);
    CoreRecoveryRecord fresh;
    assert(prepareCoreRecoveryRestart(previous, install, sameChain, fresh, error));
    assert(error.empty() && fresh.beforeVersion == install.modVersion);
    assert(fresh.currentStep == 0 && fresh.replayFull);
    assert(fresh.phase == CoreRecoveryPhase::Installing && coreRecoveryNeedsRepair(fresh));
    assert(!canDiscardCoreRecoveryDownload(fresh));
    assert(coreRecoveryMatchesInstall(fresh, install));
    assert(fresh.plan.size() == 3 && fresh.plan.front().kind == ReleaseKind::Full);
    assert(fresh.plan.back().version == previous.plan.back().version);
    // The old operation stays untouched until the caller commits the new tiny
    // journal. A cancelled download or restart cannot infer that game files are
    // healthy just because the independently written marker is already current.
    assert(serializeCoreRecovery(previous, actualText, error) && actualText == previousText);
    roundTrip(fresh);
    assert(reconcileCoreRecovery(fresh, install.modVersion, error) == CoreRecoveryReconcile::RetryRequired);
    assert(fresh.currentStep == 0 && coreRecoveryNeedsRepair(fresh));
    install.modVersion = "0.0.05";
    assert(prepareCoreRecoveryRestart(previous, install, sameChain, fresh, error));
    assert(reconcileCoreRecovery(fresh, install.modVersion, error) == CoreRecoveryReconcile::RetryRequired);
    assert(fresh.currentStep == 0); // Full replay never skips its first archive.
    install.modVersion = "0.0.05P2.1";
    assert(prepareCoreRecoveryRestart(previous, install, sameChain, fresh, error));
    assert(coreRecoveryNeedsRepair(fresh));
    for (const auto& marker : {std::string{}, std::string("Not installed")}) {
        install.modVersion = marker;
        assert(prepareCoreRecoveryRestart(previous, install, sameChain, fresh, error));
        assert(fresh.beforeVersion == "Not installed" && coreRecoveryNeedsRepair(fresh));
    }
    auto newerBase = base;
    newerBase.version = "0.1.0";
    newerBase.sha256 = DigestC;
    newerBase.downloadUrl = "https://github.com/publisher/mod/releases/download/v1/new-full.zip";
    auto newerPatch = first;
    newerPatch.fromVersion = newerBase.version;
    newerPatch.version = "0.1.0P1.0";
    install.modVersion = "0.0.05P2.0";
    assert(prepareCoreRecoveryRestart(previous, install, {&newerBase, &newerPatch}, fresh, error));
    assert(fresh.plan.size() == 2 && fresh.plan.front().sha256 == DigestC);
    assert(fresh.plan.back().version == newerPatch.version);
    // A partially completed old chain also restarts from the complete snapshot,
    // rather than trusting any completed old step or patch prerequisite.
    auto partial = previous;
    assert(advanceCoreRecovery(partial, base.version, error));
    assert(prepareCoreRecoveryRestart(partial, install, sameChain, fresh, error));
    assert(fresh.currentStep == 0 && fresh.plan.size() == 3 && coreRecoveryNeedsRepair(fresh));

    const auto rejected = [&](const CoreRecoveryRecord& old, const GameInstall& selected,
                              const std::vector<const Release*>& plan) {
        auto retained = record();
        std::string retainedText, afterText;
        assert(serializeCoreRecovery(retained, retainedText, error));
        assert(!prepareCoreRecoveryRestart(old, selected, plan, retained, error));
        assert(!error.empty());
        assert(serializeCoreRecovery(retained, afterText, error));
        assert(afterText == retainedText); // Failure cannot replace caller state.
    };
    for (unsigned field = 0; field < 7; ++field) {
        auto changed = install;
        if (field == 0) changed.update.device = Device::MLC;
        if (field == 1) changed.sku = &SupportedSkus[1];
        if (field == 2) changed.languageFolder = "French";
        if (field == 3) changed.update.root += "/../other";
        if (field == 4) changed.update.present = false;
        if (field == 5) changed.destinationReady = false;
        if (field == 6) changed.modVersion = "invalid";
        rejected(previous, changed, sameChain);
    }
    rejected(previous, install, {});
    rejected(previous, install, {nullptr});
    rejected(previous, install, {&first, &second}); // Patch-only repair is unsafe.
    rejected(previous, install, {&base}); // Would discard the old chain's newer patches.
    rejected(previous, install, {&base, &second}); // Missing required patch step.
    auto missingMarker = install;
    missingMarker.modVersion = "Not installed";
    // A missing/lowered external marker cannot authorize going below files
    // that the old operation may already have installed from its newer plan.
    rejected(previous, missingMarker, {&base, &first});
    auto wrongRegion = newerBase;
    wrongRegion.regionKey = "JPN";
    rejected(previous, install, {&wrongRegion});
    auto newerDisk = install;
    newerDisk.modVersion = "0.2.0";
    rejected(previous, newerDisk, {&newerBase, &newerPatch});
    auto invalid = previous;
    invalid.plan.front().sha256 = "bad";
    rejected(invalid, install, sameChain);
    auto complete = previous;
    while (complete.currentStep < complete.plan.size()) {
        assert(markCoreRecoveryInstalling(complete, error));
        const auto version = complete.plan[complete.currentStep].version;
        assert(advanceCoreRecovery(complete, version, error));
    }
    auto unchangedComplete = complete;
    assert(reconcileCoreRecovery(unchangedComplete, complete.beforeVersion, error) ==
           CoreRecoveryReconcile::Completed);
    assert(!coreRecoveryNeedsRepair(unchangedComplete));
    // The caller offers fresh repair only on Conflict. A completed journal can
    // also become stale if another installer changes its marker afterwards.
    auto staleComplete = complete;
    assert(reconcileCoreRecovery(staleComplete, install.modVersion, error) ==
           CoreRecoveryReconcile::Conflict);
    assert(prepareCoreRecoveryRestart(complete, install, sameChain, fresh, error));
    assert(fresh.currentStep == 0 && fresh.phase == CoreRecoveryPhase::Installing);
    assert(fresh.replayFull && coreRecoveryNeedsRepair(fresh));
    assert(fresh.beforeVersion == install.modVersion && coreRecoveryMatchesInstall(fresh, install));
    assert(serializeCoreRecovery(previous, actualText, error) && actualText == previousText);
}

void downloadSizeCorrectionTests() {
    auto original = record();
    original.plan.front().compressedBytes = 1257582;
    auto published = original.plan.front();
    published.compressedBytes = 1287763222;
    auto corrected = original;
    bool changed = false;
    std::string error, beforeText, afterText;
    assert(refreshCoreRecoveryDownloadSize(corrected, {published}, changed, error));
    assert(changed && error.empty());
    auto expected = original;
    expected.plan.front().compressedBytes = published.compressedBytes;
    assert(serializeCoreRecovery(corrected, afterText, error));
    assert(serializeCoreRecovery(expected, beforeText, error));
    assert(afterText == beforeText); // No other journal field may change.
    assert(corrected.device == original.device && corrected.titleId == original.titleId);
    assert(corrected.language == original.language && corrected.currentStep == original.currentStep);
    assert(corrected.beforeVersion == original.beforeVersion && corrected.phase == original.phase);
    assert(corrected.replayFull == original.replayFull && corrected.plan.size() == original.plan.size());
    assert(refreshCoreRecoveryDownloadSize(corrected, {published, published}, changed, error));
    assert(!changed && error.empty()); // Identical duplicate records are unambiguous.

    // Release identity is exact. A replacement archive, URL, region, kind,
    // prerequisite, or version is never adopted into an interrupted plan.
    for (const bool writing : {false, true}) {
        for (unsigned field = 0; field < 6; ++field) {
            auto unrelated = published;
            if (field == 0) unrelated.regionKey = "USA";
            if (field == 1) unrelated.kind = ReleaseKind::Patch;
            if (field == 2) unrelated.fromVersion = "0.0.04";
            if (field == 3) unrelated.version = "0.0.06";
            if (field == 4) unrelated.downloadUrl += "?replacement=1";
            if (field == 5) unrelated.sha256 = DigestB;
            auto retained = original;
            if (writing) assert(markCoreRecoveryInstalling(retained, error));
            assert(serializeCoreRecovery(retained, beforeText, error));
            assert(refreshCoreRecoveryDownloadSize(retained, {unrelated}, changed, error));
            assert(!changed && error.empty());
            assert(serializeCoreRecovery(retained, afterText, error));
            assert(afterText == beforeText);
        }
    }
    auto retained = original;
    assert(refreshCoreRecoveryDownloadSize(retained, {}, changed, error));
    assert(!changed && retained.plan.front().compressedBytes == original.plan.front().compressedBytes);
    for (const auto size : {0ULL, config::MaxArchiveBytes + 1}) {
        auto invalid = published;
        invalid.compressedBytes = size;
        retained = original;
        assert(!refreshCoreRecoveryDownloadSize(retained, {invalid}, changed, error));
        assert(!changed && !error.empty());
        assert(retained.plan.front().compressedBytes == original.plan.front().compressedBytes);
    }
    auto conflicting = published;
    ++conflicting.compressedBytes;
    retained = original;
    assert(!refreshCoreRecoveryDownloadSize(retained, {published, conflicting}, changed, error));
    assert(!changed && !error.empty());
    assert(retained.plan.front().compressedBytes == original.plan.front().compressedBytes);

    // A repair may correct its size without changing which exact payload must
    // be replayed. No marker, phase, step, target or completion is inferred.
    retained = original;
    assert(markCoreRecoveryInstalling(retained, error));
    auto expectedRepair = retained;
    expectedRepair.plan.front().compressedBytes = published.compressedBytes;
    assert(refreshCoreRecoveryDownloadSize(retained, {published}, changed, error));
    assert(changed && retained.plan.front().compressedBytes == published.compressedBytes);
    assert(retained.phase == CoreRecoveryPhase::Installing && retained.currentStep == 0);
    assert(coreRecoveryNeedsRepair(retained) && !canDiscardCoreRecoveryDownload(retained));
    assert(serializeCoreRecovery(retained, afterText, error));
    assert(serializeCoreRecovery(expectedRepair, beforeText, error));
    assert(afterText == beforeText);
    roundTrip(retained);
    CoreRecoveryRecord restored;
    assert(parseCoreRecovery(afterText, restored, error));
    assert(restored.phase == CoreRecoveryPhase::Installing && coreRecoveryNeedsRepair(restored));
    assert(reconcileCoreRecovery(restored, restored.beforeVersion, error) == CoreRecoveryReconcile::RetryRequired);
    assert(restored.currentStep == 0 && restored.phase == CoreRecoveryPhase::Installing);
    // A later package may also be corrected, without touching completed or
    // future steps in the captured chain.
    assert(advanceCoreRecovery(retained, retained.plan.front().version, error));
    assert(retained.currentStep == 1 && retained.phase == CoreRecoveryPhase::BeforeDownload);
    auto currentPatch = retained.plan[1];
    currentPatch.compressedBytes = 8101234;
    const auto completedBytes = retained.plan[0].compressedBytes;
    const auto futureBytes = retained.plan[2].compressedBytes;
    assert(refreshCoreRecoveryDownloadSize(retained, {published, currentPatch}, changed, error));
    assert(changed && retained.currentStep == 1);
    assert(retained.plan[1].compressedBytes == currentPatch.compressedBytes);
    assert(retained.plan[0].compressedBytes == completedBytes && retained.plan[2].compressedBytes == futureBytes);
    assert(markCoreRecoveryInstalling(retained, error));
    assert(advanceCoreRecovery(retained, retained.plan[1].version, error));
    assert(markCoreRecoveryInstalling(retained, error));
    assert(advanceCoreRecovery(retained, retained.plan[2].version, error));
    assert(refreshCoreRecoveryDownloadSize(retained, {published}, changed, error));
    assert(!changed && retained.phase == CoreRecoveryPhase::Completed);
    retained = original;
    retained.language = "../invalid";
    assert(!refreshCoreRecoveryDownloadSize(retained, {published}, changed, error));
    assert(!changed && !error.empty());
}

struct FakeStateIo {
    std::map<std::string, std::string> files;
    std::vector<std::string> reads, mutations;
    std::string unreadable, blockedRemove;
    bool blockedRename = false;

    core_recovery_detail::StateFile read(const std::string& path, CoreRecoveryRecord& value,
                                          std::string& error) {
        reads.push_back(path);
        if (path == unreadable) {
            error = "Injected read failure";
            return core_recovery_detail::StateFile::Invalid;
        }
        const auto found = files.find(path);
        if (found == files.end()) return core_recovery_detail::StateFile::Missing;
        return parseCoreRecovery(found->second, value, error) ? core_recovery_detail::StateFile::Present :
                                                              core_recovery_detail::StateFile::Invalid;
    }
    bool rename(const std::string& from, const std::string& to, std::string& error) {
        mutations.push_back("rename:" + from);
        if (blockedRename || files.find(from) == files.end()) {
            error = "Injected rename failure";
            return false;
        }
        files[to] = files.at(from);
        files.erase(from);
        return true;
    }
    bool remove(const std::string& path, std::string& error) {
        mutations.push_back("remove:" + path);
        if (path == blockedRemove) {
            error = "Injected remove failure";
            return false;
        }
        files.erase(path);
        return true;
    }
};

void fileStateTests() {
    using namespace core_recovery_detail;
    const std::string path = "approved/state/core-install.txt";
    std::string initialText, activeText, completedText, error;
    auto initial = record();
    auto active = initial;
    assert(markCoreRecoveryInstalling(active, error));
    assert(serializeCoreRecovery(initial, initialText, error));
    assert(serializeCoreRecovery(active, activeText, error));
    auto completed = active;
    for (const auto& version : {"0.0.05", "0.0.05P2.0", "0.0.05P2.1"}) {
        assert(advanceCoreRecovery(completed, version, error));
        if (completed.phase != CoreRecoveryPhase::Completed) assert(markCoreRecoveryInstalling(completed, error));
    }
    assert(serializeCoreRecovery(completed, completedText, error));
    CoreRecoveryRecord loaded;
    FakeStateIo io;
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Missing);
    assert(io.mutations.empty());
    io.files[path] = activeText;
    io.files[path + ".unbound-old"] = initialText;
    io.files[path + ".unbound-new"] = "incomplete temporary save";
    io.reads.clear();
    const auto originals = io.files;
    assert(loadStateWithIo(path, loaded, error, false, io) == CoreRecoveryLoadStatus::Pending);
    assert(loaded.phase == CoreRecoveryPhase::Installing);
    assert(io.reads.size() == 1 && io.mutations.empty() && io.files == originals);
    io.files[path] = "corrupted primary";
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Invalid);
    assert(io.mutations.empty()); // Never bypass a malformed authoritative journal.
    io.files[path] = activeText;
    io.unreadable = path;
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Invalid);
    assert(io.mutations.empty());

    // Atomic commit window: old exists, primary has not been renamed in yet.
    io = {};
    io.files[path + ".unbound-old"] = activeText;
    io.files[path + ".unbound-new"] = completedText;
    const auto oldWindow = io.files;
    assert(loadStateWithIo(path, loaded, error, false, io) == CoreRecoveryLoadStatus::Pending);
    assert(loaded.phase == CoreRecoveryPhase::Installing && io.files == oldWindow);
    assert(io.mutations.empty());
    io.blockedRename = true;
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Invalid);
    assert(io.files == oldWindow);
    io.blockedRename = false;
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Pending);
    assert(io.files.at(path) == activeText && !io.files.count(path + ".unbound-old"));
    assert(reconcileCoreRecovery(loaded, "0.0.05", error) == CoreRecoveryReconcile::Advanced);

    // Only the first small save may legitimately leave a sole .new file. A
    // later orphan is retained as Invalid, not forgotten or treated as current.
    io = {};
    io.files[path + ".unbound-new"] = initialText;
    assert(loadStateWithIo(path, loaded, error, false, io) == CoreRecoveryLoadStatus::Pending);
    assert(io.mutations.empty() && !io.files.count(path));
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Pending);
    assert(io.files.at(path) == initialText && !io.files.count(path + ".unbound-new"));
    for (const auto& orphan : {activeText, completedText, std::string("malformed")}) {
        io = {};
        io.files[path + ".unbound-new"] = orphan;
        assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Invalid);
        assert(io.mutations.empty() && io.files.size() == 1);
    }
    io = {};
    io.files[path + ".unbound-old"] = "malformed";
    io.files[path + ".unbound-new"] = initialText;
    assert(loadStateWithIo(path, loaded, error, true, io) == CoreRecoveryLoadStatus::Invalid);
    assert(io.mutations.empty());

    // Removing a journal is exact-match and metadata-only. Sidecars disappear
    // before primary, and any failed removal retains the authoritative state.
    io = {};
    io.files[path] = activeText;
    assert(!removeExactStateWithIo(path, completed, error, io));
    assert(io.files.at(path) == activeText && io.mutations.empty());
    io.files[path] = completedText;
    io.files[path + ".unbound-old"] = activeText;
    io.files[path + ".unbound-new"] = completedText;
    io.files["approved/cache/core-some.zip"] = "the verified archive";
    io.blockedRemove = path + ".unbound-old";
    assert(!removeExactStateWithIo(path, completed, error, io));
    assert(io.files.at(path) == completedText);
    assert(io.files.at(path + ".unbound-old") == activeText);
    io.mutations.clear();
    io.blockedRemove.clear();
    assert(removeExactStateWithIo(path, completed, error, io));
    assert(io.mutations == (std::vector<std::string>{"remove:" + path + ".unbound-new",
                                                   "remove:" + path + ".unbound-old", "remove:" + path}));
    assert(io.files.size() == 1 && io.files.count("approved/cache/core-some.zip"));
    io = {};
    io.files[path] = initialText;
    assert(canDiscardCoreRecoveryDownload(initial));
    assert(removeExactStateWithIo(path, initial, error, io));
    assert(io.files.empty());
    io = {};
    assert(removeExactStateWithIo(path, completed, error, io));
    assert(io.mutations.empty());
}

} // namespace

int main() {
    using namespace unbound;
    std::string error, text;
    // A private local test must lead back to a complete public install, not
    // skip the full package or create an invalid repair journal.
    {
        auto tested = target();
        tested.modVersion = "0.0.0";
        const std::vector<Release> releases{full(), patchOne(), patchTwo()};
        const auto restorePlan = releasePlanForSku(releases, tested.sku, tested.modVersion);
        assert(restorePlan.size() == 3 && restorePlan.front()->kind == ReleaseKind::Full);
        CoreRecoveryRecord restore, parsed;
        assert(beginCoreRecovery(tested, restorePlan, false, restore, error));
        assert(restore.beforeVersion == "0.0.0");
        assert(serializeCoreRecovery(restore, text, error));
        assert(parseCoreRecovery(text, parsed, error));
        assert(coreRecoveryMatchesInstall(parsed, tested));
    }
    auto saved = record();
    assert(validateCoreRecovery(saved, error));
    assert(!coreRecoveryNeedsRepair(saved));
    assert(canDiscardCoreRecoveryDownload(saved));
    assert(saved.phase == CoreRecoveryPhase::BeforeDownload && saved.currentStep == 0);
    assert(saved.beforeVersion == "Not installed");
    assert(coreRecoveryMatchesInstall(saved, target()));
    auto lowerCaseTarget=target();lowerCaseTarget.languageFolder="english";
    assert(coreRecoveryMatchesInstall(saved,lowerCaseTarget));
    lowerCaseTarget.update.device=Device::MLC;
    assert(!coreRecoveryMatchesInstall(saved,lowerCaseTarget));
    assert(coreRecoveryTargetRoot(saved) == "storage_usb01:/usr/title/0005000e/1010cf00");
    assert(coreRecoveryPreferences(saved).updateDevice == Device::USB);
    assert(coreRecoveryPreferences(saved).languageFolder == "English");
    roundTrip(saved);

    assert(serializeCoreRecovery(saved, text, error));
    CoreRecoveryRecord decoded;
    assert(parseCoreRecovery(text.substr(0, text.size() - 1), decoded, error));
    auto windowsText = text;
    for (std::size_t index = 0; index < windowsText.size(); ++index)
        if (windowsText[index] == '\n') windowsText.insert(index++, 1, '\r');
    assert(parseCoreRecovery(windowsText, decoded, error));
    auto largeArchive = saved;
    largeArchive.plan.front().compressedBytes = 3520339520ULL;
    roundTrip(largeArchive);

    for (const auto& bad : {
            replaceText(text, "HQCORE1", "HQCORE2"),
            replaceText(text, "target=usb:", "target=disc:"),
            replaceText(text, "0005000e1010cf00", "000500001010cf00"),
            replaceText(text, "0005000e1010cf00", "0005000c1010cf00"),
            replaceText(text, "0005000e1010cf00", "0005000e99999999"),
            replaceText(text, "target=usb:0005000e1010cf00", "target=../../code"),
            replaceText(text, "language=English", "language=../English"),
            replaceText(text, "language=English", "language=English/code"),
            replaceText(text, "language=English", "language=unknown"),
            replaceText(text, "phase=download", "phase=complete"),
            replaceText(text, "phase=download", "phase=delete"),
            replaceText(text, "step=0", "step=-1"),
            replaceText(text, "step=0", "step=4"),
            replaceText(text, "step=0", "step=18446744073709551616"),
            replaceText(text, "prior=Not installed", "prior=broken"),
            replaceText(text, "replay=0", "replay=true"),
            replaceText(text, "count=3", "count=0"),
            replaceText(text, "count=3", "count=33"),
            replaceText(text, "count=3", "count=2"),
            replaceText(text, "count=3", "count=4"),
            replaceText(text, "https://github.com/", "http://github.com/"),
            replaceText(text, "https://github.com/", "https:///"),
            replaceText(text, "https://github.com/", "https://user@github.com/"),
            replaceText(text, "full.zip", "full.zip#fragment"),
            replaceText(text, "full.zip", "full.zip extra"),
            replaceText(text, DigestA, std::string(64, 'x')),
            replaceText(text, DigestA, std::string(63, 'a')),
            replaceText(text, "|200000000", "|" + std::to_string(config::MaxArchiveBytes + 1)),
            replaceText(text, "|200000000", "|-1"),
            replaceText(text, "|200000000", "|18446744073709551616"),
            replaceText(text, "UNIVERSAL|FULL|-", "JPN|FULL|-"),
            replaceText(text, "UNIVERSAL|FULL|-", "UNIVERSAL|FULL|0.0.01"),
            replaceText(text, "UNIVERSAL|FULL|-", "UNIVERSAL|DELETE|-"),
            replaceText(text, "0.0.05|https", "0.0.9999999999|https"),
            replaceText(text, "0.0.05|https", "0.0.05P|https"),
            replaceText(text, "0.0.05|https", "0.0.05p2.0|https"),
            replaceText(text, "PATCH|0.0.05|0.0.05P2.0", "PATCH|0.0.05P3.0|0.0.05P2.0"),
            replaceText(text, "PATCH|0.0.05P2.0|0.0.05P2.1", "PATCH|0.0.05P2.2|0.0.05P2.1"),
            replaceText(text, "step=0\n", "step=0\nstep=0\n"),
            replaceText(text, "target=usb:0005000e1010cf00\n", "target=usb:0005000e1010cf00\ntarget=auto\n"),
            text + "target=auto\n", text + "\n", std::string(MaxCoreRecoveryBytes + 1, 'x'),
            std::string{}, text.substr(0, text.size() - 100)}) rejectText(bad);

    auto bad = saved;
    bad.phase = static_cast<CoreRecoveryPhase>(99);
    assert(!validateCoreRecovery(bad, error));
    assert(!canDiscardCoreRecoveryDownload(bad));
    bad = saved;
    bad.plan[1].kind = static_cast<ReleaseKind>(99);
    assert(!validateCoreRecovery(bad, error));
    assert(coreRecoveryProtectedCachePaths(bad).empty());
    bad = saved;
    bad.beforeVersion = "0.0.05P2.1";
    assert(!validateCoreRecovery(bad, error));
    bad = saved;
    bad.currentStep = 1;
    assert(!validateCoreRecovery(bad, error));

    // All roots come from the supported update IDs. There are no paths in the
    // journal that can point at save files, arbitrary titles or another device.
    for (std::size_t skuIndex = 0; skuIndex < std::size(SupportedSkus); ++skuIndex) {
        for (const auto device : {Device::MLC, Device::USB}) {
            auto install = target(device, skuIndex);
            install.languageFolder = skuIndex == 4 ? "fulljap" : "French";
            auto release = full();
            release.regionKey = install.sku->key;
            assert(beginCoreRecovery(install, {&release}, false, decoded, error));
            assert(coreRecoveryTargetRoot(decoded) == install.update.root);
            assert(coreRecoveryMatchesInstall(decoded, install));
            const auto preferences = coreRecoveryPreferences(decoded);
            assert(validInstallPreferences(preferences));
            install.languageFolder = "english";
            assert(!coreRecoveryMatchesInstall(decoded, install));
            install.languageFolder = preferences.languageFolder;
            install.update.root += "/../save";
            assert(!coreRecoveryMatchesInstall(decoded, install));
        }
    }

    auto changedTarget = target();
    changedTarget.update.root = "storage_mlc01:/usr/title/0005000e/1010cf00";
    const auto base = full();
    assert(!beginCoreRecovery(changedTarget, {&base}, false, decoded, error));
    changedTarget = target();
    changedTarget.update.present = false;
    assert(!beginCoreRecovery(changedTarget, {&base}, false, decoded, error));
    assert(!beginCoreRecovery(target(), {}, false, decoded, error));
    assert(!beginCoreRecovery(target(), {nullptr}, false, decoded, error));

    // A canceled download does not imply game writes, and a marker changed by
    // an external installer never authorizes skipping a saved package.
    auto downloading = saved;
    assert(reconcileCoreRecovery(downloading, "", error) == CoreRecoveryReconcile::RetryRequired);
    assert(!coreRecoveryNeedsRepair(downloading));
    assert(reconcileCoreRecovery(downloading, "0.0.05", error) == CoreRecoveryReconcile::Conflict);
    assert(downloading.currentStep == 0);

    assert(markCoreRecoveryInstalling(saved, error));
    assert(coreRecoveryNeedsRepair(saved));
    assert(!canDiscardCoreRecoveryDownload(saved));
    roundTrip(saved);
    assert(reconcileCoreRecovery(saved, "Not installed", error) == CoreRecoveryReconcile::RetryRequired);
    auto conflict = saved;
    for (const auto& marker : {"0.0.04", "0.0.05P2.1", "1.0.00", "broken"}) {
        assert(reconcileCoreRecovery(conflict, marker, error) == CoreRecoveryReconcile::Conflict);
        assert(conflict.currentStep == 0 && conflict.phase == CoreRecoveryPhase::Installing);
    }
    // Crash after committed marker but before tiny journal advancement.
    assert(reconcileCoreRecovery(saved, "0.0.05", error) == CoreRecoveryReconcile::Advanced);
    assert(saved.currentStep == 1 && saved.beforeVersion == "0.0.05");
    assert(saved.phase == CoreRecoveryPhase::BeforeDownload);
    assert(!canDiscardCoreRecoveryDownload(saved));
    roundTrip(saved);
    assert(!advanceCoreRecovery(saved, "0.0.05P2.0", error));
    assert(markCoreRecoveryInstalling(saved, error));
    assert(!advanceCoreRecovery(saved, "0.0.05P2.1", error));
    assert(advanceCoreRecovery(saved, "0.0.05P2.0", error));
    roundTrip(saved);
    assert(markCoreRecoveryInstalling(saved, error));
    assert(reconcileCoreRecovery(saved, "0.0.05P2.1", error) == CoreRecoveryReconcile::Completed);
    assert(saved.currentStep == 3 && saved.phase == CoreRecoveryPhase::Completed);
    assert(!coreRecoveryNeedsRepair(saved));
    assert(!canDiscardCoreRecoveryDownload(saved));
    assert(coreRecoveryProtectedCachePaths(saved).empty());
    roundTrip(saved);
    assert(!markCoreRecoveryInstalling(saved, error));
    assert(!advanceCoreRecovery(saved, "0.0.05P2.1", error));
    assert(reconcileCoreRecovery(saved, "0.0.05P2.1", error) == CoreRecoveryReconcile::Completed);
    assert(reconcileCoreRecovery(saved, "0.0.05P2.0", error) == CoreRecoveryReconcile::Conflict);

    // Reinstalling the same version needs explicit success from the installer.
    // Its existing marker cannot distinguish complete versus half-written files.
    auto install = target();
    install.modVersion = "0.0.05";
    assert(beginCoreRecovery(install, {&base}, true, decoded, error));
    assert(markCoreRecoveryInstalling(decoded, error));
    assert(reconcileCoreRecovery(decoded, "0.0.05", error) == CoreRecoveryReconcile::RetryRequired);
    assert(decoded.currentStep == 0 && coreRecoveryNeedsRepair(decoded));
    assert(advanceCoreRecovery(decoded, "0.0.05", error));
    assert(decoded.phase == CoreRecoveryPhase::Completed);
    install.modVersion = "0.0.06";
    assert(!beginCoreRecovery(install, {&base}, true, decoded, error));
    auto patch = patchOne();
    assert(!beginCoreRecovery(target(), {&patch}, true, decoded, error));
    assert(!beginCoreRecovery(target(), {&patch}, false, decoded, error));

    // A multi-package full replay has a valid base marker halfway through, but
    // the new language still needs its saved patches before launching the game.
    auto first = patchOne();
    auto second = patchTwo();
    install.modVersion = "0.0.05P2.1";
    assert(beginCoreRecovery(install, {&base, &first, &second}, true, decoded, error));
    assert(markCoreRecoveryInstalling(decoded, error));
    assert(advanceCoreRecovery(decoded, "0.0.05", error));
    assert(coreRecoveryNeedsRepair(decoded));
    assert(reconcileCoreRecovery(decoded, "0.0.05P2.1", error) == CoreRecoveryReconcile::Conflict);

    const auto initial = record();
    const auto paths = coreRecoveryProtectedCachePaths(initial);
    assert(paths.size() == 9);
    assert(paths[0] == std::string(config::CacheRoot) + "/core-" + DigestA + ".zip");
    assert(paths[1] == paths[0] + ".part" && paths[2] == paths[0] + ".part.resume");
    auto upper = full();
    upper.sha256 = std::string(64, 'A');
    assert(coreRecoveryArchivePath(upper) == paths[0]);
    upper.sha256 = "../bad";
    assert(coreRecoveryArchivePath(upper).empty());
    auto partial = initial;
    assert(markCoreRecoveryInstalling(partial, error));
    assert(advanceCoreRecovery(partial, "0.0.05", error));
    assert(coreRecoveryProtectedCachePaths(partial).size() == 6);
    assert(coreRecoveryProtectedCachePaths(partial).front().find(DigestB) != std::string::npos);

    // Feed changes do not alter the captured download URL, hashes, prerequisites
    // or remaining chain. A recovery record never rebuilds its plan from latest.
    std::string frozen;
    assert(serializeCoreRecovery(initial, frozen, error));
    auto newestUnrelated = patchTwo();
    newestUnrelated.version = "1.0.00";
    assert(parseCoreRecovery(frozen, decoded, error));
    assert(decoded.plan.back().version == "0.0.05P2.1");
    assert(decoded.plan.back().version != newestUnrelated.version);
    assert(decoded.plan.back().sha256 == DigestC);

    freshRepairTests();
    downloadSizeCorrectionTests();
    fileStateTests();

    std::cout << "Core install recovery tests passed\n";
}
