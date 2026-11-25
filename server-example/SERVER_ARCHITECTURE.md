# Future mod catalog, votes, and comments

The active Workshop is plugins-only. It reads the team-controlled static
`workshop-plugins.txt` catalog; `workshop.txt`, user mods, votes, comments, and uploads are not
reachable. The current catalog lists Unbound Mod Link, GamePad Mic Redirect, and Protarium.
HQ revision 28 enables Protarium installation and optional startup Auto-Update. Workshop displays
a saved or built-in listing immediately, refreshes the team catalog
only when the user presses **X**, and checks a publisher feed and installed files when an available
plugin's detail page opens. Protarium and GamePad Mic Redirect default to Auto-Update OFF when no
preference is saved, with no publisher-feed request for them at startup while OFF. Existing saved
ON preferences are preserved. Enabled plugin Auto-Updates remain synchronous startup work. External
publisher feeds control release bytes only within the exact Aroma paths authorized by the active
catalog.
When community mods are enabled in a later revision, use a versioned JSON catalog for mod release
metadata and keep votes/comments in a separate API. Static trusted install metadata can then be
cached aggressively without mixing user-generated content into its write authority.

## Suggested small deployment

- Cloudflare R2 (or any HTTPS object store/CDN): ZIP packages, screenshots, and `catalog.json`.
- Cloudflare Worker: read-only catalog proxy plus the vote/comment API.
- Cloudflare D1: mods, installations, votes, comments, and reports.
- A private admin page or CLI: moderation and publishing. Never put its secret in the WUHB.

The same shape also works with Node/Express, PostgreSQL, and S3-compatible storage.

## Recommended split

Keep these responsibilities separate:

```text
GitHub repository + Releases
  Universal Unbound core update-only ZIPs
  releases-chain-v2.txt (HQ app + game content), releases-update-only.txt, recent-changes.txt, about.txt
  workshop-plugins.txt plus team-curated plugin write allowlists
  Immutable team-reviewed plugin and future mod release assets

Cloudflare Worker + D1
  Public comments and vote API
  Aggregate counts, moderation state, reports, rate limits

Cloudflare R2
  Quarantined user-submitted ZIPs and preview images
  Approved community packages if GitHub Releases becomes cumbersome

Small browser-based submission/admin portal
  Account login, upload, required metadata, moderation and publishing
```

Do not let the Wii U upload arbitrary mod ZIPs directly in the first production version. A web
submission form is easier to secure, supports resumable uploads, and gives moderators a place to
inspect authorship and package contents. The console should download approved packages and send
only small vote, comment, and report requests.

## Initial Cloudflare setup

Create a TypeScript Worker, D1 database, and private R2 quarantine bucket:

```sh
npm create cloudflare@latest -- unbound-community-api
cd unbound-community-api
npx wrangler d1 create unbound-community
npx wrangler r2 bucket create unbound-submissions
```

Bind the D1 database as `DB` and the R2 bucket as `SUBMISSIONS` in `wrangler.jsonc`. Add random
Worker secrets for hashing installation IDs and authenticating the private moderation portal;
never compile either secret into the WUHB:

```sh
npx wrangler secret put INSTALLATION_HMAC_KEY
npx wrangler secret put MODERATION_SESSION_KEY
```

Create the tables below in `schema.sql`, apply locally while developing, and then apply remotely:

```sh
npx wrangler d1 execute unbound-community --local --file=./schema.sql
npx wrangler d1 execute unbound-community --remote --file=./schema.sql
npx wrangler deploy
```

Point a domain such as `community.unbound.example` at the Worker. A future WUHB should call only
HTTPS endpoints on that domain. Do not expose vote/comment controls until the API client, moderation,
and persistence are implemented.

## User-submitted package flow

1. The author signs in to the web submission portal and enters name, creator, description,
   package scope (`UNIVERSAL`), localized-path contract, and version.
2. The portal requests a short-lived presigned R2 `PUT` URL from the Worker and uploads directly
   to the private `unbound-submissions` bucket. This avoids proxying large ZIPs through a Worker.
3. The Worker creates a `pending` submission row containing the object key, claimed SHA-256,
   size, author account, and timestamps.
