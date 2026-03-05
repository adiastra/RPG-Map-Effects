# Releasing RPG Map Effects

Releases are built by GitHub Actions when you push a **version tag**. A **draft** release is created with installable artifacts (macOS `.pkg`, Windows `.exe`/`.zip`, Ubuntu `.deb`, source tarballs). You then publish the draft from the GitHub Releases page.

## One-time setup

1. **Push the repo to GitHub**  
   If you develop from a local clone of `obs-studio`, push the plugin to its own repo (e.g. `https://github.com/adiastra/RPG-Map-Effects`).

2. **Optional: macOS code signing & notarization**  
   For signed and notarized macOS `.pkg` installers, add these [repository secrets](https://docs.github.com/en/actions/security-guides/encrypted-secrets):
   - `MACOS_SIGNING_APPLICATION_IDENTITY` – Developer ID Application
   - `MACOS_SIGNING_INSTALLER_IDENTITY` – Developer ID Installer
   - `MACOS_SIGNING_CERT`, `MACOS_SIGNING_CERT_PASSWORD`, `MACOS_KEYCHAIN_PASSWORD`
   - `MACOS_SIGNING_PROVISIONING_PROFILE` (if needed)
   - `MACOS_NOTARIZATION_USERNAME`, `MACOS_NOTARIZATION_PASSWORD` (Apple ID app-specific password)

   Without these, the workflow still runs and produces an **unsigned** macOS package; users may need to allow it in System Settings → Privacy & Security.

## Creating a release

1. **Commit and push everything**  
   All changes that should be in the release must be on the remote branch first. If you develop locally (e.g. inside an `obs-studio` clone), commit every change you want released, then push to `main` (or your default branch):
   ```bash
   git add -A
   git status   # check what will be committed
   git commit -m "Prepare release 1.0.0"
   git push origin main
   ```

2. **Set the version**  
   In `buildspec.json`, set `"version"` to the release version (e.g. `"1.0.0"`). If you did that in the commit above, you’re done; otherwise commit and push again.

3. **Create and push a tag**  
   Tag format must be **semver**: `X.Y.Z` or `X.Y.Z-beta1` / `X.Y.Z-rc1` (beta/rc create a *prerelease*).

   ```bash
   git tag -a 1.0.0 -m "Release 1.0.0"
   git push origin 1.0.0
   ```
   The tag must point to the commit you just pushed (the one with all your release changes).

4. **Wait for the workflow**  
   Go to **Actions** on GitHub. The **Push** workflow runs: it builds the plugin on macOS (universal), Windows (x64), and Ubuntu, then the **Create Release** job creates a **draft** release and attaches the built files.

5. **Publish the release**  
   - Open **Releases** → find the draft for your tag (e.g. **RPG Map Effects 1.0.0**).
   - Edit the draft: add release notes (what’s new, install instructions).
   - Click **Publish release**.

## Artifacts users get

This repo’s CI builds **macOS only**. Each release includes:

- **macOS:** `.pkg` installer and `.tar.xz` (universal: arm64 + x86_64).

Windows and Ubuntu builds are disabled in the workflow; you can re-enable them later in `.github/workflows/build-project.yaml` if you add support.

## Troubleshooting

- **Build fails on a platform**  
  Check the failing job in Actions (e.g. Xcode version on macOS, missing deps on Linux/Windows). Fix and push a new tag (e.g. `1.0.1`) or delete the tag, fix, and re-push the same tag (`git tag -d 1.0.0` then recreate and `git push origin 1.0.0 --force`).

- **Release draft is empty or missing artifacts**  
  The **Create Release** job only runs when the tag matches `X.Y.Z` or `X.Y.Z-beta*`/`X.Y.Z-rc*`, and it needs the build artifacts. Ensure all three build jobs (macOS, Ubuntu, Windows) succeeded.

- **macOS runner: Xcode version**  
  The workflow selects Xcode 16.1.0. If GitHub’s runner image changes, you may need to update the path in `.github/workflows/build-project.yaml` (step “Set Up Environment” in the macOS job).
