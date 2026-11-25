#include "install_feedback.hpp"
#include <cassert>
#include <iostream>

int main() {
    using unbound::installFailureFeedback;
    const auto link=installFailureFeedback("Package 1 stopped. Destination folder check failed: storage_usb01:/usr/title/0005000e/1010cf00/content/english/sound/loaded; mode=0xa100; Not a directory");
    assert(link.title=="Game folder needs attention");
    assert(link.message.find("storage_usb01")==std::string::npos);
    assert(link.message.find("mode=")==std::string::npos);
    assert(link.message.find("saved package was kept")!=std::string::npos);
    assert(installFailureFeedback("Package SHA-256 does not match the manifest").title=="Package could not be verified");
    assert(installFailureFeedback("Release metadata: correct SHA-256 but wrong size").title=="Release needs attention");
    assert(installFailureFeedback("Archive paths or size exceed safety limits").title=="Release needs attention");
    assert(installFailureFeedback("Server returned HTTP 404").title=="Download unavailable");
    assert(installFailureFeedback("Recv failure: Socket operation on non-socket").title=="Download interrupted");
    assert(installFailureFeedback("An SD read failed while checking the saved ZIP.").title=="SD card needs attention");
    assert(installFailureFeedback("Not enough SD space for this download").title=="More storage space needed");
    assert(installFailureFeedback("Not enough memory to verify the saved ZIP").title=="HQ needs to restart");
    assert(installFailureFeedback("ZIP CRC mismatch").title=="Release needs attention");
    assert(installFailureFeedback("Selected language is unavailable").title=="Check your install location");
    assert(installFailureFeedback("Unknown failure").message.find("before launching the game")!=std::string::npos);
    const auto progress=unbound::installProgressFeedback("Preparing destination folders: Unbound 0.2.0 (1/1)",
        "Destination 39 of 105\nstat: storage_usb01:/usr/title/0005000e/1010cf00/content/english/sound/loaded\nElapsed 00:18\nThis step is taking longer. Keep the console on.");
    assert(progress.title=="Preparing installation: Unbound 0.2.0 (1/1)");
    assert(progress.message.find("Destination 39 of 105")!=std::string::npos);
    assert(progress.message.find("Elapsed 00:18")!=std::string::npos);
    assert(progress.message.find("stat:")==std::string::npos);
    assert(progress.message.find("storage_usb01")==std::string::npos);
    const std::string measured="477.0 MB of 1287.8 MB\nTime remaining: 2m 04s  |  6.31 MiB/s";
    assert(unbound::installProgressFeedback("Checking saved package",measured).message==measured);
    std::cout<<"Actionable install feedback and technical-detail separation tests passed\n";
}
