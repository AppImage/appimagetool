# appimagetool ![Downloads](https://img.shields.io/github/downloads/AppImage/appimagetool/total.svg) [![irc](https://img.shields.io/badge/IRC-%23AppImage%20on%20libera.chat-blue.svg)](https://web.libera.chat/#AppImage) [![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=ZT9CL8M5TJU72)

## Usage

`appimagetool` is used to generate an AppImage from an existing `AppDir`. Many community-provided higher-level [tools for deploying applications in AppImage format](https://github.com/AppImageCommunity/awesome-appimage/blob/main/README.md#appimage-developer-tools) use it internally. A precompiled version can be downloaded on [GitHub Releases](../..//releases), but in most cases you will be better off using one of the higher-level tools instead of using `appimagetool` directly.

Usage in a nutshell, assuming that you already have an [AppDir](https://github.com/AppImage/AppImageSpec/blob/master/draft.md#appdir) in place:

```
ARCH=x86_64 ./appimagetool-x86_64.AppImage some.AppDir
```

Detailed usage:
```
Usage:
  appimagetool [OPTION...] SOURCE [DESTINATION] - Generate AppImages from existing AppDirs

Help Options:
  -h, --help                  Show help options

Application Options:
  -l, --list                  List files in SOURCE AppImage
  -u, --updateinformation     Embed update information STRING; if zsyncmake is installed, generate zsync file
  -g, --guess                 Guess update information based on GitHub or GitLab environment variables
  --version                   Show version number
  -v, --verbose               Produce verbose output
  -s, --sign                  Sign with gpg[2]
  --comp                      Squashfs compression
  -n, --no-appstream          Do not check AppStream metadata
  --exclude-file              Uses given file as exclude file for mksquashfs, in addition to .appimageignore.
  --runtime-file              Runtime file to use
  --sign-key                  Key ID to use for gpg[2] signatures
  --sign-args                 Extra arguments to use when signing with gpg[2]
```

### Environment variables

Some of the parameters above can alternatively be specified as environment variables. Also, some additional environment variables are available, too.

- `ARCH`: Needs to be set whenever appimagetool cannot automatically determine the architecture of the binaries inside the AppDir to choose a suitable runtime (e.g., when binaries for multiple architectures or just shell scripts are contained in there).
- `APPIMAGETOOL_FORCE_SIGN`: By default, if signing fails, appimagetool just logs a warning but will not abort immediately. If this environment variable is set, appimagetool exits with a non-zero return code.
- `APPIMAGETOOL_SIGN_PASSPHRASE`: If the `--sign-key` is encrypted and requires a passphrase to be used for signing (and, for some reason, GnuPG cannot be used interactively, e.g., in a CI environment), this environment variable can be used to safely pass the key.
- `VERSION`: This value will be inserted by appimagetool into the root desktop file and (if the destination parameter is not provided by the user) in the output filename.

## Building

To build for various architectures on a local machine (or on GitHub Codespaces) using Docker:

* For 64 bit Intel, run `ARCH=x86_64 bash ./ci/build-in-docker.sh`
* For 32 bit Intel, run `ARCH=i686 bash ./ci/build-in-docker.sh`

If you are on an Intel machine and would like to cross-compile for ARM:

* Prepare the Docker system for cross-compiling with `docker run --rm --privileged multiarch/qemu-user-static --reset -p yes`, then run
* For 64 bit ARM, run `ARCH=aarch64 bash ./ci/build-in-docker.sh`
* For 32 bit ARM, run `ARCH=armhf bash ./ci/build-in-docker.sh`
