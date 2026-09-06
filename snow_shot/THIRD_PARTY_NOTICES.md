# Snow Shot Third-Party Notices

Snow Shot incorporates third-party software and assets under their respective
licenses. The release build generates a complete, versioned notice bundle from
the resolved Rust dependency graph, installed vcpkg packages, the audited
static Qt build, and repository-owned attribution files.

Installed releases place that bundle under:

```text
share/snow-shot/licenses/third-party/
```

`INDEX.md` in that directory records every collected package and source
license file. The bundle includes the Ant Design Icons MIT notice from
`ant_design_qt/THIRD_PARTY_NOTICES.md`.

Screen color restoration uses nalgebra (Apache-2.0) for fixed-size matrix
inversion and validation. Its license and resolved dependencies are included
in the generated Rust dependency notice bundle.

Local crash diagnostics use Crashpad (Apache-2.0), pinned by the vcpkg baseline
and the repository's Crashpad overlay. The client, helper, Chromium base code,
and their bundled notices are included in the vcpkg license collection. The
Qt-independent OCR bridge and shared Rust panic hook are Apache-2.0 code under
`snow-crates/`.

The generated bundle is authoritative for a particular binary because its
contents are produced from that build environment. Dependency licenses and
copyright notices remain the property of their respective owners.
