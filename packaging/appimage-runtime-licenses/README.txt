License texts for libraries statically linked into the official AppImage
runtime. The runtime's own MIT notice is ../AppImage-runtime-LICENSE.txt.

libfuse 3.15.0: LICENSE and LGPL2.txt from the exact source archive pinned
by the runtime's scripts/common/install-dependencies.sh.
squashfuse 0.5.2: LICENSE from the exact source archive pinned there.
musl: COPYRIGHT from https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT?h=v1.2.5
zstd: LICENSE from https://github.com/facebook/zstd/blob/v1.5.6/LICENSE
zlib: LICENSE from https://github.com/madler/zlib/blob/v1.3.1/LICENSE
mimalloc: LICENSE from https://github.com/microsoft/mimalloc/blob/v2.1.7/LICENSE

The upstream runtime's build recipe selects Alpine 3.21 packages for musl,
zstd, zlib and mimalloc; it does not record their exact binary revisions.
The complete runtime source, libfuse source and patch, squashfuse source,
and the upstream build scripts are provided in the release Source archive.
These text copies preserve the library families' complete copyright and
license terms; they do not assert exact Alpine binary-package revisions.
