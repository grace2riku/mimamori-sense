/**
 * @file diag_config.h
 * @brief Build switch for verbose bring-up diagnostics (Issue #183)
 * @details
 * CPU0 internal flash is nearly exhausted. A large share of the remaining
 * headroom was consumed by string literals belonging to bring-up-era
 * diagnostic shell commands (register dumps, self-tests, benchmarks) that
 * are not needed during normal operation.
 *
 * Rather than deleting that code -- which would permanently cost us the
 * ability to triage hardware problems on a board -- the verbose diagnostics
 * are guarded by MIMAMORI_VERBOSE_DIAG and excluded from the default build.
 *
 * Enabling it again:
 *   - Temporarily: change the default below to 1 and rebuild.
 *   - Per-build:   add -DMIMAMORI_VERBOSE_DIAG=1 to the project's
 *                  C compiler "Preprocessor > Defined symbols" settings
 *                  (e2 studio: Project > Properties > C/C++ Build > Settings).
 *
 * Note that a verbose build will not fit alongside everything else once the
 * flash budget is tight; disable other features (or build with the AI model
 * relocated) when enabling this for debugging.
 *
 * What stays in the default build:
 *   - All status/inspection commands used for routine operation
 *     (`camera status`, `display status`, `sdram status`, `dave2d status`,
 *      `camera sensor`, `camera fb`, `camera phy`, ...)
 *   - All functional code paths (LCD, camera, AI inference, fall detection).
 *
 * What is excluded unless MIMAMORI_VERBOSE_DIAG is 1:
 *   - `camera diag`     : full MIPI data path register dump
 *   - `camera timing`   : D-PHY timing parameter dump
 *   - `camera test`     : capture/FPS/stream integration test suite
 *   - `dave2d test`     : GPU drawing self-tests
 *   - `dave2d bench`    : GPU vs CPU benchmark suite
 *   - `display test`    : GLCDC test pattern generator
 *   - `sdram test`      : SDRAM data/address bus and pattern tests
 *
 * A command excluded from the build still parses and responds; it reports
 * that the verbose build is required (see cmd_print_diag_disabled()), so the
 * shell never silently ignores a request.
 */

#ifndef DIAG_CONFIG_H
#define DIAG_CONFIG_H

/**
 * Verbose bring-up diagnostics build switch.
 *
 * 0 = excluded (default; saves internal flash)
 * 1 = built in
 *
 * Guarded by #ifndef so a -D on the command line takes precedence.
 */
#ifndef MIMAMORI_VERBOSE_DIAG
#define MIMAMORI_VERBOSE_DIAG   (0)
#endif

#endif /* DIAG_CONFIG_H */
