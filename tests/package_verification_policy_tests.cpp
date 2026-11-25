#include "package_verification_policy.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    using namespace unbound;
    const std::string hash(64, 'a'), other(64, 'b'), upper(64, 'A');
    const FileHashResult complete{FileHashStatus::Ok, 0, 1024, 1024};
    assert(classifyPackageIntegrity(complete, hash, hash, 1024) == PackageIntegrityDecision::Valid);
    assert(classifyPackageIntegrity(complete, hash, upper, 0) == PackageIntegrityDecision::Valid);
    assert(classifyPackageIntegrity(complete, hash, hash, 1000) == PackageIntegrityDecision::MetadataMismatch);
    assert(classifyPackageIntegrity(complete, hash, hash, 1025) == PackageIntegrityDecision::MetadataMismatch);
    assert(classifyPackageIntegrity(complete, other, hash, 1000) == PackageIntegrityDecision::InvalidContent);
    assert(classifyPackageIntegrity(complete, other, hash, 1024) == PackageIntegrityDecision::InvalidContent);
    assert(classifyPackageIntegrity(complete, "", hash, 1024) == PackageIntegrityDecision::InvalidContent);
    assert(classifyPackageIntegrity({FileHashStatus::Ok, 0, 1000, 1024}, hash, hash, 1000) == PackageIntegrityDecision::InvalidContent);
    for (const auto status : {FileHashStatus::Truncated, FileHashStatus::SizeChanged,
                              FileHashStatus::SizeMismatch})
        assert(classifyPackageIntegrity({status, 0, 512, 1024}, hash, hash, 1024) == PackageIntegrityDecision::InvalidContent);
    for (const auto status : {FileHashStatus::StatFailed, FileHashStatus::NotRegular,
            FileHashStatus::OpenFailed, FileHashStatus::BufferUnavailable,
            FileHashStatus::BufferSetupFailed, FileHashStatus::ReadFailed,
            FileHashStatus::CloseFailed})
        assert(classifyPackageIntegrity({status, 5, 512, 1024}, hash, hash, 1024) == PackageIntegrityDecision::ReadFailure);
    assert(classifyPackageIntegrity({FileHashStatus::Cancelled, 0, 512, 1024}, hash, hash, 1000) == PackageIntegrityDecision::Cancelled);
    for (const auto& invalid : {std::string{}, std::string(63, 'a'), std::string(64, 'z')})
        assert(classifyPackageIntegrity(complete, hash, invalid, 1024) == PackageIntegrityDecision::ReadFailure);
    std::cout << "Package verification cache-retention and metadata-mismatch policy tests passed\n";
}
