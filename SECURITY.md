# Security and Supply Chain

This document describes the security measures and supply chain considerations for appimagetool.

## Compiler Security Flags

The project is built with comprehensive compiler warnings enabled to catch potential bugs and undefined behavior:

- `-Wall`: Enable all common warnings
- `-Wextra`: Enable extra warnings
- `-Wconversion`: Warn about implicit type conversions that may alter values
- `-Werror`: Treat warnings as errors to ensure they are addressed

These flags help ensure code quality and catch potential security issues at compile time.

### Future Enhancements

Additional static analysis tools that could be integrated:
- **scan-build** (Clang Static Analyzer): Performs code flow analysis to detect issues like null pointer dereferences and use-after-free
- **gcc -fanalyzer**: GCC's built-in static analyzer for similar code flow analysis

## Download Verification

External dependencies downloaded during the build process are verified where practical:

### Runtime Binaries

**Important Note**: The AppImage runtime binaries are downloaded from the `continuous` release at https://github.com/AppImage/type2-runtime/releases. 

**Current Limitation**: Hash verification for continuous releases is problematic because:
- Continuous releases are updated regularly
- Hard-coded hashes would break when type2-runtime is updated
- This creates a maintenance burden

**Current Approach**: The build process prints the SHA256 hash and size of the downloaded runtime for transparency and audit purposes, but does not enforce hash verification.

**Recommended Future Improvements**:
1. Use GPG signature verification (download `.sig` files and verify with GPG)
2. Switch to versioned/tagged releases instead of continuous
3. Implement automatic hash updates when type2-runtime changes

### Build Tools

External build tools use strict hash verification:

- **mksquashfs 4.6.1**: SHA256 hash `9c4974e07c61547dae14af4ed1f358b7d04618ae194e54d6be72ee126f0d2f53`
- **zsyncmake 0.6.2**: SHA256 hash `0b9d53433387aa4f04634a6c63a5efa8203070f2298af72a705f9be3dda65af2`
- **desktop-file-validate 0.28**: SHA256 hash `379ecbc1354d0c052188bdf5dbbc4a020088ad3f9cab54487a5852d1743a4f3b`

These are versioned dependencies where hash verification is practical and effective.

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

**Note**: Sanitizers cannot be used with static builds and are intended for development/testing only.

**Future Enhancement**: To be fully effective, sanitizer builds should be run in CI with both:
- The full application exercising real-world use cases
- Unit tests covering both happy paths and error handling paths

This would catch issues before they reach production.

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
