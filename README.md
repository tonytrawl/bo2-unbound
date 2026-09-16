# Black Ops 2 Unbound HQ

This repository hosts the public manifests and release metadata used by the Black Ops 2
Unbound HQ Wii U application. Large packages are attached to versioned GitHub Releases and
are not committed to Git history.

## Current beta

`v0.0.01` is the first USA beta. Its core payload is split into two required assets because
GitHub limits each release asset to less than 2 GiB:

- `unbound-USA-0.0.01-update.zip`
- `unbound-USA-0.0.01-aoc.zip`

The HQ app verifies both SHA-256 hashes, installs UPDATE first and AOC second, and writes
`update/content/update.txt` only after both components succeed.

## Publishing order

1. Create a prerelease with tag `v0.0.01`.
2. Upload every file from the local `v0.0.01` distribution-staging directory.
3. Verify the uploaded asset names exactly match `release-drafts/v0.0.01/releases.txt`.
4. Copy `release-drafts/v0.0.01/releases.txt` over the root `releases.txt`, then commit and
   push that activation as the final step.

Publishing the manifest last prevents the Wii U application from discovering a release whose
assets are not online yet. Never replace a published asset in place; publish a new version with
new hashes.

## Repository files

- `releases.txt` — regional core-release URLs and SHA-256 values.
- `workshop.txt` — curated Workshop listing.
- `recent-changes.txt` — the HQ landing-page announcement.
- `about.txt` — project description for the future About screen.
- `manifests/` — auditable per-file release manifests and checksums.

Comments, votes, and user uploads will require a separate authenticated/moderated service.
GitHub remains the source for team-controlled static manifests and release downloads.
