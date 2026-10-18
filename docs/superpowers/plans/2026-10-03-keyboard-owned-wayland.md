# Keyboard Owned Wayland Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement inline. User approved this design and requested continuing implementation.

**Goal:** Deliver the existing keyboard without LayerShellQt package dependencies.
**Architecture:** Bundle a narrowly scoped Qt Wayland shell-integration plugin. The desktop adapter configures the keyboard's reservation through a QWindow property; Qt retains rendering and input ownership.
**Tech Stack:** Qt6.8.0 private WaylandClient shell API, Wayland protocol scanner, DTK6, Fcitx5.
**Spec:** docs/superpowers/specs/2026-10-03-keyboard-owned-wayland.md

## Constraints

- Preserve all QML and input behavior, X11 path and 3-second idle reclamation.
- Remove both external LayerShellQt packages and ELF references; do not uninstall other applications' dependencies.
- Qt private ABI 6.8.0 must be declared; retain honest preview gates.

## Tasks

### 1. Artifact dependency guard
- [x] Implement a test-first executable package validator: reject old Depends/ELF references, require bundled shell plugin and Qt private ABI. Verify old package RED.
- [x] Integrate validator into clean build after artifact creation.

### 2. Native Wayland behavior
- [x] Add real KWin reservation/focus/show-hide/rebuild/Fn geometry regression. Baseline first exposed fixture GL-only maximize effect; corrected fixture based on backtrace.
- [x] Implement owned shell plugin: registry binding, layer_surface lifecycle, configure/ack/exposure, output selection, sizes and reservation property updates.
- [x] Remove LayerShellQt linkage and replace adapter calls with owned property configuration; preserve settings normal shell path.
- [x] Test with LayerShellQt physically absent from owned SDK session: plugin selection, real input, reservation and redraw. Run full CTest.

### 3. Final review and delivery
- [x] Independent code review of protocol/Qt lifecycle and fix substantive findings with tests.
- [x] Preserve prior artifacts; clean build/install/test with dependencies absent; commit/source archive match.
- [x] DDE X11 actual package tests, screenshots and three-run PSS/rebuild measurements; document limitations.
- [x] Produce new versioned deb in Downloads plus complete source/evidence bundle, checksums and clean worktree.

## Execution ledger

- Start: clean owned worktree at 017c3cd; existing SDK restarted. Only approved backend and packaging changes.
- Ruling: written design/plan record the user's existing approval; no extra approval loop. Cost: any unknown ABI issue is resolved by tests and documented rather than changing product scope.

- Artifact guard: old package RED (external dependency); plugin-selection test RED before implementation, GREEN after installation.
- Binding: own Qt template plugin (v3) + generated wlr and xdg descriptors; Qt scanner namespace→scope argument rename only. Dynamic properties bridge adapter reservation. Explicit private ABI constraints. Linker rejects undefined symbols.
- Tests: SDK physically lacks all LayerShellQt packages. Native reservation/Fn/rebuild/focus, settings xdg path and protocol ack-before-buffer passed. First complete 16-test run: test bodies passed, one shell wrapper failed because script edited while running. Source will remain frozen during authoritative clean 18-test build.
- Review: two substantive findings; two-output reservation RED (456 != 800), fixed explicit remap; closed/replaced role RED (3 roles != 2), fixed recovery lifetime. Both GREEN. Controlled protocol peer also verifies zero-dimension configure without modifying production APIs.
- Packaging: version 0.1.1; prototype consumer references updated to new filename. Repository 16/16 tests pass. Prior artifacts preserved before clean build; actual package/VM results pending.

- Final: source 8146d98; clean build 18/18 CTest, 16/16 repository unittest. Deb dependency guard passes all 6 ELF files; no LayerShellQt in build package inventory. Host apt simulation adds only keyboard.
- VM: final deb upgraded old package; DDE X11 14 pass/0 fail/10 environment skips. Three-run PSS input40.43/hidden23.61MiB; 12 rebuild frames median79.3ms. All11 screenshots exactly equal previous package. All55 archive files match source commit.
- Packaging: 0.1.1 is415296bytes, Installed-Size2088KiB; SHA6758bd5ff669f6cf2d6977412718279af8eb3f935bd98d3a0e1eaf68dcdc0756. Report/docs/evidence bundle completed; preserve local branch/worktree for user deb testing, no merge/push requested.
