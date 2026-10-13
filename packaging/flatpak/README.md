# Flatpak packaging

App ID: `top.suhasdissa.QuickShareLinux`, runtime `org.kde.Platform//6.11`.

## Build and install locally

Run from the repository root:

```sh
flatpak install flathub org.kde.Platform//6.11 org.kde.Sdk//6.11
flatpak-builder --user --install --force-clean --disable-rofiles-fuse \
  --state-dir=/tmp/qs_flatpak_state \
  /tmp/qs_flatpak_build packaging/flatpak/top.suhasdissa.QuickShareLinux.yml
flatpak run top.suhasdissa.QuickShareLinux
```

Add `--repo=/tmp/qs_flatpak_repo` to also export to an OSTree repo. Lint with
`flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest packaging/flatpak/top.suhasdissa.QuickShareLinux.yml`.

## Modules

Built from pinned tarballs (sha256): abseil-cpp, protobuf, nlohmann_json, sdbus-cpp
(with `sdbus-c++-xml2cpp`). OpenSSL, libcurl, libsystemd and libbluetooth come from the KDE SDK.
The SDK's abseil is too old, so abseil is built here and protobuf uses it
(`protobuf_ABSL_PROVIDER=package`). When bumping, keep protobuf and abseil compatible.

## Flathub notes

- No network at build time; every source is pinned.
- The app source is `type: dir` for local builds. For Flathub, replace it with a `type: git`
  source (`url`, `tag`, `commit`) pointing at the release tag.
- The manifest must be named `top.suhasdissa.QuickShareLinux.yml` in the Flathub repo root, and
  the app ID must match the metainfo, desktop file and icon names.
- Review the `--system-talk-name`, `--own-name=org.kde.*` and `--allow=bluetooth` finish-args;
  each needs a justification in the submission PR (comments in the manifest explain them).
