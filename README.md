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
  -g, --guess                 Guess update information based on Travis CI or GitLab environment variables
  --bintray-user              Bintray user name
  --bintray-repo              Bintray repository
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

## Building

To build for various architectures on a local machine (or on GitHub Codespaces) using Docker:

* For 64 bit Intel, run `ARCH=x86_64 bash ./ci/build-in-docker.sh`
* For 32 bit Intel, run `ARCH=i686 bash ./ci/build-in-docker.sh`

If you are on an Intel machine and would like to cross-compile for ARM:

* Prepare the Docker system for cross-compiling with `docker run --rm --privileged multiarch/qemu-user-static --reset -p yes`, then run
* For 64 bit ARM, run `ARCH=aarch64 bash ./ci/build-in-docker.sh`
* For 32 bit ARM, run `ARCH=armhf bash ./ci/build-in-docker.sh`
