# OAG OWNER D4 — Online Deployment Preparation

BY MR. AHMED GAMAL HOSNI — GEMI & OMAR

This is a **non-secret deployment scaffold** for the separate OAG OWNER D4 admin panel. The verified application source and compiled Windows/Linux binaries are delivered separately in the ChatGPT artifact `OAG_OWNER_D4_ONLINE_READY_PACKAGE.zip`. This GitHub branch does **not yet contain** the complete application source or firmware (the original firmware archive contains over ten thousand files).

## Important
- **No D4 firmware modification**. Original UF2 SHA-256: `737f3685358abdfe06dd1c0742ce982804622792d5284c5a5bd2552c117d026f`.
- Original D4 licence codes are signed using the existing OAG1 format and the original issuer private key, which must **never** be published or embedded in a release.
- PC/mobile game subscription covers the game's weapons as already implemented in D4.
- The admin can add/edit/delete records and issue signed renewal/revocation codes. **Deleting a subscriber from the database or issuing a revocation code does not remotely disable a device**; the recipient must enter the new signed code into their D4 device.
- This is a **public GitHub repository**: do not commit the encrypted owner vault, private keys, passwords, real .env, client information, issued codes, or private backup ZIP.
- This repository is for code hosting, **not** a live server. Internet access requires a real DNS name and an always-on host with HTTPS. No public admin URL is active just by creating this Git branch.

## Deploy
1. Download `OAG_OWNER_D4_ONLINE_FULL_SOURCE.zip` from the current ChatGPT deliverable.
2. Extract `OAG_OWNER_D4_ONLINE_SOURCE/` into this folder (keeping Dockerfile, Go files, templates, static files, and deployment files).
3. On a private VPS, install Docker + Compose, create a real DNS A record for `OAG_DOMAIN`, configure secure admin credentials in a private `.env`, and run `docker compose up -d --build`.
4. Scan the TOTP setup key into Google Authenticator (time-based 6-digit codes). Keep the secret off GitHub.
5. Open `https://<your-real-domain>` only after DNS, Caddy HTTPS, and the server are running.
6. Backup the owner_data Docker volume and export subscriber data regularly. Do not issue renewals from disconnected independent databases for the same device.

The complete detailed Arabic instructions are in `README_ONLINE_AR.md` inside the source ZIP. The private L4 key backup's public owner ID and verification key match D4, but decryption with the actual original private key has not yet been field-tested. The hardware and live VPS have not been field-tested either.
