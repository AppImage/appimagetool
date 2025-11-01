# Security and Supply Chain

This document describes the security measures and supply chain considerations for appimagetool.

## Compiler Security Flags

The project is built with comprehensive compiler warnings enabled to catch potential bugs and undefined behavior:

- `-Wall`: Enable all common warnings
- `-Wextra`: Enable extra warnings
- `-Wconversion`: Warn about implicit type conversions that may alter values
- `-Werror`: Treat warnings as errors to ensure they are addressed

These flags help ensure code quality and catch potential security issues at compile time.

## Download Verification

All external dependencies downloaded during the build process are verified using SHA256 hashes:

### Runtime Binaries

The AppImage runtime binaries are downloaded from https://github.com/AppImage/type2-runtime/releases and verified with SHA256 hashes for each architecture:

- `x86_64`: e70ffa9b69b211574d0917adc482dd66f25a0083427b5945783965d55b0b0a8b
- `i686`: 3138b9f0c7a1872cfaf0e32db87229904524bb08922032887b298b22aed16ea8
- `aarch64`: c1b2278cf0f42f5c603ab9a0fe43314ac2cbedf80b79a63eb77d3a79b42600c5
- `armhf`: 6704e63466fa53394eb9326076f6b923177e9eb48840b85acf1c65a07e1fcf2b

The build process prints the hash and size of the downloaded runtime for transparency.

### Build Tools

External build tools are also verified:

- **mksquashfs 4.6.1**: SHA256 hash `9c4974e07c61547dae14af4ed1f358b7d04618ae194e54d6be72ee126f0d2f53`
- **zsyncmake 0.6.2**: SHA256 hash `0b9d53433387aa4f04634a6c63a5efa8203070f2298af72a705f9be3dda65af2` (already verified)
- **desktop-file-validate 0.28**: SHA256 hash `30355df75de31a5c5a2e87fab197fcd77c0a8d1317e86e0dfe515eb0f94f29f8`

## Build Provenance Attestation

The GitHub Actions workflow generates cryptographically signed build provenance attestations using GitHub's attestation service. These attestations:

- Prove that the artifacts were built by the official GitHub Actions workflow
- Include the full build context (commit SHA, workflow, runner environment)
- Can be verified by downstream users using the GitHub CLI or API

To verify an AppImage artifact:

```bash
gh attestation verify appimagetool-x86_64.AppImage --owner AppImage
```

## Sanitizer Support

For development and testing, the build system supports AddressSanitizer (ASAN) and UndefinedBehaviorSanitizer (UBSAN):

```bash
cmake -DENABLE_SANITIZERS=ON /path/to/source
make
```

These sanitizers help detect:
- Memory errors (use-after-free, buffer overflows, memory leaks)
- Undefined behavior (integer overflow, null pointer dereferences, etc.)

Note: Sanitizers cannot be used with static builds and are intended for development/testing only.

## Updating Hashes

When updating dependencies, the hashes must be updated accordingly:

1. Download the new version of the dependency
2. Calculate its SHA256 hash: `sha256sum <file>`
3. Update the hash in the corresponding script in `ci/`
4. Document the change in the commit message

## Supply Chain Considerations

This project takes the following measures to ensure supply chain security:

1. **Pinned Dependencies**: All external dependencies are pinned to specific versions
2. **Hash Verification**: All downloads are verified against known-good SHA256 hashes
3. **Minimal Trust Surface**: Only downloads from official sources (GitHub releases, official package repositories)
4. **Transparency**: All hashes and versions are printed during the build process
5. **Reproducibility**: Static builds ensure consistent behavior across different systems
6. **Build Provenance**: GitHub attestations provide cryptographic proof of build authenticity

## Reporting Security Issues

If you discover a security vulnerability in appimagetool, please report it by opening an issue on GitHub. Please provide as much detail as possible to help us understand and address the issue.
