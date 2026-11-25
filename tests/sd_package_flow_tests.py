from pathlib import Path
s=Path("source/main.cpp").read_text()
step=s[s.index("bool installCoreStep("):s.index("bool installCorePlan(")]
assert step.index("sdPackageCandidates") < step.index("getPackage(ui,label")
assert "checkDownloadedPackage(candidate,release.sha256,release.compressedBytes" in step
assert "PackageVerificationStatus::Cancelled){downloadWasCancelled=true;return false;}" in step
assert "PackageVerificationStatus::MetadataMismatch)return false;" in step
assert "if(archive==cachedArchive)std::remove(archive.c_str());" in step
assert "std::remove(candidate" not in step and "rename(candidate" not in step
assert step.index("PackageVerificationStatus::Valid") < step.index("installCoreArchive(")
assert "beforeMutation" in step and "advanceCoreRecovery" in step
print("SD integration guards: verified selection, cancellation, internet fallback, source retention and recovery passed")
