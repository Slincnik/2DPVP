# Client CI and automatic updates

The client pipeline is defined in `.github/workflows/client.yml`.

- Pull requests and pushes to `main` build and test the Linux AppImage.
- A tag such as `v0.1.0` creates a GitHub Release.
- The release contains the AppImage, its SHA-256 checksum, and `latest.json`.
- `latest.json` is downloaded from the stable `latest` release URL.

The client checks the manifest at startup. If a newer semantic version is
available, the bundled `pvp_duel_updater` downloads the AppImage over HTTPS,
checks its SHA-256, replaces the current AppImage atomically, and starts the
new version.

Local builds have updates disabled unless `PVP_DUEL_UPDATE_MANIFEST_URL` is
provided. The release workflow embeds the repository's `latest.json` URL.

Create a release with:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Only stable releases are used by the updater. Pre-releases are not selected
by the `latest` GitHub release URL.
