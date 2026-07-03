SplitCA ASE 2026 Artifact Status
================================

Requested badges
----------------

1. Artifacts Evaluated - Functional
2. Artifacts Evaluated - Reusable
3. Artifacts Available

Functional badge justification
------------------------------

The artifact is documented, consistent, complete, exercisable, and includes
verification evidence:

  - README.md provides a reviewer-oriented Getting Started guide and detailed
    reproduction instructions.
  - Dockerfile provides a containerized executable environment for the artifact.
  - artifact/smoke_test.sh builds the tool if needed, runs SplitCA on included
    test inputs, and validates the generated covering arrays with
    build/bin/check_tuples.
  - The smoke test checks known tuple counts:
      * 3-wise axtls: 916254
      * 4-wise axtls: 37505369
  - experimental_results/ contains the result tables used by the paper.

Reusable badge justification
----------------------------

The artifact is structured and documented for reuse beyond simply rerunning the
paper examples:

  - README.md documents the CLI options and gives direct commands for running
    SplitCA on CNF and d-DNNF inputs.
  - The source layout is described so future users can locate parsing,
    generation, optimization, and validation code.
  - Benchmarks and result tables are provided in standard CNF and CSV formats.
  - artifact/reproduce_results.sh supports smoke, reduced, table-check, and full
    modes, including controls for long-running experiments.
  - THIRD_PARTY_NOTICES.md identifies the third-party components and helper
    executables relevant to reuse.

Available badge justification
-----------------------------

The artifact is prepared for public archival release:

  - LICENSE provides open-source distribution terms for SplitCA.
  - artifact/make_archive.sh creates a clean source archive suitable for deposit.
  - The authors should deposit the archive, and preferably a prebuilt container
    image archive, in a persistent archival repository such as Zenodo, FigShare,
    or Software Heritage.
  - The DOI or archival link is intentionally not embedded here because it will
    be supplied through HotCRP and the archival repository metadata.

Known limitations
-----------------

  - The helper executables in bin/ are Linux x86_64 binaries, so the full
    executable artifact requires a Linux x86_64 container or VM.
  - Full timing reproduction depends on hardware, CPU count, OS scheduling, and
    benchmark selection. The artifact therefore provides a quick reduced review
    scope and a full command template for longer campaigns.
  - Competitor tools are represented in the CSV result tables. They are not
    bundled unless redistribution is permitted.
