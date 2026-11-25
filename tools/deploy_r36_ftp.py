"""Upload the user-authorized r36 WUHB, preserving the currently installed app."""
from ftplib import FTP
from hashlib import sha256
import json
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
PACKAGE = PROJECT / "staging-r36/BO2-Unbound-HQ-v0.0.36-beta-TEST"
REMOTE = "/fs/vol/external01/wiiu/apps/BO2-Unbound-HQ"
APP = "BO2-Unbound-HQ.wuhb"
STAGED = APP + ".upload-r36"
PREVIOUS = APP + ".pre-r36"


def remote_hash(ftp, name):
    digest = sha256()
    ftp.retrbinary("RETR " + name, digest.update, blocksize=256 * 1024)
    return digest.hexdigest()


def main():
    receipt = json.loads((PACKAGE / "BUILD_RECEIPT.json").read_text())
    app = PACKAGE / APP
    expected = receipt["wuhbSha256"]
    assert receipt["revision"] == 36
    assert app.stat().st_size == receipt["wuhbBytes"]
    assert sha256(app.read_bytes()).hexdigest() == expected
    with FTP() as ftp:
        ftp.connect("10.0.0.169", 21, timeout=20)
        ftp.login("anonymous", "")
        ftp.cwd(REMOTE)  # Missing/unmounted SD stops here, without any writes.
        ftp.voidcmd("TYPE I")
        if remote_hash(ftp, APP) == expected:
            print("r36 is already installed and its remote SHA-256 matches.")
            return
        names = set(ftp.nlst())
        if STAGED in names or PREVIOUS in names:
            raise RuntimeError("An existing r36 staging/backup file was retained. Inspect it before retrying.")
        print("Uploading verified r36 WUHB...", flush=True)
        with app.open("rb") as stream:
            ftp.storbinary("STOR " + STAGED, stream, blocksize=256 * 1024)
        if remote_hash(ftp, STAGED) != expected:
            raise RuntimeError("Staged read-back hash failed. Installed HQ was not changed.")
        ftp.rename(APP, PREVIOUS)
        try:
            ftp.rename(STAGED, APP)
        except Exception:
            ftp.rename(PREVIOUS, APP)
            raise
        if remote_hash(ftp, APP) != expected:
            # Preserve the suspect upload under the owned staging name and put
            # the previous application back. Never delete the recovery copy.
            ftp.rename(APP, STAGED)
            ftp.rename(PREVIOUS, APP)
            raise RuntimeError("Final read-back failed; the previous HQ was restored.")
        print("Installed r36 and verified its remote SHA-256:", expected)
        print("Previous WUHB retained at", REMOTE + "/" + PREVIOUS)


if __name__ == "__main__":
    main()
