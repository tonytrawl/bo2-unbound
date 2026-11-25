#include "core_recovery_record.hpp"
#include "core_recovery_restart.hpp"
#include "platform.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace unbound {
namespace {

using core_recovery_detail::StateFile;

StateFile readState(const std::string& path, CoreRecoveryRecord& record, std::string& error) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
        if (errno == ENOENT) return StateFile::Missing;
        error = "Could not inspect the install repair record: " + std::string(std::strerror(errno));
        return StateFile::Invalid;
    }
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        static_cast<unsigned long long>(info.st_size) > MaxCoreRecoveryBytes) {
        error = "The install repair record is not a bounded regular file.";
        return StateFile::Invalid;
    }
    std::string text;
    if (!readTextFile(path, text, MaxCoreRecoveryBytes)) {
        error = "Could not read the install repair record. HQ will not forget its unfinished installation.";
        return StateFile::Invalid;
    }
    return parseCoreRecovery(text, record, error) ? StateFile::Present : StateFile::Invalid;
}

bool removeStateFile(const std::string& path, std::string& error) {
    if (std::remove(path.c_str()) == 0 || errno == ENOENT) return true;
    error = "Could not clear install repair metadata: " + std::string(std::strerror(errno));
    return false;
}

struct StateIo {
    StateFile read(const std::string& path, CoreRecoveryRecord& record, std::string& error) {
        return readState(path, record, error);
    }
    bool rename(const std::string& from, const std::string& to, std::string& error) {
        if (std::rename(from.c_str(), to.c_str()) == 0) return true;
        error = "Could not restore the interrupted install repair record: " + std::string(std::strerror(errno));
        return false;
    }
    bool remove(const std::string& path, std::string& error) {
        return removeStateFile(path, error);
    }
};

} // namespace

std::string coreRecoveryStatePath() {
    return std::string(config::gameStateRoot()) + "/core-install.txt";
}

CoreRecoveryLoadStatus loadCoreRecovery(CoreRecoveryRecord& record, std::string& error,
                                        bool allowRecoveryWrites) {
    StateIo io;
    return core_recovery_detail::loadStateWithIo(coreRecoveryStatePath(), record, error,
                                                allowRecoveryWrites, io);
}

bool saveCoreRecovery(const CoreRecoveryRecord& record, std::string& error) {
    std::string text;
    if (!serializeCoreRecovery(record, text, error)) return false;
    // The standard text commit only copies this small journal. It never backs
    // up, scans or hashes any game file, keeping the fast installer unchanged.
    return writeTextFileAtomic(coreRecoveryStatePath(), text, error);
}

bool restartCoreRecovery(const CoreRecoveryRecord& previous,
                         const CoreRecoveryRecord& replacement,std::string& error) {
    struct RestartIo {
        bool read(CoreRecoveryRecord& record,std::string& failure) {
            if(loadCoreRecovery(record,failure,false)==CoreRecoveryLoadStatus::Pending)return true;
            if(failure.empty())failure="The unfinished install record is no longer available.";
            return false;
        }
        CoreRecoveryCacheFile inspectCache(const std::string& path) {
            struct stat info{};
            if(lstat(path.c_str(),&info)==0)
                return S_ISREG(info.st_mode)?CoreRecoveryCacheFile::Regular:CoreRecoveryCacheFile::Other;
            return errno==ENOENT?CoreRecoveryCacheFile::Missing:CoreRecoveryCacheFile::Error;
        }
        bool removeCache(const std::string& path,std::string& failure) {
            if(std::remove(path.c_str())==0||errno==ENOENT)return true;
            failure="Could not clear saved download "+path+": "+std::strerror(errno);
            return false;
        }
        bool save(const CoreRecoveryRecord& record,std::string& failure) {
            return saveCoreRecovery(record,failure);
        }
    } io;
    return restartCoreRecoveryWithIo(previous,replacement,io,error);
}

namespace {

bool removeExactState(const CoreRecoveryRecord& record, std::string& error) {
    StateIo io;
    return core_recovery_detail::removeExactStateWithIo(coreRecoveryStatePath(), record, error, io);
}

} // namespace

bool clearCoreRecovery(const CoreRecoveryRecord& record, std::string& error) {
    if (!validateCoreRecovery(record, error)) return false;
    if (record.phase != CoreRecoveryPhase::Completed) {
        error = "An unfinished installation cannot have its repair record cleared.";
        return false;
    }
    return removeExactState(record, error);
}

bool discardCoreRecoveryDownload(const CoreRecoveryRecord& record, std::string& error) {
    if (!canDiscardCoreRecoveryDownload(record)) {
        error = "This installation has begun writing game content. Complete its repair before choosing a different destination.";
        return false;
    }
    // ZIPs and resume pairs remain normal cache files. Clear only this exact
    // small metadata record; later normal cleanup may remove unused archives.
    return removeExactState(record, error);
}

} // namespace unbound
