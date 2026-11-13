# Changes in vscp-demo-wiznet-w55rp20-nfc

Every change that goes into a release is listed here, newest release first.
Add each change under **Unreleased** when it is made. When a release is
tagged, rename **Unreleased** to the version and date, and start a new
**Unreleased** section above it.

## Unreleased

- Fix the CI checksum step: readable SHA256SUMS files, bash 3.2 on macOS, and jobs that run in a subfolder
- Add scripts/download-artifacts.sh to download and verify the newest CI build artifacts
- CI writes a SHA256SUMS-<artifact> checksum file into every uploaded build artifact
- Interface working and brought up (819d8ad)
- Added VSCP firmware as submodule (6f66175)
- feat: add firmware cmake startup scaffold using pico and wiznet submodules (db7bf0c)
- Initial plan (693917a)
- Initial commit (965959b)

No release has been tagged yet.