4. An isolated scanner validates the ZIP structure, rejects traversal/symlinks/executables that
   violate policy, calculates its own SHA-256, and produces an inspection report. Never extract
   an untrusted ZIP on the production Worker or moderator's everyday computer.
5. A moderator approves or rejects it. Approval copies the immutable object to a public bucket
   or attaches it to a GitHub Release, then creates the published catalog record.
6. The console sees only `published` records. Removing a mod hides it from new catalogs without
   silently changing an already published package/hash.

R2 presigned URLs are bearer credentials: limit them to one object key and `PUT`, use a short
expiry, enforce an expected size/checksum when possible, and never expose R2 account credentials.

## Catalog

Publish `catalog.json` atomically: upload packages first, verify their SHA-256 values, upload a
new catalog under a versioned name, and finally replace the small `latest.json` pointer. Keep
old packages available so consoles downloading an earlier catalog do not receive a 404.

Every package has an immutable URL, exact byte size, lowercase SHA-256, mod version, and
compatible game title IDs. Core payloads are universal: localized files use the `@language`
path component and the console selects the actual language directory. The app should reject
unknown schema versions and packages larger than their declared limits. The included
`catalog.example.json` demonstrates the proposed shape.

The frozen legacy `workshop.txt` format is not read by revision 11. If a later migration imports its
optional preview counts, treat them as untrusted display hints only—the API must be authoritative.

## API

All responses use UTF-8 JSON over HTTPS. A practical first version is:

```text
GET  /v1/mods/{modId}/reviews?cursor=...&limit=20
POST /v1/mods/{modId}/vote
POST /v1/mods/{modId}/reviews
POST /v1/reviews/{reviewId}/report
```

Vote request:

```json
{"installationId":"random-uuid","vote":"up","modVersion":"1.2.0"}
```

Review request:

```json
{
  "installationId":"random-uuid",
  "displayName":"Player",
  "body":"Worked well in local co-op.",
  "appVersion":"0.2.0-beta",
  "modVersion":"1.2.0"
}
```

Create the installation UUID randomly on first launch and store it under the app's SD folder.
Do not use the console serial, NNID, IP address, or another permanent hardware/account ID.
Store only a keyed hash of the UUID server-side. Use one row per `(mod_id, installation_hash)`
so a vote is changeable but cannot be multiplied by repeat requests.

The future app may prefill `displayName` from the console's current local Nintendo Network/Pretendo
account ID, but the name is unverified user-facing text. Never use it as
identity, authentication, vote ownership, or a moderation key; allow the user to replace it and
apply the same normalization and moderation rules as any other submitted display name.

`POST /vote` returns current aggregate counts and the caller's selected vote. Review creation
returns `202 Accepted` with a moderation status. `GET /reviews` returns only published reviews,
aggregate counts, a stable cursor, and whether more results exist.

## Minimal database tables

```text
mods(id, status, created_at)
mod_releases(id, mod_id, version, sku, url, sha256, byte_size, published_at)
votes(mod_id, installation_hash, value, updated_at)
reviews(id, mod_id, installation_hash, display_name, body, mod_version,
        status, created_at, updated_at)
reports(id, review_id, installation_hash, reason, created_at)
authors(id, provider, provider_subject, display_name, status, created_at)
submissions(id, author_id, object_key, claimed_sha256, verified_sha256, byte_size,
            metadata_json, status, scan_report, created_at, reviewed_at, reviewer_id)
```

Use foreign keys and unique indexes on mod IDs and vote ownership. Calculate public totals in
SQL or cache them briefly at the edge; never accept aggregate counts from a console.

## Abuse and moderation

- Limit reviews to roughly 500 characters and names to roughly 24 characters.
- Apply per-installation and coarse per-IP rate limits, but do not retain raw IPs longer than
  operationally necessary.
- Normalize UTF-8, reject control characters/URLs where appropriate, and escape text when shown.
- Queue new comments for moderation until the community is small and abuse controls are proven.
- Provide report, hide, and ban-hash tooling plus an audit log for moderator actions.
- Do not expose database credentials or a privileged API key in the app.

Because a Wii U cannot safely hold a shared client secret, the API must assume all client
requests are forgeable. Rate limits, unique constraints, moderation, and backups are the real
security boundary. Package integrity is separately enforced by the hashes in the catalog.
