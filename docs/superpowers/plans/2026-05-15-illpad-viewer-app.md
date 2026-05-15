# ILLPAD Viewer JUCE App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone macOS desktop app (JUCE 8) that monitors an ILLPAD48 V2 via USB serial — unified runtime dashboard (8 pot parameters per bank, ALL BANKS state, events log) and embedded VT100 setup mode (replaces `vt100_serial_terminal.py` + iTerm2). Auto-mode-switching, scale-to-fit window, always-on-top, no firmware control beyond setup key input.

**Architecture:** 4 layers (SerialReader thread → Parsers → Model → JUCE View). Two co-existing parsers gated by a ModeDetector state machine. Bidirectional serial via libserialport (sigrok) with event-driven `sp_wait` for low-latency setup keyboard nav. VT100 emulation via libvterm (neovim). Single IBM Plex Mono font everywhere. Dark theme fixed. Aspect ratio fixed 3:2 with scale-to-fit on resize.

**Tech Stack:** JUCE 8, C++17, CMake, libserialport (sigrok) vendored as static lib, libvterm (neovim) vendored as static lib, Catch2 v3 for Layer 1 unit tests, IBM Plex Mono (OFL) embedded via JUCE BinaryData. Targets macOS arm64.

**Spec reference:** [`docs/superpowers/specs/2026-05-15-illpad-viewer-design.md`](../specs/2026-05-15-illpad-viewer-design.md)

**Firmware dependency:** Requires the API extensions from [`2026-05-15-illpad-firmware-viewer-api.md`](2026-05-15-illpad-firmware-viewer-api.md) to be uploaded on the connected ILLPAD. Independent build, but smoke-test integration requires both.

**Implementation discipline — anti-hallucination on JUCE / external libs**

This plan has been cross-audited 2026-05-15 against:
- JUCE 8.0.12 source tree at
  `/Users/loic/Code/PROJECTS/CirclePI/cpp/mac/external/JUCE/modules/`
- libserialport master at `https://github.com/sigrokproject/libserialport`
- libvterm master at `https://github.com/neovim/libvterm`

Every API call in the snippets below has been verified against these sources
(method exists, exact signature, ownership semantics). When implementing,
keep this discipline alive: **before adding a JUCE / libserialport / libvterm
call that is NOT in this plan**, open the corresponding header
(`juce_X.h:NNN`, `libserialport.h:NNN`, `vterm.h:NNN`) and verify the
signature. Cite the source as a one-line comment at the call site, e.g.:

```cpp
// juce_Font.h:65 — Font(FontOptions) is the only non-deprecated ctor in JUCE 8.
auto f = juce::Font(juce::FontOptions("IBM Plex Mono", 14.0f, juce::Font::bold));
```

This convention matches the discipline used in the CirclePI project (cf.
`CirclePI/CLAUDE.md` § "JUCE — discipline de lecture API"). It costs ~5
seconds per call site and prevents hallucinations / silent rotation by future
JUCE versions.

---

## File map

```
ILLPADViewer/
├── CMakeLists.txt                      Root build
├── cmake/                              Find scripts for vendored libs
│   ├── libserialport.cmake
│   └── libvterm.cmake
├── vendor/
│   ├── libserialport/                  Source vendored (git submodule or source-tree)
│   └── libvterm/                       Source vendored
├── Source/
│   ├── Main.cpp                        JUCE entry, JUCEApplication subclass
│   ├── MainComponent.{h,cpp}           Root component, hosts ModeSwitcher
│   ├── AppLookAndFeel.{h,cpp}          Dark theme + Plex Mono setup
│   │
│   ├── ui/
│   │   ├── ModeSwitcher.{h,cpp}        Switch between RuntimeMode and SetupMode views
│   │   ├── runtime/
│   │   │   ├── RuntimeMode.{h,cpp}     Root of runtime UI
│   │   │   ├── HeaderBar.{h,cpp}
│   │   │   ├── TransportBar.{h,cpp}    With PANIC flash
│   │   │   ├── CurrentBankPanel.{h,cpp}
│   │   │   ├── PotCell.{h,cpp}
│   │   │   ├── AllBanksPanel.{h,cpp}
│   │   │   ├── BankRow.{h,cpp}         With Play/Stop tick
│   │   │   └── EventLogPanel.{h,cpp}
│   │   └── setup/
│   │       ├── SetupMode.{h,cpp}       Root of setup UI
│   │       ├── TerminalView.{h,cpp}    Renders VTerm screen state
│   │       └── TerminalKeyMap.{h,cpp}  KeyPress → escape sequence
│   │
│   ├── model/
│   │   ├── Model.{h,cpp}               Aggregator + Listener notification
│   │   ├── DeviceState.{h,cpp}
│   │   ├── BankInfo.{h,cpp}
│   │   ├── CurrentBankState.{h,cpp}
│   │   ├── PotSlot.{h,cpp}
│   │   ├── EventBuffer.{h,cpp}
│   │   ├── TerminalModel.{h,cpp}       Wraps libvterm state
│   │   └── ModeDetector.{h,cpp}        UNKNOWN / RUNTIME / SETUP state machine
│   │
│   ├── serial/
│   │   ├── SerialPortDiscovery.{h,cpp} VID/PID auto-detect via libserialport
│   │   ├── SerialReader.{h,cpp}        Thread + sp_wait loop
│   │   ├── OutputQueue.{h,cpp}         Thread-safe write queue
│   │   ├── LineSplitter.{h,cpp}        bytes → \n-delimited lines
│   │   ├── RuntimeParser.{h,cpp}       Line → ParsedEvent variant
│   │   ├── TerminalDriver.{h,cpp}      bytes → vterm_input_write
│   │   ├── ITermSeqInterceptor.{h,cpp} Pre-parses DEC 2026 + CSI 8 t + OSC 1337
│   │   └── Commands.{h,cpp}            ?STATE / ?BANKS / ?BOTH helpers
│   │
│   └── enums/
│       ├── BankType.{h,cpp}
│       ├── PotTarget.{h,cpp}
│       └── ClockSource.{h,cpp}
│
├── Resources/
│   └── Fonts/
│       ├── IBMPlexMono-Regular.ttf
│       ├── IBMPlexMono-Medium.ttf
│       └── IBMPlexMono-Bold.ttf
│
└── Tests/
    ├── CMakeLists.txt
    ├── test_RuntimeParser.cpp
    ├── test_ModeDetector.cpp
    └── test_ITermSeqInterceptor.cpp
```

---

## Phase 0 — Foundation

### Task 1: CMake skeleton + JUCE 8

**Files:**
- Create: `CMakeLists.txt`
- Create: `Source/Main.cpp`
- Create: `Source/MainComponent.h`, `Source/MainComponent.cpp`

- [ ] **Step 1: Create root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(ILLPADViewer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

add_subdirectory(vendor/JUCE)

juce_add_gui_app(ILLPADViewer
    PRODUCT_NAME "ILLPAD Viewer"
    BUNDLE_ID com.loiclachaize.illpadviewer
    VERSION 1.0.0
    COMPANY_NAME "Loic Lachaize"
)

juce_generate_juce_header(ILLPADViewer)

target_sources(ILLPADViewer PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
)

target_compile_definitions(ILLPADViewer PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CAMERA=0
)

target_link_libraries(ILLPADViewer PRIVATE
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
```

- [ ] **Step 2: Add JUCE 8 as git submodule**

```bash
git submodule add https://github.com/juce-framework/JUCE.git vendor/JUCE
cd vendor/JUCE && git checkout 8.0.0 && cd ../..
```

- [ ] **Step 3: Minimal `Source/Main.cpp`**

```cpp
#include <JuceHeader.h>
#include "MainComponent.h"

class ILLPADViewerApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "ILLPAD Viewer"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override {
        mainWindow = std::make_unique<MainWindow>("ILLPAD Viewer", new MainComponent(), *this);
    }
    void shutdown() override { mainWindow = nullptr; }

    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(juce::String name, juce::Component* c, JUCEApplication& app)
          : DocumentWindow(name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons),
            ownerApp(app)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(c, true);
            setResizable(true, true);
            centreWithSize(900, 600);
            setVisible(true);
            setAlwaysOnTop(true);
        }
        void closeButtonPressed() override { ownerApp.systemRequestedQuit(); }
    private:
        JUCEApplication& ownerApp;
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ILLPADViewerApp)
```

- [ ] **Step 4: Minimal `Source/MainComponent.{h,cpp}`**

`MainComponent.h`:
```cpp
#pragma once
#include <JuceHeader.h>

class MainComponent : public juce::Component {
public:
    MainComponent();
    void paint(juce::Graphics&) override;
    void resized() override;
};
```

`MainComponent.cpp`:
```cpp
#include "MainComponent.h"

MainComponent::MainComponent() {
    setSize(900, 600);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromString("FF0D1117"));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("ILLPAD Viewer — foundation", getLocalBounds(), juce::Justification::centred);
}

void MainComponent::resized() {}
```

- [ ] **Step 5: Configure + build**

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Expected: build succeeds, produces `build/ILLPADViewer_artefacts/ILLPADViewer.app`.

- [ ] **Step 6: Run and verify**

```bash
open build/ILLPADViewer_artefacts/ILLPADViewer.app
```

Expected: window 900×600 with dark BG, "ILLPAD Viewer — foundation" text. Always-on-top works (open above other windows). Close → app quits.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt Source/Main.cpp Source/MainComponent.h Source/MainComponent.cpp .gitmodules vendor/JUCE
git commit -m "feat(viewer): CMake skeleton + JUCE 8 foundation"
```

---

### Task 2: Embed IBM Plex Mono + AppLookAndFeel

**Files:**
- Create: `Resources/Fonts/IBMPlexMono-Regular.ttf`, `Medium.ttf`, `Bold.ttf` (download from IBM Plex GitHub)
- Create: `Source/AppLookAndFeel.h`, `Source/AppLookAndFeel.cpp`
- Modify: `CMakeLists.txt` (add BinaryData)
- Modify: `Source/Main.cpp` (set LookAndFeel)

- [ ] **Step 1: Download IBM Plex Mono ttf files**

```bash
mkdir -p Resources/Fonts
cd Resources/Fonts
curl -LO https://github.com/IBM/plex/raw/master/IBM-Plex-Mono/fonts/complete/ttf/IBMPlexMono-Regular.ttf
curl -LO https://github.com/IBM/plex/raw/master/IBM-Plex-Mono/fonts/complete/ttf/IBMPlexMono-Medium.ttf
curl -LO https://github.com/IBM/plex/raw/master/IBM-Plex-Mono/fonts/complete/ttf/IBMPlexMono-Bold.ttf
cd ../..
```

- [ ] **Step 2: Add BinaryData target to `CMakeLists.txt`**

Insert after `juce_generate_juce_header`:

```cmake
juce_add_binary_data(ILLPADViewerData SOURCES
    Resources/Fonts/IBMPlexMono-Regular.ttf
    Resources/Fonts/IBMPlexMono-Medium.ttf
    Resources/Fonts/IBMPlexMono-Bold.ttf
)
```

And add `ILLPADViewerData` to `target_link_libraries`:

```cmake
target_link_libraries(ILLPADViewer PRIVATE
    ILLPADViewerData
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
```

- [ ] **Step 3: Create `Source/AppLookAndFeel.{h,cpp}`**

`AppLookAndFeel.h`:
```cpp
#pragma once
#include <JuceHeader.h>

class AppLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AppLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    // Color palette (cf. spec §24-§28)
    static constexpr juce::uint32 BG_PRIMARY   = 0xFF0D1117;
    static constexpr juce::uint32 BG_SECONDARY = 0xFF161B22;
    static constexpr juce::uint32 BG_TERTIARY  = 0xFF21262D;
    static constexpr juce::uint32 TEXT_PRIMARY = 0xFFE6EDF3;
    static constexpr juce::uint32 TEXT_SECONDARY = 0xFF8B949E;
    static constexpr juce::uint32 BORDER       = 0xFF30363D;
    static constexpr juce::uint32 ACCENT       = 0xFF58A6FF;

    static constexpr juce::uint32 TYPE_NORMAL    = 0xFFF5DEB3;
    static constexpr juce::uint32 TYPE_ARPEG     = 0xFF7DD3FC;
    static constexpr juce::uint32 TYPE_ARPEG_GEN = 0xFF3B82F6;
    static constexpr juce::uint32 TYPE_LOOP      = 0xFFF59E0B;

    static constexpr juce::uint32 STATUS_OK      = 0xFF22C55E;
    static constexpr juce::uint32 STATUS_WARN    = 0xFFF59E0B;
    static constexpr juce::uint32 STATUS_ERROR   = 0xFFEF4444;
    static constexpr juce::uint32 STATUS_STOP    = 0xFF6B7280;

private:
    juce::Typeface::Ptr plexRegular;
    juce::Typeface::Ptr plexMedium;
    juce::Typeface::Ptr plexBold;
};
```

`AppLookAndFeel.cpp`:
```cpp
#include "AppLookAndFeel.h"
#include "BinaryData.h"

AppLookAndFeel::AppLookAndFeel() {
    plexRegular = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    plexMedium = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
    plexBold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::IBMPlexMonoBold_ttf, BinaryData::IBMPlexMonoBold_ttfSize);

    setDefaultSansSerifTypeface(plexRegular);
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(BG_PRIMARY));
}

juce::Typeface::Ptr AppLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    if (font.isBold()) return plexBold;
    if (font.getTypefaceStyle() == "Medium") return plexMedium;
    return plexRegular;
}
```

**Note on `juce::Font` construction throughout this plan** — every font in the
runtime UI is built via `juce::Font(juce::FontOptions(name, size, style))`,
NOT the historical `juce::Font(name, size, style)` ctor. In JUCE 8 the latter
is marked `[[deprecated ("Use the constructor that takes a FontOptions
argument")]]` at `modules/juce_graphics/fonts/juce_Font.h:88`. Compiling
against the deprecated ctor would emit a warning at every call site, breaking
the per-task "zero new warnings" pass criterion. `FontOptions` wraps the same
`(name, size, style)` legacy args without deprecation
(`modules/juce_graphics/fonts/juce_FontOptions.h:76`). Cross-audit
2026-05-15.

- [ ] **Step 4: Install LookAndFeel in `Main.cpp`**

In `ILLPADViewerApp::initialise`, before creating the window:

```cpp
#include "AppLookAndFeel.h"
// ...
void initialise (const juce::String&) override {
    laf = std::make_unique<AppLookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel(laf.get());
    mainWindow = std::make_unique<MainWindow>("ILLPAD Viewer", new MainComponent(), *this);
}
// ...
private:
    std::unique_ptr<AppLookAndFeel> laf;
```

- [ ] **Step 5: Build, run, verify**

```bash
cmake --build build && open build/ILLPADViewer_artefacts/ILLPADViewer.app
```

Expected: same window, but background now `#0D1117`, text uses IBM Plex Mono. Resize window — text re-renders properly.

- [ ] **Step 6: Commit**

```bash
git add Resources Source/AppLookAndFeel.h Source/AppLookAndFeel.cpp Source/Main.cpp CMakeLists.txt
git commit -m "feat(viewer): embed IBM Plex Mono + dark theme palette"
```

---

### Task 3: Vendor libserialport and libvterm

**Files:**
- Create: `cmake/libserialport.cmake`
- Create: `cmake/libvterm.cmake`
- Add: `vendor/libserialport/` (git submodule)
- Add: `vendor/libvterm/` (git submodule or sourcepull)

- [ ] **Step 1: Add libserialport as git submodule**

```bash
git submodule add https://github.com/sigrokproject/libserialport.git vendor/libserialport
```

Note: the canonical upstream is `git://sigrok.org/libserialport` but the GitHub
mirror is HTTPS-reachable on restricted networks. Both contain the same
master.

- [ ] **Step 2: Add libvterm as git submodule**

```bash
git submodule add https://github.com/neovim/libvterm vendor/libvterm
```

- [ ] **Step 3: Create `cmake/libserialport.cmake`**

libserialport historically builds via autotools. The 3 source files used on
macOS (`serialport.c`, `macosx.c`, `timing.c`) all include `<config.h>` and
use the `SP_PRIV`/`SP_API` macros normally generated by `configure`. We
avoid pulling autotools into the build by emitting a 14-line stub `config.h`
at CMake configure time and putting it on the PRIVATE include path. This
approach was compile-tested 2026-05-15 against
`https://github.com/sigrokproject/libserialport` master (3 objects produced
under `clang -Wall -Wextra`).

```cmake
# vendor libserialport — sources built as a static library, no autotools.
# Generate a minimal config.h replacing what `./configure` would produce.
# Verified to compile serialport.c + macosx.c + timing.c on macOS clang
# (-Wall -Wextra). Cross-audit 2026-05-15 finding B1.
set(SP_STUB_DIR ${CMAKE_BINARY_DIR}/libserialport_stub)
file(WRITE ${SP_STUB_DIR}/config.h
"/* Stub config.h for libserialport — macOS, no autotools.
 * Replaces values normally injected by configure.ac / config-header.py.
 * SP_PRIV / SP_API : visibility macros (cf. libserialport_internal.h:278+).
 * HAVE_* : feature flags consumed by serialport.c / macosx.c. */
#define SP_API   __attribute__((visibility(\"default\")))
#define SP_PRIV  __attribute__((visibility(\"hidden\")))
#define SP_PACKAGE_VERSION_MAJOR 0
#define SP_PACKAGE_VERSION_MINOR 1
#define SP_PACKAGE_VERSION_MICRO 1
#define SP_PACKAGE_VERSION_STRING \"0.1.1\"
#define SP_LIB_VERSION_CURRENT    1
#define SP_LIB_VERSION_REVISION   0
#define SP_LIB_VERSION_AGE        1
#define SP_LIB_VERSION_STRING     \"1:0:1\"
#define HAVE_REALPATH    1
#define HAVE_FLOCK       1
#define HAVE_SYS_FILE_H  1
")

add_library(libserialport STATIC
    ${CMAKE_SOURCE_DIR}/vendor/libserialport/serialport.c
    ${CMAKE_SOURCE_DIR}/vendor/libserialport/macosx.c
    ${CMAKE_SOURCE_DIR}/vendor/libserialport/timing.c
)
target_include_directories(libserialport
    PUBLIC  ${CMAKE_SOURCE_DIR}/vendor/libserialport  # for libserialport.h
    PRIVATE ${SP_STUB_DIR}                            # for config.h
)
target_compile_definitions(libserialport PRIVATE
    LIBSERIALPORT_ATBUILD=1   # enables `#include <config.h>` path in
                              # libserialport_internal.h:40-43
)
if(APPLE)
    target_link_libraries(libserialport PUBLIC
        "-framework IOKit"          # IOServiceMatching, kIOMainPortDefault,
                                    # IOIteratorNext (macosx.c)
        "-framework CoreFoundation" # CFStringGetCString, kCFAllocatorDefault,
                                    # kCFStringEncodingASCII (macosx.c)
    )
endif()
```

If a future libserialport revision adds new HAVE_* checks or new translation
units that include `<config.h>`, extend the stub above. The full set of
HAVE_* macros consumed by the codebase can be re-derived with
`grep -hE "^#if(def| .*defined)?\\s*HAVE_" vendor/libserialport/*.c
vendor/libserialport/*.h | sort -u` and matched against `configure.ac`
`AC_CHECK_FUNC`/`AC_CHECK_HEADER` lines.

- [ ] **Step 4: Create `cmake/libvterm.cmake`**

Verified 2026-05-15 against `https://github.com/neovim/libvterm` master:
all 9 `.c` listed below exist in `src/`, and the `.inc` files they pull in
(`encoding/DECdrawing.inc`, `encoding/uk.inc`, `fullwidth.inc`) are
committed as artefacts — no Perl regeneration needed at build time. The
PRIVATE `src/` include path is required because `encoding.c:207-208`
`#include "encoding/DECdrawing.inc"` and `unicode.c:299`
`#include "fullwidth.inc"` resolve relative to `src/`.

```cmake
add_library(libvterm STATIC
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/encoding.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/keyboard.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/mouse.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/parser.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/pen.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/screen.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/state.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/unicode.c
    ${CMAKE_SOURCE_DIR}/vendor/libvterm/src/vterm.c
)
target_include_directories(libvterm
    PUBLIC  ${CMAKE_SOURCE_DIR}/vendor/libvterm/include  # vterm.h, vterm_keycodes.h
    PRIVATE ${CMAKE_SOURCE_DIR}/vendor/libvterm/src      # vterm_internal.h, .inc files
)
```

- [ ] **Step 5: Include cmake files and link in `CMakeLists.txt`**

```cmake
include(cmake/libserialport.cmake)
include(cmake/libvterm.cmake)

target_link_libraries(ILLPADViewer PRIVATE
    libserialport
    libvterm
    ILLPADViewerData
    juce::juce_gui_extra
    ...
)
```

- [ ] **Step 6: Verify build links both libs**

```bash
cmake --build build
```

Expected: build succeeds. The libserialport `config.h` gotcha is already
handled by Step 3 (stub written at configure time). If the build fails at
this step despite the stub being in place, run
`grep -hE "^#if(def| .*defined)?\\s*HAVE_" vendor/libserialport/*.c |
sort -u` and confirm every macro listed has an entry in the stub — a fresh
libserialport pull may introduce a new `HAVE_*` check.

- [ ] **Step 7: Commit**

```bash
git add cmake/ CMakeLists.txt .gitmodules vendor/libserialport vendor/libvterm
git commit -m "feat(viewer): vendor libserialport and libvterm as static libs"
```

---

## Phase 1 — Serial layer (Layer 0)

### Task 4: `SerialPortDiscovery` — list ports + VID/PID query

**Files:**
- Create: `Source/serial/SerialPortDiscovery.h`
- Create: `Source/serial/SerialPortDiscovery.cpp`

- [ ] **Step 1: Define interface**

`SerialPortDiscovery.h`:
```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>

struct SerialPortInfo {
    std::string name;          // /dev/cu.usbmodem...
    std::string description;   // Human-readable name
    std::optional<int> vid;    // USB Vendor ID, if available
    std::optional<int> pid;    // USB Product ID, if available
    bool isEspressifILLPAD() const {
        return vid && *vid == 0x303A; // ESP32-S3
    }
};

class SerialPortDiscovery {
public:
    static std::vector<SerialPortInfo> listAvailable();
    static std::optional<SerialPortInfo> autoDetectILLPAD(const std::vector<SerialPortInfo>& ports);
};
```

- [ ] **Step 2: Implement in `.cpp`**

```cpp
#include "SerialPortDiscovery.h"
#include <libserialport.h>

std::vector<SerialPortInfo> SerialPortDiscovery::listAvailable() {
    std::vector<SerialPortInfo> result;
    struct sp_port** portList = nullptr;
    if (sp_list_ports(&portList) != SP_OK) return result;

    for (int i = 0; portList[i] != nullptr; i++) {
        SerialPortInfo info;
        info.name = sp_get_port_name(portList[i]);
        const char* desc = sp_get_port_description(portList[i]);
        info.description = desc ? desc : "";

        int vid = 0, pid = 0;
        if (sp_get_port_usb_vid_pid(portList[i], &vid, &pid) == SP_OK) {
            info.vid = vid;
            info.pid = pid;
        }
        result.push_back(info);
    }
    sp_free_port_list(portList);
    return result;
}

std::optional<SerialPortInfo> SerialPortDiscovery::autoDetectILLPAD(const std::vector<SerialPortInfo>& ports) {
    for (const auto& p : ports) {
        if (p.isEspressifILLPAD()) return p;
    }
    return std::nullopt;
}
```

- [ ] **Step 3: Quick test main** (throwaway diagnostic, will remove)

In `MainComponent::MainComponent()`, temporarily add:
```cpp
auto ports = SerialPortDiscovery::listAvailable();
juce::String text;
for (const auto& p : ports) {
    text += p.name + " (vid=" + juce::String::toHexString((int)p.vid.value_or(0)) +
            " pid=" + juce::String::toHexString((int)p.pid.value_or(0)) + ")\n";
}
DBG(text);
```

- [ ] **Step 4: Build, run, verify**

```bash
cmake --build build && open build/ILLPADViewer_artefacts/ILLPADViewer.app
```

Console (visible via Xcode console or `Console.app`): expect to see at least one `/dev/cu....` entry. If ILLPAD plugged: should see `vid=303A`.

- [ ] **Step 5: Remove diagnostic + commit**

```bash
git add Source/serial CMakeLists.txt
git commit -m "feat(viewer): SerialPortDiscovery (list + auto-detect ESP32 VID/PID)"
```

(Add `Source/serial/*.cpp` to `target_sources` in CMakeLists.txt.)

---

### Task 5: `SerialReader` — thread + `sp_wait` read loop with manual port

**Files:**
- Create: `Source/serial/SerialReader.h`
- Create: `Source/serial/SerialReader.cpp`

- [ ] **Step 1: Define interface**

`SerialReader.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include <libserialport.h>
#include <atomic>
#include <functional>
#include <string>

class SerialReader : public juce::Thread {
public:
    using BytesCallback = std::function<void(const std::vector<uint8_t>&)>;

    SerialReader();
    ~SerialReader() override;

    void setBytesCallback(BytesCallback cb) { bytesCb = std::move(cb); }

    void connect(const std::string& portName);  // async-safe (sets target)
    void disconnect();                           // graceful

    bool isConnected() const { return connected.load(); }
    std::string getCurrentPortName() const;

    void run() override;

private:
    enum class State { Disconnected, Connecting, Connected, Reconnecting };

    std::atomic<State> state{State::Disconnected};
    std::atomic<bool>  connected{false};
    std::string        portTarget;
    juce::CriticalSection portLock;

    sp_port* port = nullptr;
    BytesCallback bytesCb;

    bool openPort(const std::string& name);
    void closePort();
};
```

- [ ] **Step 2: Implement in `.cpp`**

```cpp
#include "SerialReader.h"

SerialReader::SerialReader() : juce::Thread("ILLPADSerialReader") {}

SerialReader::~SerialReader() {
    stopThread(2000);
    closePort();
}

void SerialReader::connect(const std::string& portName) {
    {
        juce::ScopedLock lock(portLock);
        portTarget = portName;
    }
    if (!isThreadRunning()) startThread();
}

void SerialReader::disconnect() {
    {
        juce::ScopedLock lock(portLock);
        portTarget.clear();
    }
    closePort();
    connected.store(false);
}

std::string SerialReader::getCurrentPortName() const {
    juce::ScopedLock lock(portLock);
    return portTarget;
}

bool SerialReader::openPort(const std::string& name) {
    if (sp_get_port_by_name(name.c_str(), &port) != SP_OK) return false;
    if (sp_open(port, SP_MODE_READ_WRITE) != SP_OK) { sp_free_port(port); port = nullptr; return false; }
    sp_set_baudrate(port, 115200);
    sp_set_bits(port, 8);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_stopbits(port, 1);
    sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);
    return true;
}

void SerialReader::closePort() {
    if (port) { sp_close(port); sp_free_port(port); port = nullptr; }
}

void SerialReader::run() {
    uint8_t buf[1024];
    while (!threadShouldExit()) {
        std::string target;
        {
            juce::ScopedLock lock(portLock);
            target = portTarget;
        }

        if (target.empty()) { juce::Thread::sleep(200); continue; }

        if (!port && !openPort(target)) {
            connected.store(false);
            juce::Thread::sleep(1000);  // retry
            continue;
        }

        connected.store(true);

        // event-driven wait
        sp_event_set* evts = nullptr;
        sp_new_event_set(&evts);
        sp_add_port_events(evts, port, SP_EVENT_RX_READY);
        sp_wait(evts, 100);  // 100ms timeout
        sp_free_event_set(evts);

        int n = sp_nonblocking_read(port, buf, sizeof(buf));
        if (n > 0 && bytesCb) {
            std::vector<uint8_t> v(buf, buf + n);
            bytesCb(v);
        } else if (n < 0) {
            // I/O error -> reconnect
            closePort();
            connected.store(false);
            juce::Thread::sleep(500);
        }
    }
    closePort();
    connected.store(false);
}
```

- [ ] **Step 3: Wire to MainComponent for manual test**

In `MainComponent`:
```cpp
SerialReader reader;
// ... in ctor
reader.setBytesCallback([](const std::vector<uint8_t>& bytes) {
    juce::String s; for (auto b : bytes) s += (juce::juce_wchar)b;
    DBG(s);
});
// later, with a hardcoded port for now:
reader.connect("/dev/cu.usbmodem1101");  // adapt to actual port
```

- [ ] **Step 4: Build, plug ILLPAD, run**

```bash
cmake --build build && open build/ILLPADViewer_artefacts/ILLPADViewer.app
```

Expected: console shows incoming bytes including `[INIT]`, `[BANKS]`, `[STATE]`, `[POT]` lines from firmware.

- [ ] **Step 5: Commit**

```bash
git add Source/serial/SerialReader.h Source/serial/SerialReader.cpp Source/MainComponent.cpp
git commit -m "feat(viewer): SerialReader thread + sp_wait event-driven read loop"
```

---

### Task 6: `OutputQueue` + bidirectional writes

**Files:**
- Create: `Source/serial/OutputQueue.h`
- Modify: `Source/serial/SerialReader.{h,cpp}` (drain output in run loop)

- [ ] **Step 1: Create `OutputQueue`**

`OutputQueue.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <deque>

class OutputQueue {
public:
    void push(const std::vector<uint8_t>& bytes) {
        juce::ScopedLock lock(mutex);
        pending.push_back(bytes);
    }
    bool pop(std::vector<uint8_t>& out) {
        juce::ScopedLock lock(mutex);
        if (pending.empty()) return false;
        out = std::move(pending.front());
        pending.pop_front();
        return true;
    }
private:
    juce::CriticalSection mutex;
    std::deque<std::vector<uint8_t>> pending;
};
```

- [ ] **Step 2: Wire `OutputQueue` into `SerialReader::run`**

Add member `OutputQueue outQueue;` (public accessor `getOutputQueue()`) to `SerialReader.h`. In `run()`, after the read block:

```cpp
// drain output
std::vector<uint8_t> outChunk;
while (outQueue.pop(outChunk)) {
    sp_nonblocking_write(port, outChunk.data(), outChunk.size());
}
```

- [ ] **Step 3: Quick manual test — send `?BOTH\n` after connect**

In MainComponent ctor, after `reader.connect`:
```cpp
juce::Timer::callAfterDelay(500, [this]() {
    reader.getOutputQueue().push({'?','B','O','T','H','\n'});
});
```

- [ ] **Step 4: Build, run, verify**

Expected: a few hundred ms after the app starts and connects, you should see in console output (DBG from earlier callback) a fresh `[BANKS] count=8` + 8× `[BANK]` + `[STATE]` lines — proving the bidirectional path works.

- [ ] **Step 5: Commit**

```bash
git add Source/serial/OutputQueue.h Source/serial/SerialReader.h Source/serial/SerialReader.cpp
git commit -m "feat(viewer): OutputQueue + bidirectional serial writes"
```

---

### Task 7: Auto-detect ILLPAD + reconnect on disconnect

**Files:**
- Modify: `Source/serial/SerialReader.{h,cpp}`
- Modify: `Source/MainComponent.cpp` (use auto-detect)

- [ ] **Step 1: Add auto-detect on no-target**

In `SerialReader::run()`, replace the `if (target.empty()) { sleep; continue; }` block with:

```cpp
if (target.empty()) {
    auto ports = SerialPortDiscovery::listAvailable();
    auto found = SerialPortDiscovery::autoDetectILLPAD(ports);
    if (found) {
        juce::ScopedLock lock(portLock);
        portTarget = found->name;
    } else {
        juce::Thread::sleep(1000);
        continue;
    }
}
```

- [ ] **Step 2: Reconnect on I/O error**

Already in place via `closePort()` + retry loop, but ensure `portTarget` is preserved across reopens. The current implementation keeps the target until `disconnect()` is called — good.

- [ ] **Step 3: Replace manual port in MainComponent with auto-detect**

```cpp
reader.connect("");  // empty = auto-detect
```

- [ ] **Step 4: Build, run, plug/unplug test**

Plug ILLPAD before starting → auto-detect.
Unplug ILLPAD during runtime → reader marks disconnected, retries.
Replug → reconnects.

- [ ] **Step 5: Commit**

```bash
git add Source/serial Source/MainComponent.cpp
git commit -m "feat(viewer): auto-detect ILLPAD VID/PID + reconnect on disconnect"
```

---

## Phase 2 — Runtime parser (Layer 1)

### Task 8: `LineSplitter` — bytes to newline-delimited lines

**Files:**
- Create: `Source/serial/LineSplitter.h`
- Create: `Source/serial/LineSplitter.cpp`

- [ ] **Step 1: Define interface**

`LineSplitter.h`:
```cpp
#pragma once
#include <string>
#include <vector>
#include <functional>

class LineSplitter {
public:
    using LineCallback = std::function<void(const std::string&)>;
    void setCallback(LineCallback cb) { lineCb = std::move(cb); }
    void feed(const std::vector<uint8_t>& bytes);
    void reset() { buffer.clear(); }
private:
    std::string buffer;
    LineCallback lineCb;
};
```

- [ ] **Step 2: Implement**

```cpp
#include "LineSplitter.h"

void LineSplitter::feed(const std::vector<uint8_t>& bytes) {
    for (auto b : bytes) {
        if (b == '\n') {
            if (!buffer.empty() && buffer.back() == '\r') buffer.pop_back();
            if (lineCb) lineCb(buffer);
            buffer.clear();
        } else {
            buffer += (char)b;
            if (buffer.size() > 4096) buffer.clear();  // safety cap
        }
    }
}
```

- [ ] **Step 3: Add to CMakeLists.txt sources**

- [ ] **Step 4: Commit**

```bash
git add Source/serial/LineSplitter.h Source/serial/LineSplitter.cpp CMakeLists.txt
git commit -m "feat(viewer): LineSplitter (bytes -> \\n-delimited lines)"
```

---

### Task 9: `RuntimeParser` core + `ParsedEvent` variant + Catch2 setup

**Files:**
- Create: `Source/serial/RuntimeParser.h`
- Create: `Source/serial/RuntimeParser.cpp`
- Create: `Tests/CMakeLists.txt`
- Create: `Tests/test_RuntimeParser.cpp`
- Modify: root `CMakeLists.txt` (add Tests subdir, Catch2 fetch)

- [ ] **Step 1: Define event types + variant**

`RuntimeParser.h`:
```cpp
#pragma once
#include <string>
#include <variant>
#include <vector>
#include <optional>

// --- event types ---

struct BanksHeaderEvent { int count; };

struct BankInfoEvent {
    int idx;
    std::string type;                       // "NORMAL", "ARPEG", "ARPEG_GEN", "LOOP"
    int channel;
    char group;                              // '0'..'D'
    std::optional<std::string> division;
    std::optional<bool> playing;
    std::optional<int> octave;
    std::optional<int> mutationLevel;
};

struct PotSlotData {
    std::string slot;       // "R1", "R1H", ...
    std::string target;     // "Tempo", "Gate", ...
    std::string value;      // "120", "1/8", "Up", "0.50"
    bool isEmpty = false;
};

struct StateEvent {
    int bank;
    std::string mode;
    int channel;
    std::string scaleRoot;
    std::string scaleMode;
    bool chromatic = false;
    std::optional<int> octave;
    std::optional<int> mutationLevel;
    PotSlotData slots[8];
};

struct PotEvent {
    std::string slot;
    std::string target;
    std::string value;
    std::string unit;   // optional "BPM", empty otherwise
};

struct BankSwitchEvent {
    int newBank;
    int channel;
    std::string mode;
};

struct ScaleEvent {
    std::string root;
    std::string mode;
    bool chromatic;
};

enum class ArpAction { NoteAdd, NoteRemove, Play, PlayRelaunch, Stop, StopCleared, OctaveChange, QueueFullWarning };
struct ArpEvent {
    int bank;
    ArpAction action;
    std::optional<int> count;
};

struct ArpGenEvent { int mutationLevel; };

struct GenEvent {
    int seqLen;
    std::optional<int> eInit;
    std::optional<int> pile;
    std::optional<int> lo;
    std::optional<int> hi;
    bool degenerate;  // pile=1 case
};

enum class ClockSource { USB, BLE, Internal, LastKnown };
struct ClockEvent {
    ClockSource source;
    std::optional<float> bpm;  // present for "last known (X BPM)"
};

enum class MidiTransport { BLE, USB };
struct MidiEvent { MidiTransport which; bool connected; };

struct PanicEvent {};

struct UnknownEvent { std::string raw; };

using ParsedEvent = std::variant<
    BanksHeaderEvent, BankInfoEvent, StateEvent,
    PotEvent, BankSwitchEvent, ScaleEvent,
    ArpEvent, ArpGenEvent, GenEvent,
    ClockEvent, MidiEvent, PanicEvent,
    UnknownEvent
>;

// --- parser entry ---
ParsedEvent parseRuntimeLine(std::string_view line);
```

- [ ] **Step 2: Stub the parser, return UnknownEvent for everything initially**

```cpp
#include "RuntimeParser.h"
ParsedEvent parseRuntimeLine(std::string_view line) {
    return UnknownEvent{std::string(line)};
}
```

- [ ] **Step 3: Set up Catch2 v3 via CMake FetchContent**

In root `CMakeLists.txt`, after `project(...)`:

```cmake
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)
    enable_testing()
    add_subdirectory(Tests)
endif()
```

- [ ] **Step 4: Create `Tests/CMakeLists.txt`**

```cmake
add_executable(illpad_viewer_tests
    test_RuntimeParser.cpp
    ${CMAKE_SOURCE_DIR}/Source/serial/RuntimeParser.cpp
)
target_include_directories(illpad_viewer_tests PRIVATE ${CMAKE_SOURCE_DIR}/Source)
target_link_libraries(illpad_viewer_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(illpad_viewer_tests)
```

- [ ] **Step 5: Create first failing test**

`Tests/test_RuntimeParser.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "serial/RuntimeParser.h"

TEST_CASE("BanksHeader parses count", "[parser]") {
    auto ev = parseRuntimeLine("[BANKS] count=8");
    auto* h = std::get_if<BanksHeaderEvent>(&ev);
    REQUIRE(h != nullptr);
    REQUIRE(h->count == 8);
}
```

- [ ] **Step 6: Run test (expect FAIL)**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 1 test fails because parser returns UnknownEvent.

- [ ] **Step 7: Implement `BanksHeader` parsing**

Replace the parser body:
```cpp
#include <cstdio>
#include <cstring>

ParsedEvent parseRuntimeLine(std::string_view line) {
    std::string s(line);
    int count;
    if (sscanf(s.c_str(), "[BANKS] count=%d", &count) == 1) {
        return BanksHeaderEvent{count};
    }
    return UnknownEvent{s};
}
```

- [ ] **Step 8: Run test (expect PASS)**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 9: Commit**

```bash
git add Source/serial/RuntimeParser.h Source/serial/RuntimeParser.cpp Tests CMakeLists.txt
git commit -m "feat(viewer): RuntimeParser scaffold + Catch2 v3 + first test passing"
```

---

### Task 10: Parse `[BANK]` and `[STATE]` events (TDD)

**Files:**
- Modify: `Source/serial/RuntimeParser.cpp`
- Modify: `Tests/test_RuntimeParser.cpp`

- [ ] **Step 1: Add failing tests for `[BANK]`**

```cpp
TEST_CASE("BankInfo NORMAL bank", "[parser]") {
    auto ev = parseRuntimeLine("[BANK] idx=1 type=NORMAL ch=1 group=A");
    auto* b = std::get_if<BankInfoEvent>(&ev);
    REQUIRE(b != nullptr);
    REQUIRE(b->idx == 1);
    REQUIRE(b->type == "NORMAL");
    REQUIRE(b->channel == 1);
    REQUIRE(b->group == 'A');
    REQUIRE_FALSE(b->division.has_value());
    REQUIRE_FALSE(b->octave.has_value());
}

TEST_CASE("BankInfo ARPEG with division+playing+octave", "[parser]") {
    auto ev = parseRuntimeLine("[BANK] idx=3 type=ARPEG ch=3 group=B division=1/8 playing=true octave=2");
    auto* b = std::get_if<BankInfoEvent>(&ev);
    REQUIRE(b != nullptr);
    REQUIRE(b->type == "ARPEG");
    REQUIRE(b->division.value() == "1/8");
    REQUIRE(b->playing.value() == true);
    REQUIRE(b->octave.value() == 2);
}

TEST_CASE("BankInfo ARPEG_GEN with mutationLevel", "[parser]") {
    auto ev = parseRuntimeLine("[BANK] idx=5 type=ARPEG_GEN ch=5 group=0 division=1/4 playing=false mutationLevel=3");
    auto* b = std::get_if<BankInfoEvent>(&ev);
    REQUIRE(b != nullptr);
    REQUIRE(b->type == "ARPEG_GEN");
    REQUIRE(b->mutationLevel.value() == 3);
    REQUIRE(b->group == '0');
}
```

- [ ] **Step 2: Run tests (expect FAIL)**

- [ ] **Step 3: Implement `[BANK]` parsing**

Add token-based parsing logic in `parseRuntimeLine`. Strategy: prefix match on `[BANK]`, then split rest into space-separated `key=value` pairs, fill the `BankInfoEvent`. Use a small helper:

```cpp
static std::optional<std::string> findKey(const std::string& s, const std::string& key) {
    auto k = key + "=";
    auto pos = s.find(k);
    if (pos == std::string::npos) return std::nullopt;
    auto valStart = pos + k.size();
    auto valEnd = s.find(' ', valStart);
    if (valEnd == std::string::npos) valEnd = s.size();
    return s.substr(valStart, valEnd - valStart);
}

// in parseRuntimeLine:
// Strict prefix: [BANK] idx=... is the dump format. [BANK] Bank N (ch ...) is
// the switch event (handled below in Task 11 step 2). Disambiguation by
// "idx=" suffix avoids ordering dependency between the two branches.
if (s.rfind("[BANK] idx=", 0) == 0) {
    BankInfoEvent b;
    auto idx = findKey(s, "idx");
    auto type = findKey(s, "type");
    auto ch = findKey(s, "ch");
    auto group = findKey(s, "group");
    if (idx) b.idx = std::stoi(*idx);
    if (type) b.type = *type;
    if (ch) b.channel = std::stoi(*ch);
    if (group) b.group = (*group)[0];
    if (auto v = findKey(s, "division"); v) b.division = *v;
    if (auto v = findKey(s, "playing"); v) b.playing = (*v == "true");
    if (auto v = findKey(s, "octave"); v) b.octave = std::stoi(*v);
    if (auto v = findKey(s, "mutationLevel"); v) b.mutationLevel = std::stoi(*v);
    return b;
}
```

- [ ] **Step 4: Run tests (expect PASS)**

- [ ] **Step 5: Add failing tests for `[STATE]`**

```cpp
TEST_CASE("StateEvent ARPEG with octave and 8 slots", "[parser]") {
    // Firmware uses diatonic mode names (Ionian..Locrian) — cf. ScaleManager.cpp:9-13
    // and KeyboardData.h:338-342. The viewer aligns on this convention (decision
    // B3 from 2026-05-15 cross-audit). Use "Aeolian" instead of "Minor".
    auto ev = parseRuntimeLine(
        "[STATE] bank=3 mode=ARPEG ch=3 scale=D:Aeolian octave=2 "
        "R1=Tempo:120 R1H=BaseVel:80 R2=Gate:0.50 R2H=VelVar:20 "
        "R3=Division:1/8 R3H=PitchBend:64 R4=Pattern:Up R4H=ShufDepth:0.30"
    );
    auto* st = std::get_if<StateEvent>(&ev);
    REQUIRE(st != nullptr);
    REQUIRE(st->bank == 3);
    REQUIRE(st->mode == "ARPEG");
    REQUIRE(st->scaleRoot == "D");
    REQUIRE(st->scaleMode == "Aeolian");
    REQUIRE_FALSE(st->chromatic);
    REQUIRE(st->octave.value() == 2);
    REQUIRE(st->slots[0].slot == "R1");
    REQUIRE(st->slots[0].target == "Tempo");
    REQUIRE(st->slots[0].value == "120");
    REQUIRE(st->slots[2].target == "Gate");
    REQUIRE(st->slots[2].value == "0.50");
}

TEST_CASE("StateEvent Chromatic + empty slot", "[parser]") {
    auto ev = parseRuntimeLine(
        "[STATE] bank=1 mode=NORMAL ch=1 scale=Chromatic:C "
        "R1=Tempo:120 R1H=--- R2=BaseVel:64 R2H=--- "
        "R3=PitchBend:0 R3H=--- R4=Shape:0.50 R4H=---"
    );
    auto* st = std::get_if<StateEvent>(&ev);
    REQUIRE(st != nullptr);
    REQUIRE(st->chromatic);
    REQUIRE(st->scaleRoot == "C");
    REQUIRE(st->slots[1].isEmpty);
    REQUIRE(st->slots[3].isEmpty);
}
```

- [ ] **Step 6: Implement `[STATE]` parsing**

Add to `parseRuntimeLine`:
```cpp
if (s.rfind("[STATE] ", 0) == 0) {
    StateEvent st;
    if (auto v = findKey(s, "bank"); v) st.bank = std::stoi(*v);
    if (auto v = findKey(s, "mode"); v) st.mode = *v;
    if (auto v = findKey(s, "ch"); v) st.channel = std::stoi(*v);
    if (auto v = findKey(s, "scale"); v) {
        // "C:Major" or "Chromatic:D"
        auto colonPos = v->find(':');
        if (colonPos != std::string::npos) {
            auto left = v->substr(0, colonPos);
            auto right = v->substr(colonPos + 1);
            if (left == "Chromatic") { st.chromatic = true; st.scaleRoot = right; }
            else { st.scaleRoot = left; st.scaleMode = right; }
        }
    }
    if (auto v = findKey(s, "octave"); v) st.octave = std::stoi(*v);
    if (auto v = findKey(s, "mutationLevel"); v) st.mutationLevel = std::stoi(*v);

    // 8 slots
    const char* SLOT_NAMES[8] = { "R1","R1H","R2","R2H","R3","R3H","R4","R4H" };
    for (int i = 0; i < 8; i++) {
        st.slots[i].slot = SLOT_NAMES[i];
        if (auto v = findKey(s, SLOT_NAMES[i]); v) {
            if (*v == "---") { st.slots[i].isEmpty = true; }
            else {
                auto colon = v->find(':');
                if (colon != std::string::npos) {
                    st.slots[i].target = v->substr(0, colon);
                    st.slots[i].value = v->substr(colon + 1);
                }
            }
        }
    }
    return st;
}
```

- [ ] **Step 7: Run tests (expect all PASS)**

- [ ] **Step 8: Commit**

```bash
git add Source/serial/RuntimeParser.cpp Tests/test_RuntimeParser.cpp
git commit -m "feat(viewer): parse [BANK] and [STATE] events with TDD"
```

---

### Task 11: Parse `[POT]`, `[BANK]` switch, `[SCALE]`, `[ARP]`, `[ARP_GEN]`, `[GEN]`, `[CLOCK]`, `[MIDI]`, `[PANIC]`

**Files:**
- Modify: `Source/serial/RuntimeParser.cpp`
- Modify: `Tests/test_RuntimeParser.cpp`

For each event type below: write failing test first, then implement the parsing branch, verify pass, commit.

- [ ] **Step 1: `[POT]` events — test + impl**

Test:
```cpp
TEST_CASE("PotEvent with unit", "[parser]") {
    auto ev = parseRuntimeLine("[POT] R1: Tempo=120 BPM");
    auto* p = std::get_if<PotEvent>(&ev);
    REQUIRE(p != nullptr);
    REQUIRE(p->slot == "R1");
    REQUIRE(p->target == "Tempo");
    REQUIRE(p->value == "120");
    REQUIRE(p->unit == "BPM");
}
TEST_CASE("PotEvent no unit", "[parser]") {
    auto ev = parseRuntimeLine("[POT] R3H: Division=1/8");
    auto* p = std::get_if<PotEvent>(&ev);
    REQUIRE(p != nullptr);
    REQUIRE(p->slot == "R3H");
    REQUIRE(p->target == "Division");
    REQUIRE(p->value == "1/8");
    REQUIRE(p->unit.empty());
}
TEST_CASE("PotEvent float value", "[parser]") {
    auto ev = parseRuntimeLine("[POT] R2: Gate=0.50");
    auto* p = std::get_if<PotEvent>(&ev);
    REQUIRE(p != nullptr);
    REQUIRE(p->value == "0.50");
}
```

Impl branch:
```cpp
if (s.rfind("[POT] ", 0) == 0) {
    // Format: "[POT] SLOT: TARGET=VALUE [UNIT]"
    // Note: firmware also emits boot-time lines without ": " pattern (e.g.
    // "[POT] Rebuilt 16 bindings from mapping (3 CC slots)" from PotRouter.cpp:267).
    // Those must NOT be parsed as PotEvent — return UnknownEvent so the Model
    // does not get a zombie event with empty slot/target/value.
    auto afterTag = s.substr(6);  // skip "[POT] "
    auto colonPos = afterTag.find(": ");
    if (colonPos == std::string::npos) {
        return UnknownEvent{s};
    }
    PotEvent p;
    p.slot = afterTag.substr(0, colonPos);
    auto rest = afterTag.substr(colonPos + 2);
    auto eqPos = rest.find('=');
    if (eqPos == std::string::npos) {
        return UnknownEvent{s};
    }
    p.target = rest.substr(0, eqPos);
    auto valPart = rest.substr(eqPos + 1);
    auto spacePos = valPart.find(' ');
    if (spacePos != std::string::npos) {
        p.value = valPart.substr(0, spacePos);
        p.unit = valPart.substr(spacePos + 1);
    } else {
        p.value = valPart;
    }
    return p;
}
```

Add a failing test for the zombie case to lock the behavior:
```cpp
TEST_CASE("PotEvent reject non-annotated firmware lines", "[parser]") {
    // PotRouter.cpp:267 emits this at boot — must not parse as a slot event.
    auto ev = parseRuntimeLine("[POT] Rebuilt 16 bindings from mapping (3 CC slots)");
    REQUIRE(std::holds_alternative<UnknownEvent>(ev));
}
```

- [ ] **Step 2: `[BANK] Bank N (ch N, MODE)` — bank switch event**

Test:
```cpp
TEST_CASE("BankSwitchEvent", "[parser]") {
    auto ev = parseRuntimeLine("[BANK] Bank 3 (ch 3, ARPEG)");
    auto* b = std::get_if<BankSwitchEvent>(&ev);
    REQUIRE(b != nullptr);
    REQUIRE(b->newBank == 3);
    REQUIRE(b->channel == 3);
    REQUIRE(b->mode == "ARPEG");
}
```

Impl branch (strict prefix `[BANK] Bank ` to disambiguate from `[BANK] idx=...` of Task 10). Branch order is no longer fragile thanks to the explicit prefixes:
```cpp
if (s.rfind("[BANK] Bank ", 0) == 0) {
    BankSwitchEvent b;
    char mode[32]={0};
    if (sscanf(s.c_str(), "[BANK] Bank %d (ch %d, %31[^)])", &b.newBank, &b.channel, mode) == 3) {
        b.mode = mode;
        return b;
    }
}
```

- [ ] **Step 3: `[SCALE]` events**

Tests:
```cpp
// Firmware emits diatonic mode names (Ionian..Locrian) from ScaleManager.cpp:134-156.
TEST_CASE("ScaleEvent Root+Mode", "[parser]") {
    auto ev = parseRuntimeLine("[SCALE] Root C (mode Ionian)");
    auto* s = std::get_if<ScaleEvent>(&ev);
    REQUIRE(s != nullptr);
    REQUIRE(s->root == "C");
    REQUIRE(s->mode == "Ionian");
    REQUIRE_FALSE(s->chromatic);
}
TEST_CASE("ScaleEvent Chromatic", "[parser]") {
    auto ev = parseRuntimeLine("[SCALE] Chromatic (root D)");
    auto* s = std::get_if<ScaleEvent>(&ev);
    REQUIRE(s != nullptr);
    REQUIRE(s->chromatic);
    REQUIRE(s->root == "D");
}
```

Impl:
```cpp
if (s.rfind("[SCALE] Root ", 0) == 0) {
    ScaleEvent e; e.chromatic = false;
    char root[8]={0}, mode[16]={0};
    if (sscanf(s.c_str(), "[SCALE] Root %7s (mode %15[^)])", root, mode) == 2) {
        e.root = root; e.mode = mode; return e;
    }
}
if (s.rfind("[SCALE] Mode ", 0) == 0) {
    ScaleEvent e; e.chromatic = false;
    char mode[16]={0}, root[8]={0};
    if (sscanf(s.c_str(), "[SCALE] Mode %15s (root %7[^)])", mode, root) == 2) {
        e.root = root; e.mode = mode; return e;
    }
}
if (s.rfind("[SCALE] Chromatic", 0) == 0) {
    ScaleEvent e; e.chromatic = true;
    char root[8]={0};
    if (sscanf(s.c_str(), "[SCALE] Chromatic (root %7[^)])", root) == 1) {
        e.root = root; return e;
    }
}
```

- [ ] **Step 4: `[ARP]` events (note +/-, Play, Stop, Octave, queue full)**

Tests:
```cpp
TEST_CASE("ArpEvent +note", "[parser]") {
    auto ev = parseRuntimeLine("[ARP] Bank 3: +note (5 total)");
    auto* a = std::get_if<ArpEvent>(&ev);
    REQUIRE(a != nullptr);
    REQUIRE(a->bank == 3);
    REQUIRE(a->action == ArpAction::NoteAdd);
    REQUIRE(a->count.value() == 5);
}
TEST_CASE("ArpEvent Play", "[parser]") {
    auto ev = parseRuntimeLine("[ARP] Bank 2: Play (pile 4 notes)");
    auto* a = std::get_if<ArpEvent>(&ev);
    REQUIRE(a != nullptr);
    REQUIRE(a->action == ArpAction::Play);
    REQUIRE(a->count.value() == 4);
}
TEST_CASE("ArpEvent Stop pile kept", "[parser]") {
    auto ev = parseRuntimeLine("[ARP] Bank 2: Stop — pile kept (4 notes)");
    auto* a = std::get_if<ArpEvent>(&ev);
    REQUIRE(a != nullptr);
    REQUIRE(a->action == ArpAction::Stop);
}
TEST_CASE("ArpEvent Octave", "[parser]") {
    auto ev = parseRuntimeLine("[ARP] Octave 3");
    auto* a = std::get_if<ArpEvent>(&ev);
    REQUIRE(a != nullptr);
    REQUIRE(a->action == ArpAction::OctaveChange);
    REQUIRE(a->count.value() == 3);
}
TEST_CASE("ArpEvent QueueFull", "[parser]") {
    auto ev = parseRuntimeLine("[ARP] WARNING: Event queue full — event dropped");
    auto* a = std::get_if<ArpEvent>(&ev);
    REQUIRE(a != nullptr);
    REQUIRE(a->action == ArpAction::QueueFullWarning);
}
```

Impl:
```cpp
if (s.rfind("[ARP] WARNING:", 0) == 0) {
    ArpEvent a; a.bank = 0; a.action = ArpAction::QueueFullWarning;
    return a;
}
if (s.rfind("[ARP] Octave ", 0) == 0) {
    ArpEvent a; a.bank = 0; a.action = ArpAction::OctaveChange;
    int n; if (sscanf(s.c_str(), "[ARP] Octave %d", &n) == 1) a.count = n;
    return a;
}
if (s.rfind("[ARP] Bank ", 0) == 0) {
    ArpEvent a;
    int bank, count;
    if (sscanf(s.c_str(), "[ARP] Bank %d: +note (%d total)", &bank, &count) == 2) {
        a.bank = bank; a.action = ArpAction::NoteAdd; a.count = count; return a;
    }
    if (sscanf(s.c_str(), "[ARP] Bank %d: -note (%d total)", &bank, &count) == 2) {
        a.bank = bank; a.action = ArpAction::NoteRemove; a.count = count; return a;
    }
    if (sscanf(s.c_str(), "[ARP] Bank %d: Play (pile %d notes)", &bank, &count) == 2) {
        a.bank = bank; a.action = ArpAction::Play; a.count = count; return a;
    }
    if (sscanf(s.c_str(), "[ARP] Bank %d: Play — relaunch paused pile (%d notes)", &bank, &count) == 2) {
        a.bank = bank; a.action = ArpAction::PlayRelaunch; a.count = count; return a;
    }
    if (sscanf(s.c_str(), "[ARP] Bank %d: Stop — pile kept (%d notes)", &bank, &count) == 2) {
        a.bank = bank; a.action = ArpAction::Stop; a.count = count; return a;
    }
    if (s.find("Stop — fingers down") != std::string::npos) {
        if (sscanf(s.c_str(), "[ARP] Bank %d:", &bank) == 1) {
            a.bank = bank; a.action = ArpAction::StopCleared; return a;
        }
    }
}
```

- [ ] **Step 5: `[ARP_GEN]`, `[GEN]`, `[CLOCK]`, `[MIDI]`, `[PANIC]`**

Tests:
```cpp
TEST_CASE("ArpGenEvent MutationLevel", "[parser]") {
    auto ev = parseRuntimeLine("[ARP_GEN] MutationLevel 3");
    auto* e = std::get_if<ArpGenEvent>(&ev);
    REQUIRE(e != nullptr);
    REQUIRE(e->mutationLevel == 3);
}
TEST_CASE("GenEvent normal seed", "[parser]") {
    auto ev = parseRuntimeLine("[GEN] seed seqLen=16 E_init=3 pile=4 lo=-2 hi=5");
    auto* g = std::get_if<GenEvent>(&ev);
    REQUIRE(g != nullptr);
    REQUIRE(g->seqLen == 16);
    REQUIRE(g->eInit.value() == 3);
    REQUIRE_FALSE(g->degenerate);
}
TEST_CASE("GenEvent degenerate", "[parser]") {
    auto ev = parseRuntimeLine("[GEN] seed seqLen=8 (pile=1 note 4, repetition)");
    auto* g = std::get_if<GenEvent>(&ev);
    REQUIRE(g != nullptr);
    REQUIRE(g->seqLen == 8);
    REQUIRE(g->degenerate);
}
TEST_CASE("ClockEvent USB", "[parser]") {
    auto ev = parseRuntimeLine("[CLOCK] Source: USB");
    auto* c = std::get_if<ClockEvent>(&ev);
    REQUIRE(c != nullptr);
    REQUIRE(c->source == ClockSource::USB);
}
TEST_CASE("ClockEvent BLE after USB timeout (must not match USB)", "[parser]") {
    // Regression: naive `s.find("USB")` would misclassify this line.
    auto ev = parseRuntimeLine("[CLOCK] Source: BLE (USB timed out)");
    auto* c = std::get_if<ClockEvent>(&ev);
    REQUIRE(c != nullptr);
    REQUIRE(c->source == ClockSource::BLE);
}
TEST_CASE("ClockEvent last known", "[parser]") {
    auto ev = parseRuntimeLine("[CLOCK] Source: last known (120 BPM)");
    auto* c = std::get_if<ClockEvent>(&ev);
    REQUIRE(c != nullptr);
    REQUIRE(c->source == ClockSource::LastKnown);
    REQUIRE(c->bpm.value() == 120.0f);
}
TEST_CASE("MidiEvent BLE connected", "[parser]") {
    auto ev = parseRuntimeLine("[MIDI] BLE connected");
    auto* m = std::get_if<MidiEvent>(&ev);
    REQUIRE(m != nullptr);
    REQUIRE(m->which == MidiTransport::BLE);
    REQUIRE(m->connected);
}
TEST_CASE("MidiEvent rejects 'initialized' boot lines", "[parser]") {
    // MidiTransport.cpp:92,103,107 emits these at boot — must NOT be parsed
    // as a connection state change.
    REQUIRE(std::holds_alternative<UnknownEvent>(
        parseRuntimeLine("[MIDI] USB MIDI initialized.")));
    REQUIRE(std::holds_alternative<UnknownEvent>(
        parseRuntimeLine("[MIDI] BLE MIDI initialized.")));
    REQUIRE(std::holds_alternative<UnknownEvent>(
        parseRuntimeLine("[MIDI] BLE disabled (USB only).")));
}
TEST_CASE("PanicEvent", "[parser]") {
    auto ev = parseRuntimeLine("[PANIC] All notes off on all channels");
    REQUIRE(std::holds_alternative<PanicEvent>(ev));
}
```

Impl branches:
```cpp
if (s.rfind("[ARP_GEN] MutationLevel ", 0) == 0) {
    int n; if (sscanf(s.c_str(), "[ARP_GEN] MutationLevel %d", &n) == 1) return ArpGenEvent{n};
}
if (s.rfind("[GEN] seed", 0) == 0) {
    GenEvent g; g.degenerate = false;
    int seqLen, eInit, pile, lo, hi, d;
    if (sscanf(s.c_str(), "[GEN] seed seqLen=%d (pile=1 note %d, repetition)", &seqLen, &d) == 2) {
        g.seqLen = seqLen; g.degenerate = true; return g;
    }
    if (sscanf(s.c_str(), "[GEN] seed seqLen=%d E_init=%d pile=%d lo=%d hi=%d",
               &seqLen, &eInit, &pile, &lo, &hi) == 5) {
        g.seqLen = seqLen; g.eInit = eInit; g.pile = pile; g.lo = lo; g.hi = hi; return g;
    }
}
if (s.rfind("[CLOCK] Source:", 0) == 0) {
    ClockEvent c;
    // Parse the first token after "Source: " strictly. ClockManager emits e.g.
    // "[CLOCK] Source: BLE (USB timed out)" — naive substring "USB" would
    // misclassify this as USB. The token-based check fixes that.
    auto afterPrefix = s.substr(std::string("[CLOCK] Source: ").size());
    auto firstSpace = afterPrefix.find(' ');
    std::string token = (firstSpace == std::string::npos) ? afterPrefix
                                                          : afterPrefix.substr(0, firstSpace);
    if      (token == "USB")       c.source = ClockSource::USB;
    else if (token == "BLE")       c.source = ClockSource::BLE;
    else if (token == "internal")  c.source = ClockSource::Internal;
    else if (token == "last")      c.source = ClockSource::LastKnown;  // "last known (...)"
    else                           c.source = ClockSource::Internal;   // fallback
    float bpm;
    if (sscanf(s.c_str(), "[CLOCK] Source: last known (%f BPM)", &bpm) == 1) c.bpm = bpm;
    return c;
}
if (s.rfind("[MIDI] ", 0) == 0) {
    // Strict: only "[MIDI] BLE connected", "[MIDI] BLE disconnected",
    // "[MIDI] USB connected", "[MIDI] USB disconnected". Other [MIDI] lines
    // emitted at boot (e.g. "USB MIDI initialized.", "BLE disabled (USB only).")
    // must NOT produce a MidiEvent — they would otherwise incorrectly mark
    // the transport as disconnected (no "connected" token found).
    MidiEvent m;
    bool isBLE = s.rfind("[MIDI] BLE ", 0) == 0;
    bool isUSB = s.rfind("[MIDI] USB ", 0) == 0;
    if (isBLE || isUSB) {
        // Check that suffix is exactly "connected" or "disconnected"
        auto suffix = s.substr(11);  // after "[MIDI] BLE " or "[MIDI] USB "
        if (suffix == "connected" || suffix == "disconnected") {
            m.which = isBLE ? MidiTransport::BLE : MidiTransport::USB;
            m.connected = (suffix == "connected");
            return m;
        }
    }
    return UnknownEvent{s};
}
if (s.rfind("[PANIC]", 0) == 0) {
    return PanicEvent{};
}
```

- [ ] **Step 6: Run all tests (expect ALL PASS)**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add Source/serial/RuntimeParser.cpp Tests/test_RuntimeParser.cpp
git commit -m "feat(viewer): RuntimeParser covers all runtime event categories"
```

---

## Phase 3 — Runtime Model (Layer 2)

### Task 12: `DeviceState`, `BankInfo`, `CurrentBankState`, `PotSlot`, `EventBuffer` data types

**Files:**
- Create: `Source/model/DeviceState.h`
- Create: `Source/model/BankInfo.h`
- Create: `Source/model/CurrentBankState.h`
- Create: `Source/model/PotSlot.h`
- Create: `Source/model/EventBuffer.h`
- Create: `Source/enums/BankType.h`
- Create: `Source/enums/ClockSource.h`

- [ ] **Step 1: Define shared enums in `Source/enums/`**

`BankType.h`:
```cpp
#pragma once
enum class BankType { NORMAL, ARPEG, ARPEG_GEN, LOOP, UNKNOWN };
inline const char* bankTypeName(BankType t) {
    switch (t) {
        case BankType::NORMAL: return "NORMAL";
        case BankType::ARPEG: return "ARPEG";
        case BankType::ARPEG_GEN: return "ARPEG_GEN";
        case BankType::LOOP: return "LOOP";
        default: return "?";
    }
}
inline BankType bankTypeFromString(const std::string& s) {
    if (s == "NORMAL") return BankType::NORMAL;
    if (s == "ARPEG") return BankType::ARPEG;
    if (s == "ARPEG_GEN") return BankType::ARPEG_GEN;
    if (s == "LOOP") return BankType::LOOP;
    return BankType::UNKNOWN;
}
```

`ClockSource.h`:
```cpp
#pragma once
enum class ClockSourceEnum { USB, BLE, Internal, LastKnown, Unknown };
```

- [ ] **Step 2: Model structs**

`DeviceState.h`:
```cpp
#pragma once
#include "enums/ClockSource.h"
#include <chrono>

struct DeviceState {
    int tempoBpm = 120;
    ClockSourceEnum clockSource = ClockSourceEnum::Unknown;
    bool bleMidiConnected = false;
    bool usbMidiConnected = false;
    std::chrono::steady_clock::time_point lastPanic = {};  // zero = never
    bool isPanicRecent() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPanic).count() < 2500;
    }
};
```

`PotSlot.h`:
```cpp
#pragma once
#include <string>
#include <chrono>

struct PotSlot {
    std::string slotName;     // R1, R1H, ...
    std::string target;       // Tempo, Gate, ...
    std::string displayValue; // "120", "1/8"
    std::string unit;         // "BPM" or empty
    bool isEmpty = false;
    std::chrono::steady_clock::time_point lastChange = {};
    bool isRecentlyChanged() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastChange).count() < 300;
    }
};
```

`BankInfo.h`:
```cpp
#pragma once
#include "enums/BankType.h"
#include <optional>
#include <string>

struct BankInfo {
    int idx = 0;                 // 1..8
    BankType type = BankType::UNKNOWN;
    int channel = 0;
    char group = '0';            // '0' (none) or 'A'..'D'
    // arp-only
    std::optional<std::string> division;
    bool playing = false;
    std::optional<int> octave;
    std::optional<int> mutationLevel;
    // scale (propagated via [SCALE] events even though not in [BANKS] dump)
    std::string scaleRoot;
    std::string scaleMode;
    bool chromatic = false;
};
```

`CurrentBankState.h`:
```cpp
#pragma once
#include "PotSlot.h"
#include "enums/BankType.h"
#include <optional>
#include <string>

struct CurrentBankState {
    int idx = 0;
    BankType type = BankType::UNKNOWN;
    int channel = 0;
    std::string scaleRoot;
    std::string scaleMode;
    bool chromatic = false;
    std::optional<int> octave;
    std::optional<int> mutationLevel;
    PotSlot slots[8];  // R1, R1H, R2, R2H, R3, R3H, R4, R4H
};
```

`EventBuffer.h`:
```cpp
#pragma once
#include <chrono>
#include <deque>
#include <string>

struct LoggedEvent {
    std::chrono::steady_clock::time_point time;
    std::string category;   // "BANK", "POT", "SCALE", "ARP", ...
    std::string summary;
    bool isPanic = false;
};

class EventBuffer {
public:
    explicit EventBuffer(size_t maxEntries = 500) : maxEntries(maxEntries) {}
    void push(const LoggedEvent& e) {
        events.push_back(e);
        if (events.size() > maxEntries) events.pop_front();
    }
    const std::deque<LoggedEvent>& getAll() const { return events; }
private:
    size_t maxEntries;
    std::deque<LoggedEvent> events;
};
```

- [ ] **Step 3: Commit**

```bash
git add Source/model Source/enums
git commit -m "feat(viewer): model data types (DeviceState, BankInfo, CurrentBankState, PotSlot, EventBuffer)"
```

---

### Task 13: `Model` aggregator + ChangeBroadcaster listeners

**Files:**
- Create: `Source/model/Model.h`
- Create: `Source/model/Model.cpp`

- [ ] **Step 1: Define `Model` class**

`Model.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include "DeviceState.h"
#include "BankInfo.h"
#include "CurrentBankState.h"
#include "EventBuffer.h"
#include "../serial/RuntimeParser.h"

class Model : public juce::ChangeBroadcaster {
public:
    Model();

    // Called from MessageThread (AsyncUpdater drains parser output here)
    void apply(const ParsedEvent& event);
    void resetForReconnect();

    // Read-only accessors
    const DeviceState& getDevice() const         { return device; }
    const BankInfo& getBank(int idx1) const      { return banks[idx1 - 1]; }
    const CurrentBankState& getCurrent() const   { return current; }
    const EventBuffer& getEvents() const         { return events; }

private:
    DeviceState device;
    BankInfo banks[8];
    CurrentBankState current;
    EventBuffer events;

    void applyBanksHeader(const BanksHeaderEvent&);
    void applyBankInfo(const BankInfoEvent&);
    void applyState(const StateEvent&);
    void applyPot(const PotEvent&);
    void applyBankSwitch(const BankSwitchEvent&);
    void applyScale(const ScaleEvent&);
    void applyArp(const ArpEvent&);
    void applyArpGen(const ArpGenEvent&);
    void applyGen(const GenEvent&);
    void applyClock(const ClockEvent&);
    void applyMidi(const MidiEvent&);
    void applyPanic(const PanicEvent&);
};
```

- [ ] **Step 2: Implement event dispatching**

`Model.cpp`:
```cpp
#include "Model.h"

Model::Model() {
    for (int i = 0; i < 8; i++) banks[i].idx = i + 1;
}

void Model::apply(const ParsedEvent& event) {
    std::visit([this](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, BanksHeaderEvent>) applyBanksHeader(e);
        else if constexpr (std::is_same_v<T, BankInfoEvent>) applyBankInfo(e);
        else if constexpr (std::is_same_v<T, StateEvent>) applyState(e);
        else if constexpr (std::is_same_v<T, PotEvent>) applyPot(e);
        else if constexpr (std::is_same_v<T, BankSwitchEvent>) applyBankSwitch(e);
        else if constexpr (std::is_same_v<T, ScaleEvent>) applyScale(e);
        else if constexpr (std::is_same_v<T, ArpEvent>) applyArp(e);
        else if constexpr (std::is_same_v<T, ArpGenEvent>) applyArpGen(e);
        else if constexpr (std::is_same_v<T, GenEvent>) applyGen(e);
        else if constexpr (std::is_same_v<T, ClockEvent>) applyClock(e);
        else if constexpr (std::is_same_v<T, MidiEvent>) applyMidi(e);
        else if constexpr (std::is_same_v<T, PanicEvent>) applyPanic(e);
        // UnknownEvent: ignore
    }, event);
    sendChangeMessage();
}

void Model::resetForReconnect() {
    device = DeviceState{};
    for (auto& b : banks) b = BankInfo{};
    for (int i = 0; i < 8; i++) banks[i].idx = i + 1;
    current = CurrentBankState{};
    // Keep event buffer for context.
    sendChangeMessage();
}

void Model::applyBanksHeader(const BanksHeaderEvent&) { /* could log */ }

void Model::applyBankInfo(const BankInfoEvent& e) {
    if (e.idx < 1 || e.idx > 8) return;
    auto& b = banks[e.idx - 1];
    b.idx = e.idx;
    b.type = bankTypeFromString(e.type);
    b.channel = e.channel;
    b.group = e.group;
    b.division = e.division;
    b.playing = e.playing.value_or(false);
    b.octave = e.octave;
    b.mutationLevel = e.mutationLevel;
}

void Model::applyState(const StateEvent& e) {
    current.idx = e.bank;
    current.type = bankTypeFromString(e.mode);
    current.channel = e.channel;
    current.scaleRoot = e.scaleRoot;
    current.scaleMode = e.scaleMode;
    current.chromatic = e.chromatic;
    current.octave = e.octave;
    current.mutationLevel = e.mutationLevel;
    for (int i = 0; i < 8; i++) {
        current.slots[i].slotName = e.slots[i].slot;
        current.slots[i].target = e.slots[i].target;
        current.slots[i].displayValue = e.slots[i].value;
        current.slots[i].unit = "";
        current.slots[i].isEmpty = e.slots[i].isEmpty;

        // Hydrate device-global Tempo from slots: target "Tempo" is a global
        // parameter, not per-bank. Without this, device.tempoBpm stays at 120
        // until the user physically turns the Tempo pot, even though the
        // firmware reports the real value in the [STATE] line.
        // Cross-audit 2026-05-15 finding R2.
        if (!e.slots[i].isEmpty && e.slots[i].target == "Tempo") {
            try { device.tempoBpm = std::stoi(e.slots[i].value); } catch (...) {}
        }
    }
    // also fill the current bank's BankInfo scale fields
    if (e.bank >= 1 && e.bank <= 8) {
        banks[e.bank - 1].scaleRoot = e.scaleRoot;
        banks[e.bank - 1].scaleMode = e.scaleMode;
        banks[e.bank - 1].chromatic = e.chromatic;
        banks[e.bank - 1].octave = e.octave;
        banks[e.bank - 1].mutationLevel = e.mutationLevel;
    }
    events.push({std::chrono::steady_clock::now(), "STATE",
                 "Bank " + std::to_string(e.bank) + " " + e.mode});
}

void Model::applyPot(const PotEvent& e) {
    // Update Tempo on DeviceState (it's a global parameter)
    if (e.target == "Tempo") {
        try { device.tempoBpm = std::stoi(e.value); } catch (...) {}
    }
    // Update matching slot in CurrentBankState
    for (int i = 0; i < 8; i++) {
        if (current.slots[i].slotName == e.slot) {
            current.slots[i].target = e.target;
            current.slots[i].displayValue = e.value;
            current.slots[i].unit = e.unit;
            current.slots[i].isEmpty = false;
            current.slots[i].lastChange = std::chrono::steady_clock::now();
            break;
        }
    }
    events.push({std::chrono::steady_clock::now(), "POT",
                 e.slot + " " + e.target + "=" + e.value});
}

void Model::applyBankSwitch(const BankSwitchEvent& e) {
    // The [STATE] that immediately follows will fully populate CurrentBankState.
    // Here we just log the switch.
    events.push({std::chrono::steady_clock::now(), "BANK",
                 "-> B" + std::to_string(e.newBank) + " " + e.mode});
}

void Model::applyScale(const ScaleEvent& e) {
    // Apply to current bank
    if (current.idx >= 1 && current.idx <= 8) {
        auto& b = banks[current.idx - 1];
        b.scaleRoot = e.root;
        b.scaleMode = e.mode;
        b.chromatic = e.chromatic;
        current.scaleRoot = e.root;
        current.scaleMode = e.mode;
        current.chromatic = e.chromatic;

        // Scale group propagation (viewer-side): if current bank has a group,
        // propagate to all banks in the same group.
        if (b.group != '0') {
            for (auto& other : banks) {
                if (other.idx != b.idx && other.group == b.group) {
                    other.scaleRoot = e.root;
                    other.scaleMode = e.mode;
                    other.chromatic = e.chromatic;
                }
            }
        }
    }
    events.push({std::chrono::steady_clock::now(), "SCALE",
                 (e.chromatic ? "Chromatic " : "") + e.root + " " + e.mode});
}

void Model::applyArp(const ArpEvent& e) {
    if (e.action == ArpAction::Play || e.action == ArpAction::PlayRelaunch) {
        if (e.bank >= 1 && e.bank <= 8) banks[e.bank - 1].playing = true;
    } else if (e.action == ArpAction::Stop || e.action == ArpAction::StopCleared) {
        if (e.bank >= 1 && e.bank <= 8) banks[e.bank - 1].playing = false;
    } else if (e.action == ArpAction::OctaveChange) {
        if (current.idx >= 1 && current.idx <= 8 && e.count) {
            banks[current.idx - 1].octave = e.count;
            current.octave = e.count;
        }
    }
    std::string summary;
    switch (e.action) {
        case ArpAction::NoteAdd:     summary = "+note"; break;
        case ArpAction::NoteRemove:  summary = "-note"; break;
        case ArpAction::Play:        summary = "PLAY"; break;
        case ArpAction::PlayRelaunch: summary = "PLAY relaunch"; break;
        case ArpAction::Stop:        summary = "STOP"; break;
        case ArpAction::StopCleared: summary = "STOP cleared"; break;
        case ArpAction::OctaveChange: summary = "Octave " + std::to_string(e.count.value_or(0)); break;
        case ArpAction::QueueFullWarning: summary = "queue full!"; break;
    }
    events.push({std::chrono::steady_clock::now(), "ARP",
                 "B" + std::to_string(e.bank) + " " + summary});
}

void Model::applyArpGen(const ArpGenEvent& e) {
    if (current.idx >= 1 && current.idx <= 8 &&
        banks[current.idx - 1].type == BankType::ARPEG_GEN) {
        banks[current.idx - 1].mutationLevel = e.mutationLevel;
        current.mutationLevel = e.mutationLevel;
    }
    events.push({std::chrono::steady_clock::now(), "GEN",
                 "Mutation " + std::to_string(e.mutationLevel)});
}

void Model::applyGen(const GenEvent& e) {
    events.push({std::chrono::steady_clock::now(), "GEN",
                 "seed seqLen=" + std::to_string(e.seqLen)});
}

void Model::applyClock(const ClockEvent& e) {
    device.clockSource = (e.source == ClockSource::USB) ? ClockSourceEnum::USB
                       : (e.source == ClockSource::BLE) ? ClockSourceEnum::BLE
                       : (e.source == ClockSource::Internal) ? ClockSourceEnum::Internal
                       : ClockSourceEnum::LastKnown;
    if (e.bpm) device.tempoBpm = (int)*e.bpm;
    events.push({std::chrono::steady_clock::now(), "CLOCK",
                 "Source change"});
}

void Model::applyMidi(const MidiEvent& e) {
    if (e.which == MidiTransport::BLE) device.bleMidiConnected = e.connected;
    else                                device.usbMidiConnected = e.connected;
    events.push({std::chrono::steady_clock::now(), "MIDI",
                 std::string(e.which == MidiTransport::BLE ? "BLE " : "USB ") +
                 (e.connected ? "connected" : "disconnected")});
}

void Model::applyPanic(const PanicEvent&) {
    device.lastPanic = std::chrono::steady_clock::now();
    LoggedEvent ev;
    ev.time = device.lastPanic; ev.category = "PANIC"; ev.summary = "All notes off"; ev.isPanic = true;
    events.push(ev);
}
```

- [ ] **Step 3: Add to CMakeLists target sources**

- [ ] **Step 4: Build to verify compiles**

- [ ] **Step 5: Commit**

```bash
git add Source/model/Model.h Source/model/Model.cpp CMakeLists.txt
git commit -m "feat(viewer): Model aggregator dispatches ParsedEvents to state"
```

---

## Phase 4 — Runtime UI (Layer 3)

### Task 14: Wire Layer 0 + Layer 1 + Layer 2 — bytes → parsed events → Model

**Files:**
- Modify: `Source/MainComponent.{h,cpp}` (add AsyncUpdater drain pipeline)

- [ ] **Step 1: Add bridging members**

`MainComponent.h`:
```cpp
#include "serial/SerialReader.h"
#include "serial/LineSplitter.h"
#include "serial/RuntimeParser.h"
#include "model/Model.h"

class MainComponent : public juce::Component, private juce::AsyncUpdater {
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void handleAsyncUpdate() override;

    SerialReader reader;
    LineSplitter splitter;
    Model model;

    juce::CriticalSection pendingLock;
    std::vector<ParsedEvent> pending;
};
```

- [ ] **Step 2: Bridge bytes → lines → events → model (queue + AsyncUpdater)**

`MainComponent.cpp`:
```cpp
MainComponent::MainComponent() {
    setSize(900, 600);

    splitter.setCallback([this](const std::string& line) {
        auto event = parseRuntimeLine(line);
        {
            juce::ScopedLock lock(pendingLock);
            pending.push_back(event);
        }
        triggerAsyncUpdate();
    });

    reader.setBytesCallback([this](const std::vector<uint8_t>& bytes) {
        splitter.feed(bytes);
    });

    reader.connect("");  // auto-detect ILLPAD
    // NOTE: ?BOTH is NOT pushed here. It is pushed by ModeSwitcher::setMode
    // on every transition INTO Runtime (boot first-detection + every
    // reconnect). This avoids polluting the firmware's setup-mode InputParser
    // with stray '?BOTH\n' bytes if the ILLPAD happens to be in setup mode
    // when the viewer starts — InputParser claims the serial during setup
    // and would interpret those bytes as keyboard input.
    // Cross-audit 2026-05-15 findings R3/R4/I1.
}

MainComponent::~MainComponent() {
    reader.stopThread(2000);
}

void MainComponent::handleAsyncUpdate() {
    std::vector<ParsedEvent> local;
    {
        juce::ScopedLock lock(pendingLock);
        local.swap(pending);
    }
    for (const auto& ev : local) {
        model.apply(ev);
    }
}
```

- [ ] **Step 3: Build & verify (still using temp `paint` placeholder)**

Test by tweaking pots / switching banks on ILLPAD. No visible change yet (UI not built), but the Model should be receiving events. Add a temporary `DBG(model.getCurrent().idx)` in `handleAsyncUpdate` to verify.

- [ ] **Step 4: Commit**

```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "feat(viewer): bridge serial bytes -> parser -> Model via AsyncUpdater"
```

---

### Task 15: `HeaderBar` component

**Files:**
- Create: `Source/ui/runtime/HeaderBar.{h,cpp}`

Header zone shows: app name, current port, connection status indicator (●vert/●orange/●rouge), BLE status, Resync button, always-on-top toggle.

- [ ] **Step 1: Define `HeaderBar`**

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/Model.h"
#include "../../serial/SerialReader.h"

class HeaderBar : public juce::Component, public juce::ChangeListener {
public:
    HeaderBar(Model& m, SerialReader& r);
    ~HeaderBar() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    std::function<void()> onResync;
    std::function<void(bool)> onTogglePin;

private:
    Model& model;
    SerialReader& reader;
    juce::TextButton resyncBtn { "Resync" };
    juce::ToggleButton pinBtn { "Pin" };
};
```

- [ ] **Step 2: Implement**

In `HeaderBar.cpp`: register listener in ctor (`model.addChangeListener(this)`), unregister in dtor. Implement `paint` drawing the labels and status dots; `resized` positions the Resync and Pin buttons on the right. Wire `resyncBtn.onClick` to call `onResync()`, `pinBtn.onClick` to call `onTogglePin(pinBtn.getToggleState())`.

```cpp
HeaderBar::HeaderBar(Model& m, SerialReader& r) : model(m), reader(r) {
    model.addChangeListener(this);
    addAndMakeVisible(resyncBtn);
    addAndMakeVisible(pinBtn);
    pinBtn.setButtonText("Pin");
    pinBtn.setToggleState(true, juce::dontSendNotification);
    resyncBtn.onClick = [this]() { if (onResync) onResync(); };
    pinBtn.onClick    = [this]() { if (onTogglePin) onTogglePin(pinBtn.getToggleState()); };
}
HeaderBar::~HeaderBar() { model.removeChangeListener(this); }

void HeaderBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(AppLookAndFeel::BG_SECONDARY));

    g.setColour(juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 14.0f, juce::Font::bold)));
    g.drawText("ILLPAD Viewer", 16, 0, 200, getHeight(), juce::Justification::centredLeft);

    auto port = reader.getCurrentPortName();
    g.setFont(11.0f);
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_SECONDARY));
    g.drawText(port.empty() ? "no port" : port, 200, 0, 250, getHeight(), juce::Justification::centredLeft);

    // status dot
    auto statusCol = reader.isConnected() ? AppLookAndFeel::STATUS_OK : AppLookAndFeel::STATUS_ERROR;
    g.setColour(juce::Colour(statusCol));
    g.fillEllipse(460, getHeight()/2.0f - 5, 10, 10);
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.drawText(reader.isConnected() ? "Connected" : "Disconnected",
               476, 0, 100, getHeight(), juce::Justification::centredLeft);

    // BLE status
    auto bleCol = model.getDevice().bleMidiConnected ? AppLookAndFeel::STATUS_OK : AppLookAndFeel::STATUS_STOP;
    g.setColour(juce::Colour(bleCol));
    g.fillEllipse(600, getHeight()/2.0f - 5, 10, 10);
    g.drawText("BLE", 616, 0, 30, getHeight(), juce::Justification::centredLeft);
}

void HeaderBar::resized() {
    auto r = getLocalBounds().reduced(8);
    pinBtn.setBounds(r.removeFromRight(60));
    resyncBtn.setBounds(r.removeFromRight(70).reduced(2, 0));
}
```

- [ ] **Step 3: Commit**

```bash
git add Source/ui CMakeLists.txt
git commit -m "feat(viewer): HeaderBar component (status + Resync + Pin)"
```

---

### Task 16: `TransportBar` component (with PANIC flash + tempo + clock source)

**Files:**
- Create: `Source/ui/runtime/TransportBar.{h,cpp}`

- [ ] **Step 1: Define interface**

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/Model.h"

class TransportBar : public juce::Component,
                     public juce::ChangeListener,
                     public juce::Timer {
public:
    explicit TransportBar(Model& m);
    ~TransportBar() override;
    void paint(juce::Graphics&) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override { repaint(); }
private:
    Model& model;
};
```

- [ ] **Step 2: Implement**

```cpp
TransportBar::TransportBar(Model& m) : model(m) {
    model.addChangeListener(this);
    startTimerHz(20);  // for PANIC flash decay + visuals
}
TransportBar::~TransportBar() { model.removeChangeListener(this); }

void TransportBar::changeListenerCallback(juce::ChangeBroadcaster*) { repaint(); }

void TransportBar::paint(juce::Graphics& g) {
    bool panic = model.getDevice().isPanicRecent();
    g.fillAll(panic ? juce::Colour(AppLookAndFeel::STATUS_ERROR)
                    : juce::Colour(AppLookAndFeel::BG_SECONDARY));

    g.setColour(panic ? juce::Colours::white : juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 28.0f, juce::Font::bold)));
    auto tempoStr = juce::String::formatted("\xe2\x99\xa9 %d BPM", model.getDevice().tempoBpm);
    g.drawText(tempoStr, 16, 0, 250, getHeight(), juce::Justification::centredLeft);

    g.setFont(13.0f);
    const char* srcName = "?";
    juce::uint32 srcCol = AppLookAndFeel::TEXT_SECONDARY;
    switch (model.getDevice().clockSource) {
        case ClockSourceEnum::USB:      srcName = "USB";      srcCol = AppLookAndFeel::STATUS_OK; break;
        case ClockSourceEnum::BLE:      srcName = "BLE";      srcCol = AppLookAndFeel::STATUS_OK; break;
        case ClockSourceEnum::Internal: srcName = "Internal"; srcCol = AppLookAndFeel::ACCENT;    break;
        case ClockSourceEnum::LastKnown:srcName = "Last";     srcCol = AppLookAndFeel::STATUS_WARN; break;
        default: break;
    }
    g.setColour(juce::Colour(srcCol));
    g.fillEllipse(280, getHeight()/2.0f - 5, 10, 10);
    g.setColour(panic ? juce::Colours::white : juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.drawText(juce::String("Clock: ") + srcName, 296, 0, 200, getHeight(), juce::Justification::centredLeft);

    if (panic) {
        g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 16.0f, juce::Font::bold)));
        g.setColour(juce::Colours::white);
        g.drawText("PANIC - All Notes Off", getWidth() - 280, 0, 270, getHeight(), juce::Justification::centredRight);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add Source/ui/runtime
git commit -m "feat(viewer): TransportBar (tempo, clock source, PANIC flash)"
```

---

### Task 17: `PotCell` component (one of the 8 cadrans)

**Files:**
- Create: `Source/ui/runtime/PotCell.{h,cpp}`

PotCell shows one PotSlot. Header (slot name e.g. "R1"), target (e.g. "Tempo"), value (e.g. "120"). Empty slot → grey "—". Recent change → background flash 300ms.

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/PotSlot.h"

class PotCell : public juce::Component, public juce::Timer {
public:
    PotCell();
    void setSlot(const PotSlot& s);
    void paint(juce::Graphics&) override;
    void timerCallback() override;
private:
    PotSlot slot;
    bool flashActive = false;
};
```

```cpp
PotCell::PotCell() { startTimerHz(20); }

void PotCell::setSlot(const PotSlot& s) {
    bool changed = s.lastChange != slot.lastChange;
    slot = s;
    if (changed) {
        flashActive = true;
        repaint();
    }
}

void PotCell::timerCallback() {
    bool wasActive = flashActive;
    flashActive = slot.isRecentlyChanged();
    if (wasActive != flashActive) repaint();
}

void PotCell::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(2);

    g.setColour(juce::Colour(flashActive ? 0xFF1E3A8A : AppLookAndFeel::BG_SECONDARY));
    g.fillRoundedRectangle(r.toFloat(), 4.0f);

    g.setColour(juce::Colour(AppLookAndFeel::BORDER));
    g.drawRoundedRectangle(r.toFloat(), 4.0f, 1.0f);

    auto inner = r.reduced(12);

    // slot name top-left
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_SECONDARY));
    g.setFont(10.0f);
    g.drawText(slot.slotName, inner.removeFromTop(14), juce::Justification::topLeft);

    // target name
    g.setFont(11.0f);
    g.drawText(slot.isEmpty ? "" : juce::String(slot.target),
               inner.removeFromTop(16), juce::Justification::topLeft);

    // value
    g.setColour(juce::Colour(slot.isEmpty ? AppLookAndFeel::TEXT_SECONDARY : AppLookAndFeel::TEXT_PRIMARY));
    g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 24.0f, juce::Font::bold)));
    g.drawText(slot.isEmpty ? "—" : juce::String(slot.displayValue),
               inner, juce::Justification::centred);
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): PotCell with flash highlight on change"
```

---

### Task 18: `CurrentBankPanel` (bank identity + 8 PotCells grid)

**Files:**
- Create: `Source/ui/runtime/CurrentBankPanel.{h,cpp}`

Component shows the foreground bank (number, mode badge colored by type, channel, scale, octave/mutation level) + 4×2 grid of PotCells.

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/Model.h"
#include "PotCell.h"

class CurrentBankPanel : public juce::Component, public juce::ChangeListener {
public:
    explicit CurrentBankPanel(Model& m);
    ~CurrentBankPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
private:
    Model& model;
    PotCell cells[8];
    juce::uint32 typeColour() const;
};
```

```cpp
CurrentBankPanel::CurrentBankPanel(Model& m) : model(m) {
    for (auto& c : cells) addAndMakeVisible(c);
    model.addChangeListener(this);
}
CurrentBankPanel::~CurrentBankPanel() { model.removeChangeListener(this); }

void CurrentBankPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    const auto& s = model.getCurrent();
    for (int i = 0; i < 8; i++) cells[i].setSlot(s.slots[i]);
    repaint();
}

juce::uint32 CurrentBankPanel::typeColour() const {
    switch (model.getCurrent().type) {
        case BankType::NORMAL:    return AppLookAndFeel::TYPE_NORMAL;
        case BankType::ARPEG:     return AppLookAndFeel::TYPE_ARPEG;
        case BankType::ARPEG_GEN: return AppLookAndFeel::TYPE_ARPEG_GEN;
        case BankType::LOOP:      return AppLookAndFeel::TYPE_LOOP;
        default: return AppLookAndFeel::TEXT_SECONDARY;
    }
}

void CurrentBankPanel::paint(juce::Graphics& g) {
    auto r = getLocalBounds();

    // vertical color bar (3px) on the left
    g.setColour(juce::Colour(typeColour()));
    g.fillRect(r.removeFromLeft(3));

    g.setColour(juce::Colour(AppLookAndFeel::BG_SECONDARY));
    g.fillRect(r);

    auto inner = r.reduced(16, 12);
    auto& s = model.getCurrent();

    // big bank number
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 48.0f, juce::Font::bold)));
    auto headerArea = inner.removeFromTop(56);
    g.drawText("BANK " + juce::String(s.idx),
               headerArea.removeFromLeft(180), juce::Justification::topLeft);

    // mode badge
    auto badge = headerArea.removeFromLeft(120).reduced(0, 8);
    g.setColour(juce::Colour(typeColour()));
    g.fillRoundedRectangle(badge.toFloat(), 3.0f);
    g.setColour(juce::Colours::black);
    g.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 14.0f, juce::Font::bold)));
    g.drawText(juce::String(bankTypeName(s.type)), badge, juce::Justification::centred);

    // channel
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_SECONDARY));
    g.setFont(14.0f);
    g.drawText("ch " + juce::String(s.channel), headerArea, juce::Justification::topLeft);

    inner.removeFromTop(8);
    // scale + octave/mutation
    juce::String scaleStr = s.chromatic ? "Chromatic " + juce::String(s.scaleRoot)
                                        : juce::String(s.scaleRoot) + " " + juce::String(s.scaleMode);
    if (s.octave) scaleStr += "   octave " + juce::String(*s.octave);
    if (s.mutationLevel) scaleStr += "   mutation " + juce::String(*s.mutationLevel);
    g.setFont(13.0f);
    g.drawText(scaleStr, inner.removeFromTop(20), juce::Justification::topLeft);

    inner.removeFromTop(12);
    // cells grid is positioned in resized()
}

void CurrentBankPanel::resized() {
    auto r = getLocalBounds().reduced(19, 0);
    r.removeFromTop(108);  // skip header area
    auto cellH = r.getHeight() / 2;
    auto cellW = r.getWidth() / 4;
    for (int i = 0; i < 4; i++) {
        cells[i].setBounds(r.getX() + i * cellW, r.getY(), cellW, cellH);
        cells[i + 4].setBounds(r.getX() + i * cellW, r.getY() + cellH, cellW, cellH);
    }
    // i: 0=R1, 1=R2, 2=R3, 3=R4 ; i+4: 4=R1H, 5=R2H, 6=R3H, 7=R4H
    // BUT our slot order is R1, R1H, R2, R2H... reorder for display:
    // Display row 1: R1 R2 R3 R4 -> cells[0,2,4,6]
    // Display row 2: R1H R2H R3H R4H -> cells[1,3,5,7]
}
```

Note on slot order: the Model stores slots indexed R1, R1H, R2, R2H, R3, R3H, R4, R4H (positions 0-7). In the visual grid, row 1 = "alone" slots (R1, R2, R3, R4 → indices 0, 2, 4, 6), row 2 = "hold" slots (R1H, R2H, R3H, R4H → indices 1, 3, 5, 7).

Replace `resized()`:
```cpp
void CurrentBankPanel::resized() {
    auto r = getLocalBounds().reduced(19, 0);
    r.removeFromTop(108);
    int cellH = r.getHeight() / 2;
    int cellW = r.getWidth() / 4;
    int aloneIdx[4] = { 0, 2, 4, 6 };  // R1, R2, R3, R4
    int holdIdx[4]  = { 1, 3, 5, 7 };  // R1H, R2H, R3H, R4H
    for (int i = 0; i < 4; i++) {
        cells[aloneIdx[i]].setBounds(r.getX() + i*cellW, r.getY(), cellW, cellH);
        cells[holdIdx[i]].setBounds(r.getX() + i*cellW, r.getY() + cellH, cellW, cellH);
    }
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): CurrentBankPanel (identity bandeau + 8-cell grid)"
```

---

### Task 19: `BankRow` (one bank entry with Play/Stop + tick pulse)

**Files:**
- Create: `Source/ui/runtime/BankRow.{h,cpp}`

Each `BankRow` displays: `▶`/blank marker, bank N, type code (3 letters NRM/ARP/GEN/LOOP), division (if arp), PLAY/STOP text. If PLAY, a green pastille pulses at `tempo × division` interval (local JUCE Timer, not sync'd to firmware).

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/BankInfo.h"
#include "../../model/DeviceState.h"

class BankRow : public juce::Component, public juce::Timer {
public:
    BankRow();
    void setData(const BankInfo& b, bool isCurrent, int tempoBpm);
    void paint(juce::Graphics&) override;
    void timerCallback() override;
private:
    BankInfo info;
    bool current = false;
    int tempo = 120;

    // tick state
    double pulseElapsed = 0.0;
    double pulseIntervalMs = 500.0;
    bool pulseOn = false;

    int divisionMultiplier() const;  // beats per quarter
    void recomputeInterval();
};
```

```cpp
BankRow::BankRow() { startTimerHz(60); }

int BankRow::divisionMultiplier() const {
    // index 0..8 -> beats per quarter ratio
    // 4/1=0.25, 2/1=0.5, 1/1=1, 1/2=2, 1/4=4, 1/8=8, 1/16=16, 1/32=32, 1/64=64
    if (!info.division.has_value()) return 4;
    static const std::pair<const char*, int> tbl[] = {
        {"4/1",0},{"2/1",0},{"1/1",1},{"1/2",2},{"1/4",4},{"1/8",8},{"1/16",16},{"1/32",32},{"1/64",64}
    };
    for (auto& [s,m] : tbl) if (*info.division == s) return m == 0 ? 1 : m;
    return 4;
}

void BankRow::recomputeInterval() {
    // pulse interval (ms) = 60000 / (BPM * divisionsPerBeat)
    double divs = divisionMultiplier();
    pulseIntervalMs = 60000.0 / (tempo * divs);
}

void BankRow::setData(const BankInfo& b, bool isCurrent, int tempoBpm) {
    bool intervalChanged = (b.division != info.division) || (tempo != tempoBpm);
    info = b; current = isCurrent; tempo = tempoBpm;
    if (intervalChanged) recomputeInterval();
    repaint();
}

void BankRow::timerCallback() {
    if (!info.playing) {
        if (pulseOn) { pulseOn = false; repaint(); }
        return;
    }
    pulseElapsed += 1000.0 / 60;
    if (pulseElapsed >= pulseIntervalMs) {
        pulseElapsed = 0;
        pulseOn = true;
        repaint();
    } else if (pulseOn && pulseElapsed > pulseIntervalMs * 0.15) {
        pulseOn = false;
        repaint();
    }
}

void BankRow::paint(juce::Graphics& g) {
    if (current) {
        g.setColour(juce::Colour(AppLookAndFeel::BG_TERTIARY));
        g.fillRect(getLocalBounds());
        // type color bar 3px left
        juce::uint32 col = AppLookAndFeel::TEXT_SECONDARY;
        switch (info.type) {
            case BankType::NORMAL:    col = AppLookAndFeel::TYPE_NORMAL; break;
            case BankType::ARPEG:     col = AppLookAndFeel::TYPE_ARPEG; break;
            case BankType::ARPEG_GEN: col = AppLookAndFeel::TYPE_ARPEG_GEN; break;
            case BankType::LOOP:      col = AppLookAndFeel::TYPE_LOOP; break;
            default: break;
        }
        g.setColour(juce::Colour(col));
        g.fillRect(0, 0, 3, getHeight());
    }

    auto r = getLocalBounds().reduced(8, 2);
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    g.setFont(13.0f);

    // marker
    g.drawText(current ? "\xe2\x96\xb6" : " ", r.removeFromLeft(20), juce::Justification::centred);

    // bank number
    g.drawText(juce::String(info.idx), r.removeFromLeft(20), juce::Justification::centred);

    // type label
    juce::uint32 typeCol = AppLookAndFeel::TEXT_SECONDARY;
    const char* code = "?";
    switch (info.type) {
        case BankType::NORMAL:    code = "NRM";  typeCol = AppLookAndFeel::TYPE_NORMAL; break;
        case BankType::ARPEG:     code = "ARP";  typeCol = AppLookAndFeel::TYPE_ARPEG; break;
        case BankType::ARPEG_GEN: code = "GEN";  typeCol = AppLookAndFeel::TYPE_ARPEG_GEN; break;
        case BankType::LOOP:      code = "LOOP"; typeCol = AppLookAndFeel::TYPE_LOOP; break;
        default: break;
    }
    g.setColour(juce::Colour(typeCol));
    g.drawText(code, r.removeFromLeft(50), juce::Justification::centredLeft);

    // division (arp only)
    g.setColour(juce::Colour(AppLookAndFeel::TEXT_PRIMARY));
    bool isArp = (info.type == BankType::ARPEG || info.type == BankType::ARPEG_GEN);
    if (isArp && info.division) {
        g.drawText(*info.division, r.removeFromLeft(50), juce::Justification::centredLeft);
    } else {
        r.removeFromLeft(50);
    }

    // Play/Stop (arp only)
    if (isArp) {
        auto stateArea = r.removeFromLeft(80);
        if (info.playing) {
            // pulse circle
            if (pulseOn) {
                g.setColour(juce::Colour(AppLookAndFeel::STATUS_OK));
                g.fillEllipse(stateArea.getX() + 2, stateArea.getY() + 4, 10, 10);
            } else {
                g.setColour(juce::Colour(AppLookAndFeel::STATUS_OK).withMultipliedAlpha(0.5f));
                g.fillEllipse(stateArea.getX() + 2, stateArea.getY() + 4, 10, 10);
            }
            g.setColour(juce::Colour(AppLookAndFeel::STATUS_OK));
            g.drawText("PLAY", stateArea.withTrimmedLeft(18), juce::Justification::centredLeft);
        } else {
            g.setColour(juce::Colour(AppLookAndFeel::STATUS_STOP));
            g.drawText("STOP", stateArea.withTrimmedLeft(18), juce::Justification::centredLeft);
        }
    }
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): BankRow with type code, division, Play/Stop, pulse tick"
```

---

### Task 20: `AllBanksPanel` (8 BankRows)

**Files:**
- Create: `Source/ui/runtime/AllBanksPanel.{h,cpp}`

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/Model.h"
#include "BankRow.h"

class AllBanksPanel : public juce::Component, public juce::ChangeListener {
public:
    explicit AllBanksPanel(Model& m);
    ~AllBanksPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
private:
    Model& model;
    BankRow rows[8];
    juce::Label title { {}, "ALL BANKS" };
};
```

```cpp
AllBanksPanel::AllBanksPanel(Model& m) : model(m) {
    addAndMakeVisible(title);
    title.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 12.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colour(AppLookAndFeel::TEXT_SECONDARY));
    for (auto& r : rows) addAndMakeVisible(r);
    model.addChangeListener(this);
}
AllBanksPanel::~AllBanksPanel() { model.removeChangeListener(this); }

void AllBanksPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    int curIdx = model.getCurrent().idx;
    int tempo = model.getDevice().tempoBpm;
    for (int i = 0; i < 8; i++) {
        rows[i].setData(model.getBank(i + 1), (i + 1) == curIdx, tempo);
    }
}

void AllBanksPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(AppLookAndFeel::BG_SECONDARY));
}

void AllBanksPanel::resized() {
    auto r = getLocalBounds().reduced(8);
    title.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);
    int rowH = r.getHeight() / 8;
    for (int i = 0; i < 8; i++) {
        rows[i].setBounds(r.removeFromTop(rowH));
    }
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): AllBanksPanel hosts 8 BankRows"
```

---

### Task 21: `EventLogPanel`

**Files:**
- Create: `Source/ui/runtime/EventLogPanel.{h,cpp}`

Scrolling log of the last ~500 events. Auto-scroll to bottom unless user is scrolling manually. Each line: `HH:MM:SS CATEG summary` in compact monospace.

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../model/Model.h"

class EventLogPanel : public juce::Component, public juce::ChangeListener {
public:
    explicit EventLogPanel(Model& m);
    ~EventLogPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
private:
    Model& model;
    juce::TextEditor logEditor;
    juce::Label title { {}, "EVENTS" };
};
```

```cpp
EventLogPanel::EventLogPanel(Model& m) : model(m) {
    addAndMakeVisible(title);
    title.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 12.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colour(AppLookAndFeel::TEXT_SECONDARY));

    addAndMakeVisible(logEditor);
    logEditor.setMultiLine(true);
    logEditor.setReadOnly(true);
    logEditor.setCaretVisible(false);
    logEditor.setFont(juce::Font(juce::FontOptions("IBM Plex Mono", 11.0f, juce::Font::plain)));
    logEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(AppLookAndFeel::BG_PRIMARY));
    logEditor.setColour(juce::TextEditor::textColourId, juce::Colour(AppLookAndFeel::TEXT_PRIMARY));

    model.addChangeListener(this);
}
EventLogPanel::~EventLogPanel() { model.removeChangeListener(this); }

void EventLogPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    juce::String text;
    auto epoch = std::chrono::steady_clock::now();
    for (auto& e : model.getEvents().getAll()) {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(e.time.time_since_epoch()).count();
        auto h = (secs / 3600) % 24;
        auto m = (secs / 60) % 60;
        auto s = secs % 60;
        text += juce::String::formatted("%02ld:%02ld:%02ld  %-7s %s\n",
                                        (long)h, (long)m, (long)s,
                                        e.category.c_str(), e.summary.c_str());
    }
    bool wasAtBottom = logEditor.getCaretPosition() >= logEditor.getText().length() - 5;
    logEditor.setText(text, juce::dontSendNotification);
    if (wasAtBottom) logEditor.moveCaretToEnd();
}

void EventLogPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(AppLookAndFeel::BG_SECONDARY));
}

void EventLogPanel::resized() {
    auto r = getLocalBounds().reduced(8);
    title.setBounds(r.removeFromTop(20));
    r.removeFromTop(4);
    logEditor.setBounds(r);
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): EventLogPanel scrolling log of recent events"
```

---

### Task 22: `RuntimeMode` — compose runtime UI panels

**Files:**
- Create: `Source/ui/runtime/RuntimeMode.{h,cpp}`

```cpp
#pragma once
#include <JuceHeader.h>
#include "HeaderBar.h"
#include "TransportBar.h"
#include "CurrentBankPanel.h"
#include "AllBanksPanel.h"
#include "EventLogPanel.h"
#include "../../model/Model.h"
#include "../../serial/SerialReader.h"

class RuntimeMode : public juce::Component {
public:
    RuntimeMode(Model& m, SerialReader& r);
    void resized() override;
    HeaderBar& getHeader() { return header; }
private:
    HeaderBar header;
    TransportBar transport;
    CurrentBankPanel currentBank;
    AllBanksPanel allBanks;
    EventLogPanel events;
};
```

```cpp
RuntimeMode::RuntimeMode(Model& m, SerialReader& r)
    : header(m, r), transport(m), currentBank(m), allBanks(m), events(m) {
    addAndMakeVisible(header);
    addAndMakeVisible(transport);
    addAndMakeVisible(currentBank);
    addAndMakeVisible(allBanks);
    addAndMakeVisible(events);
}

void RuntimeMode::resized() {
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(44));
    transport.setBounds(r.removeFromTop(48));

    auto rightCol = r.removeFromRight(r.getWidth() / 3);
    events.setBounds(rightCol.removeFromBottom(rightCol.getHeight() * 5 / 12));
    allBanks.setBounds(rightCol);

    currentBank.setBounds(r);
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): RuntimeMode composes runtime UI"
```

---

## Phase 5 — App integration

### Task 23: Wire `RuntimeMode` into `MainComponent` + always-on-top + Resync

**Files:**
- Modify: `Source/MainComponent.{h,cpp}` (replace placeholder paint with RuntimeMode)
- Modify: `Source/Main.cpp` (wire pin toggle to MainWindow.setAlwaysOnTop)

- [ ] **Step 1: Replace placeholder in MainComponent**

```cpp
// MainComponent.h additions
#include "ui/runtime/RuntimeMode.h"
private:
    std::unique_ptr<RuntimeMode> runtimeMode;

// MainComponent.cpp ctor (after model init)
runtimeMode = std::make_unique<RuntimeMode>(model, reader);
addAndMakeVisible(*runtimeMode);

runtimeMode->getHeader().onResync = [this]() {
    reader.getOutputQueue().push({'?','B','O','T','H','\n'});
};
runtimeMode->getHeader().onTogglePin = [this](bool pin) {
    if (auto* tlw = getTopLevelComponent()) {
        if (auto* dw = dynamic_cast<juce::DocumentWindow*>(tlw)) dw->setAlwaysOnTop(pin);
    }
};

// MainComponent::resized()
runtimeMode->setBounds(getLocalBounds());

// MainComponent::paint() — remove placeholder text drawing
```

- [ ] **Step 2: Build, plug ILLPAD, run**

Expected: app shows header, transport bar, current bank panel (populated), all banks list, events log. Tweak pots / switch banks → UI updates live.

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(viewer): wire RuntimeMode into MainComponent + Resync + Pin"
```

---

### Task 24: Persistence — `PropertiesFile` for port, window pos, always-on-top

**Files:**
- Create: `Source/AppSettings.{h,cpp}`
- Modify: `Source/Main.cpp` (load on init, save on exit)
- Modify: `Source/MainComponent.cpp` (use stored port if available)

- [ ] **Step 1: Define `AppSettings`**

```cpp
#pragma once
#include <JuceHeader.h>

class AppSettings {
public:
    static AppSettings& instance() { static AppSettings s; return s; }

    juce::String lastSerialPort;
    int windowX = -1, windowY = -1;
    int windowWidth = 900, windowHeight = 600;
    bool alwaysOnTop = true;
    bool firstRun = true;

    void load();
    void save();

private:
    AppSettings() = default;
    juce::File settingsFile() const;
};
```

- [ ] **Step 2: Implement**

```cpp
#include "AppSettings.h"

juce::File AppSettings::settingsFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("ILLPADViewer")
            .getChildFile("state.xml");
}

void AppSettings::load() {
    auto file = settingsFile();
    if (!file.existsAsFile()) return;
    auto xml = juce::parseXML(file);
    if (!xml) return;
    lastSerialPort = xml->getStringAttribute("lastSerialPort", "");
    windowX        = xml->getIntAttribute("windowX", -1);
    windowY        = xml->getIntAttribute("windowY", -1);
    windowWidth    = xml->getIntAttribute("windowWidth", 900);
    windowHeight   = xml->getIntAttribute("windowHeight", 600);
    alwaysOnTop    = xml->getBoolAttribute("alwaysOnTop", true);
    firstRun       = xml->getBoolAttribute("firstRun", true);
}

void AppSettings::save() {
    auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    juce::XmlElement xml("ILLPADViewerSettings");
    xml.setAttribute("lastSerialPort", lastSerialPort);
    xml.setAttribute("windowX", windowX);
    xml.setAttribute("windowY", windowY);
    xml.setAttribute("windowWidth", windowWidth);
    xml.setAttribute("windowHeight", windowHeight);
    xml.setAttribute("alwaysOnTop", alwaysOnTop);
    xml.setAttribute("firstRun", firstRun);
    xml.writeTo(file);
}
```

- [ ] **Step 3: Load in Main, save on quit, apply to MainWindow**

In `ILLPADViewerApp::initialise`:
```cpp
AppSettings::instance().load();
laf = std::make_unique<AppLookAndFeel>();
juce::LookAndFeel::setDefaultLookAndFeel(laf.get());
mainWindow = std::make_unique<MainWindow>("ILLPAD Viewer", new MainComponent(), *this);

auto& s = AppSettings::instance();
if (s.windowX >= 0) mainWindow->setTopLeftPosition(s.windowX, s.windowY);
mainWindow->setSize(s.windowWidth, s.windowHeight);
mainWindow->setAlwaysOnTop(s.alwaysOnTop);
```

In `ILLPADViewerApp::shutdown`:
```cpp
if (mainWindow) {
    auto& s = AppSettings::instance();
    s.windowX = mainWindow->getX();
    s.windowY = mainWindow->getY();
    s.windowWidth = mainWindow->getWidth();
    s.windowHeight = mainWindow->getHeight();
    s.save();
}
mainWindow = nullptr;
```

- [ ] **Step 4: In MainComponent ctor, use stored port if any**

```cpp
auto& settings = AppSettings::instance();
auto storedPort = settings.lastSerialPort.toStdString();
reader.connect(storedPort);  // empty = auto-detect; otherwise try this port first
```

Update SerialReader::run to fall back to auto-detect if the stored port name is not currently present in the list.

In Header `onTogglePin` callback, also update `AppSettings::instance().alwaysOnTop`. On successful first connect, save the active port name:
```cpp
// in SerialReader::run after successful openPort:
AppSettings::instance().lastSerialPort = target;
```

- [ ] **Step 5: Commit**

```bash
git add Source/AppSettings.h Source/AppSettings.cpp Source/Main.cpp Source/MainComponent.cpp
git commit -m "feat(viewer): persist port + window position + always-on-top"
```

---

## Phase 6 — Setup mode (VT100 emulator)

### Task 25: `ITermSeqInterceptor` + tests

**Files:**
- Create: `Source/serial/ITermSeqInterceptor.{h,cpp}`
- Create: `Tests/test_ITermSeqInterceptor.cpp`

Intercepts 3 iTerm-specific sequences before passing bytes to libvterm:
- `ESC[?2026h` / `ESC[?2026l` → BeginSync / EndSync
- `CSI 8 ; rows ; cols t` → Resize
- `OSC 1337 ; ProgressBar=end ST` → ProgressEnd (ignored)

- [ ] **Step 1: Define interface + variant of intercepted events**

`ITermSeqInterceptor.h`:
```cpp
#pragma once
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <functional>

struct BeginSync {};
struct EndSync {};
struct ResizeRequest { int rows; int cols; };
struct ProgressEnd {};

using ITermSeqEvent = std::variant<BeginSync, EndSync, ResizeRequest, ProgressEnd>;

class ITermSeqInterceptor {
public:
    using EventCallback = std::function<void(const ITermSeqEvent&)>;
    using PassthroughCallback = std::function<void(const uint8_t*, size_t)>;

    void setEventCallback(EventCallback cb)        { eventCb = std::move(cb); }
    void setPassthroughCallback(PassthroughCallback cb) { passCb = std::move(cb); }

    // Feed input bytes. Sequences are intercepted; everything else is passed through.
    void feed(const uint8_t* data, size_t len);

    // For testing — drain pending pass-through bytes
    std::vector<uint8_t> takeForwarded();
private:
    std::string buffer;
    EventCallback eventCb;
    PassthroughCallback passCb;
    std::vector<uint8_t> testForward;
};
```

- [ ] **Step 2: Failing tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "serial/ITermSeqInterceptor.h"

TEST_CASE("ITermSeqInterceptor passes through normal bytes", "[iterm]") {
    ITermSeqInterceptor ip;
    int events = 0;
    ip.setEventCallback([&](const ITermSeqEvent&){ events++; });
    const char* hello = "Hello\n";
    ip.feed((const uint8_t*)hello, 6);
    auto forwarded = ip.takeForwarded();
    REQUIRE(forwarded.size() == 6);
    REQUIRE(events == 0);
}

TEST_CASE("ITermSeqInterceptor catches DEC 2026 begin/end", "[iterm]") {
    ITermSeqInterceptor ip;
    std::vector<ITermSeqEvent> evts;
    ip.setEventCallback([&](const ITermSeqEvent& e){ evts.push_back(e); });
    std::string s = "\x1B[?2026h\x1B[?2026l";
    ip.feed((const uint8_t*)s.data(), s.size());
    REQUIRE(evts.size() == 2);
    REQUIRE(std::holds_alternative<BeginSync>(evts[0]));
    REQUIRE(std::holds_alternative<EndSync>(evts[1]));
}

TEST_CASE("ITermSeqInterceptor catches CSI 8 t resize", "[iterm]") {
    ITermSeqInterceptor ip;
    std::vector<ITermSeqEvent> evts;
    ip.setEventCallback([&](const ITermSeqEvent& e){ evts.push_back(e); });
    std::string s = "\x1B[8;50;120t";
    ip.feed((const uint8_t*)s.data(), s.size());
    REQUIRE(evts.size() == 1);
    auto* r = std::get_if<ResizeRequest>(&evts[0]);
    REQUIRE(r != nullptr);
    REQUIRE(r->rows == 50);
    REQUIRE(r->cols == 120);
}
```

- [ ] **Step 3: Implement**

```cpp
#include "ITermSeqInterceptor.h"
#include <cstdio>
#include <cstring>

static bool tryParseAndConsume(std::string& buf, ITermSeqInterceptor::EventCallback& cb) {
    if (buf.empty() || buf[0] != 0x1B) return false;
    if (buf.size() < 2) return true;  // need more bytes

    // CSI: ESC [
    if (buf[1] == '[') {
        // find end byte (alpha or '~')
        size_t end = 2;
        while (end < buf.size() && !((buf[end] >= 0x40 && buf[end] <= 0x7E) && buf[end] != ':')) end++;
        if (end == buf.size()) return true;  // need more bytes
        std::string seq = buf.substr(0, end + 1);

        // ESC [?2026h or l
        if (seq == "\x1B[?2026h") { if (cb) cb(BeginSync{}); buf.erase(0, end + 1); return true; }
        if (seq == "\x1B[?2026l") { if (cb) cb(EndSync{});   buf.erase(0, end + 1); return true; }

        // ESC [8;rows;cols t
        int r, c;
        if (sscanf(seq.c_str(), "\x1B[8;%d;%dt", &r, &c) == 2) {
            if (cb) cb(ResizeRequest{r, c});
            buf.erase(0, end + 1);
            return true;
        }
        // not intercepted: leave the whole seq in buffer (will be forwarded)
        return false;
    }

    // OSC: ESC ]
    if (buf[1] == ']') {
        // find ST (\x07 or ESC \)
        size_t end = 2;
        while (end < buf.size() && buf[end] != 0x07 &&
               !(buf[end] == 0x1B && end + 1 < buf.size() && buf[end+1] == '\\')) end++;
        if (end == buf.size()) return true;
        size_t termLen = (buf[end] == 0x07) ? 1 : 2;
        std::string seq = buf.substr(0, end + termLen);
        if (seq.find("1337;ProgressBar=end") != std::string::npos) {
            if (cb) cb(ProgressEnd{});
            buf.erase(0, end + termLen);
            return true;
        }
        return false;
    }
    return false;
}

void ITermSeqInterceptor::feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0x1B) {
            // start of escape; flush any pending normal bytes first
            // (but only if buffer is currently empty — otherwise we're mid-escape)
        }
        buffer += (char)data[i];

        // Try to consume intercepted sequences from the start of buffer
        while (!buffer.empty()) {
            if (buffer[0] != 0x1B) {
                if (passCb) passCb((const uint8_t*)&buffer[0], 1);
                testForward.push_back((uint8_t)buffer[0]);
                buffer.erase(0, 1);
                continue;
            }
            // we have an escape at [0]; try parse
            bool consumed = tryParseAndConsume(buffer, eventCb);
            if (consumed) {
                // either fully consumed an intercepted seq (buffer modified) or need more bytes
                // if buffer still starts with ESC and parseAndConsume returned true, it means
                // we're waiting for more bytes — break out
                if (!buffer.empty() && buffer[0] == 0x1B) break;
                continue;
            } else {
                // Not an intercepted sequence — forward the whole sequence as-is
                // To find seq length: parse minimally
                if (buffer.size() < 2) break;  // need more bytes
                size_t seqLen = buffer.size();
                if (buffer[1] == '[') {
                    size_t end = 2;
                    while (end < buffer.size() && !((buffer[end] >= 0x40 && buffer[end] <= 0x7E) && buffer[end] != ':')) end++;
                    if (end == buffer.size()) break;
                    seqLen = end + 1;
                } else if (buffer[1] == ']') {
                    size_t end = 2;
                    while (end < buffer.size() && buffer[end] != 0x07 &&
                           !(buffer[end] == 0x1B && end + 1 < buffer.size() && buffer[end+1] == '\\')) end++;
                    if (end == buffer.size()) break;
                    seqLen = end + ((buffer[end] == 0x07) ? 1 : 2);
                } else {
                    seqLen = 2;
                }
                if (passCb) passCb((const uint8_t*)&buffer[0], seqLen);
                for (size_t k = 0; k < seqLen; k++) testForward.push_back((uint8_t)buffer[k]);
                buffer.erase(0, seqLen);
            }
        }
    }
}

std::vector<uint8_t> ITermSeqInterceptor::takeForwarded() {
    auto r = std::move(testForward);
    testForward.clear();
    return r;
}
```

- [ ] **Step 4: Run tests (expect ALL PASS)**

- [ ] **Step 5: Commit**

```bash
git commit -am "feat(viewer): ITermSeqInterceptor pre-parses DEC 2026 + CSI 8 t + OSC 1337"
```

---

### Task 26: `TerminalDriver` (libvterm wrapper)

**Files:**
- Create: `Source/serial/TerminalDriver.{h,cpp}`
- Create: `Source/model/TerminalModel.{h,cpp}`

TerminalDriver owns a `VTerm*` + `VTermScreen*`, exposes a `feed(bytes)` method and a screen accessor. Damage tracking via libvterm callbacks notifies the TerminalView to repaint.

```cpp
// TerminalDriver.h
#pragma once
#include <functional>
#include <vector>
struct VTerm;
struct VTermScreen;
struct VTermRect;
struct VTermPos;

class TerminalDriver {
public:
    using DamageCallback = std::function<void(int row0, int col0, int row1, int col1)>;
    using CursorCallback = std::function<void(int row, int col)>;
    using ResponseCallback = std::function<void(const std::vector<uint8_t>&)>;

    TerminalDriver();
    ~TerminalDriver();

    void initialize(int rows, int cols);
    void resize(int rows, int cols);
    void feed(const uint8_t* data, size_t len);
    void getCellTextAndAttrs(int row, int col, /*out*/ char32_t& ch, /*out*/ unsigned& fg, /*out*/ unsigned& bg, /*out*/ bool& bold) const;

    int getRows() const;
    int getCols() const;

    void setDamageCallback(DamageCallback cb) { damageCb = std::move(cb); }
    void setCursorCallback(CursorCallback cb) { cursorCb = std::move(cb); }
    void setResponseCallback(ResponseCallback cb) { responseCb = std::move(cb); }
private:
    VTerm* vt = nullptr;
    VTermScreen* scr = nullptr;
    int rows = 50, cols = 120;
    DamageCallback damageCb;
    CursorCallback cursorCb;
    ResponseCallback responseCb;

    static int cb_damage(VTermRect rect, void* user);
    static int cb_movecursor(VTermPos pos, VTermPos oldPos, int visible, void* user);
    static int cb_settermprop(int prop, void* val, void* user);
    static void cb_output(const char* s, size_t len, void* user);
};
```

```cpp
// TerminalDriver.cpp
#include "TerminalDriver.h"
#include <vterm.h>

TerminalDriver::TerminalDriver() {}
TerminalDriver::~TerminalDriver() { if (vt) vterm_free(vt); }

void TerminalDriver::initialize(int r, int c) {
    rows = r; cols = c;
    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    scr = vterm_obtain_screen(vt);

    static VTermScreenCallbacks cbs = {};
    cbs.damage = &TerminalDriver::cb_damage;
    cbs.movecursor = &TerminalDriver::cb_movecursor;
    cbs.settermprop = nullptr;  // optional
    vterm_screen_set_callbacks(scr, &cbs, this);
    vterm_screen_reset(scr, 1);

    // capture vterm's outgoing bytes (e.g., responses to queries)
    vterm_output_set_callback(vt, &TerminalDriver::cb_output, this);
}

void TerminalDriver::resize(int r, int c) {
    if (!vt) { initialize(r, c); return; }
    if (r == rows && c == cols) return;
    rows = r; cols = c;
    vterm_set_size(vt, rows, cols);
}

void TerminalDriver::feed(const uint8_t* data, size_t len) {
    if (vt) vterm_input_write(vt, (const char*)data, len);
}

int TerminalDriver::getRows() const { return rows; }
int TerminalDriver::getCols() const { return cols; }

void TerminalDriver::getCellTextAndAttrs(int r, int c, char32_t& ch, unsigned& fg, unsigned& bg, bool& bold) const {
    VTermScreenCell cell;
    VTermPos pos{r, c};
    if (!scr) { ch = ' '; fg = 0xFFE6EDF3; bg = 0xFF000000; bold = false; return; }
    vterm_screen_get_cell(scr, pos, &cell);
    ch = (cell.chars[0] ? cell.chars[0] : ' ');
    bold = cell.attrs.bold;
    fg = 0xFFE6EDF3;
    bg = 0xFF000000;
    if (VTERM_COLOR_IS_RGB(&cell.fg)) fg = 0xFF000000 | (cell.fg.rgb.red << 16) | (cell.fg.rgb.green << 8) | cell.fg.rgb.blue;
    if (VTERM_COLOR_IS_RGB(&cell.bg)) bg = 0xFF000000 | (cell.bg.rgb.red << 16) | (cell.bg.rgb.green << 8) | cell.bg.rgb.blue;
}

int TerminalDriver::cb_damage(VTermRect rect, void* user) {
    auto self = static_cast<TerminalDriver*>(user);
    if (self->damageCb) self->damageCb(rect.start_row, rect.start_col, rect.end_row, rect.end_col);
    return 1;
}

int TerminalDriver::cb_movecursor(VTermPos pos, VTermPos, int, void* user) {
    auto self = static_cast<TerminalDriver*>(user);
    if (self->cursorCb) self->cursorCb(pos.row, pos.col);
    return 1;
}

void TerminalDriver::cb_output(const char* s, size_t len, void* user) {
    auto self = static_cast<TerminalDriver*>(user);
    if (self->responseCb) {
        std::vector<uint8_t> v(s, s + len);
        self->responseCb(v);
    }
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): TerminalDriver wraps libvterm + callbacks for damage/cursor/output"
```

---

### Task 27: `TerminalView` — render cells

**Files:**
- Create: `Source/ui/setup/TerminalView.{h,cpp}`

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../serial/TerminalDriver.h"

class TerminalView : public juce::Component {
public:
    explicit TerminalView(TerminalDriver& d);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override { return false; }
    void focusGained(juce::Component::FocusChangeType) override { repaint(); }
    void focusLost(juce::Component::FocusChangeType) override { repaint(); }

    int cursorRow = 0, cursorCol = 0;
    bool cursorVisible = true;

    std::function<void(const std::vector<uint8_t>&)> onKeysOut;

private:
    TerminalDriver& driver;
};
```

```cpp
TerminalView::TerminalView(TerminalDriver& d) : driver(d) {
    setWantsKeyboardFocus(true);
}

void TerminalView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colours::black);

    int rows = driver.getRows();
    int cols = driver.getCols();
    float cellW = bounds.getWidth() / (float)cols;
    float cellH = bounds.getHeight() / (float)rows;

    auto font = juce::Font(juce::FontOptions("IBM Plex Mono", cellH * 0.85f, juce::Font::plain));
    g.setFont(font);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            char32_t ch; unsigned fg, bg; bool bold;
            driver.getCellTextAndAttrs(r, c, ch, fg, bg, bold);
            juce::Rectangle<float> rect(c * cellW, r * cellH, cellW, cellH);

            // background
            if (bg != 0xFF000000) {
                g.setColour(juce::Colour(bg));
                g.fillRect(rect);
            }

            // text
            if (ch != ' ' && ch != 0) {
                g.setColour(juce::Colour(fg));
                if (bold) g.setFont(font.boldened());
                juce::String s = juce::String::charToString((juce::juce_wchar)ch);
                g.drawText(s, rect, juce::Justification::centred, false);
                if (bold) g.setFont(font);
            }
        }
    }

    // cursor
    if (cursorVisible) {
        juce::Rectangle<float> cur(cursorCol * cellW, cursorRow * cellH, cellW, cellH);
        g.setColour(juce::Colours::white.withAlpha(hasKeyboardFocus(true) ? 1.0f : 0.3f));
        if (hasKeyboardFocus(true)) g.fillRect(cur);
        else                        g.drawRect(cur, 1.0f);
    }
}

void TerminalView::resized() {
    // libvterm grid size remains (driver.rows × driver.cols).
    // Cells scale to fit. No call to driver.resize() here — that's triggered by
    // ITermSeqInterceptor's ResizeRequest in MainComponent.
    repaint();
}

bool TerminalView::keyPressed(const juce::KeyPress& key) {
    if (!onKeysOut) return false;
    std::vector<uint8_t> bytes;
    if (key == juce::KeyPress::upKey)    bytes = {0x1B, '[', 'A'};
    else if (key == juce::KeyPress::downKey) bytes = {0x1B, '[', 'B'};
    else if (key == juce::KeyPress::rightKey) bytes = {0x1B, '[', 'C'};
    else if (key == juce::KeyPress::leftKey)  bytes = {0x1B, '[', 'D'};
    else if (key == juce::KeyPress::returnKey) bytes = {0x0D};
    else if (key == juce::KeyPress::backspaceKey) bytes = {0x7F};
    else if (key == juce::KeyPress::tabKey) bytes = {0x09};
    else if (key == juce::KeyPress::escapeKey) bytes = {0x1B};
    else {
        auto ch = key.getTextCharacter();
        if (ch >= 32 && ch < 127) bytes = {(uint8_t)ch};
        else return false;
    }
    onKeysOut(bytes);
    return true;
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): TerminalView renders libvterm screen + keyboard input"
```

---

### Task 28: `SetupMode` + integrate Driver + Interceptor

**Files:**
- Create: `Source/ui/setup/SetupMode.{h,cpp}`

SetupMode wires together TerminalDriver, ITermSeqInterceptor, and TerminalView; provides a callback for outgoing bytes (keystrokes + libvterm responses).

```cpp
#pragma once
#include <JuceHeader.h>
#include "../../serial/TerminalDriver.h"
#include "../../serial/ITermSeqInterceptor.h"
#include "TerminalView.h"

class SetupMode : public juce::Component, public juce::Timer {
public:
    SetupMode();
    void resized() override;
    void timerCallback() override;

    void feedBytes(const std::vector<uint8_t>& bytes);
    std::function<void(const std::vector<uint8_t>&)> onBytesOut;
private:
    TerminalDriver driver;
    ITermSeqInterceptor interceptor;
    TerminalView view { driver };

    bool inSync = false;
    std::vector<uint8_t> syncBuffer;
    std::atomic<bool> needRepaint{false};
};
```

```cpp
SetupMode::SetupMode() {
    driver.initialize(50, 120);
    addAndMakeVisible(view);

    interceptor.setPassthroughCallback([this](const uint8_t* d, size_t n) {
        if (inSync) syncBuffer.insert(syncBuffer.end(), d, d + n);
        else        driver.feed(d, n);
    });
    interceptor.setEventCallback([this](const ITermSeqEvent& e) {
        if (std::holds_alternative<BeginSync>(e)) { inSync = true; syncBuffer.clear(); }
        else if (std::holds_alternative<EndSync>(e)) {
            inSync = false;
            if (!syncBuffer.empty()) driver.feed(syncBuffer.data(), syncBuffer.size());
            syncBuffer.clear();
            needRepaint.store(true);
        } else if (auto rr = std::get_if<ResizeRequest>(&e)) {
            // resize libvterm grid only — window stays user-controlled
            juce::MessageManager::callAsync([this, r = rr->rows, c = rr->cols]() {
                driver.resize(r, c);
                view.repaint();
            });
        }
    });

    driver.setDamageCallback([this](int, int, int, int) { needRepaint.store(true); });
    driver.setCursorCallback([this](int r, int c) {
        view.cursorRow = r; view.cursorCol = c; needRepaint.store(true);
    });
    driver.setResponseCallback([this](const std::vector<uint8_t>& bytes) {
        if (onBytesOut) onBytesOut(bytes);
    });

    view.onKeysOut = [this](const std::vector<uint8_t>& bytes) {
        if (onBytesOut) onBytesOut(bytes);
    };

    startTimerHz(30);
}

void SetupMode::resized() {
    view.setBounds(getLocalBounds().reduced(12));
}

void SetupMode::timerCallback() {
    if (needRepaint.exchange(false)) view.repaint();
}

void SetupMode::feedBytes(const std::vector<uint8_t>& bytes) {
    interceptor.feed(bytes.data(), bytes.size());
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): SetupMode wires libvterm + interceptor + view"
```

---

### Task 29: `ModeDetector` + tests

**Files:**
- Create: `Source/model/ModeDetector.{h,cpp}`
- Create: `Tests/test_ModeDetector.cpp`

State machine UNKNOWN → RUNTIME or SETUP. Resets on reconnect.

```cpp
#pragma once
#include <string>

enum class AppMode { Unknown, Runtime, Setup };

class ModeDetector {
public:
    void feed(const uint8_t* data, size_t len);
    void onReconnect() { mode = AppMode::Unknown; lineBuffer.clear(); }
    AppMode getMode() const { return mode; }
private:
    AppMode mode = AppMode::Unknown;
    std::string lineBuffer;
};
```

```cpp
#include "ModeDetector.h"
#include <regex>

void ModeDetector::feed(const uint8_t* data, size_t len) {
    if (mode != AppMode::Unknown) return;  // locked

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        // detect ANSI CSI/OSC
        if (b == 0x1B) {
            if (i + 1 < len && (data[i + 1] == '[' || data[i + 1] == ']')) {
                mode = AppMode::Setup;
                return;
            }
        }
        if (b == '\n') {
            // check if buffer matches [XXX_XXX]
            std::regex re("^\\[[A-Z_]+\\]");
            if (std::regex_search(lineBuffer, re)) {
                mode = AppMode::Runtime;
                return;
            }
            lineBuffer.clear();
        } else {
            lineBuffer += (char)b;
            if (lineBuffer.size() > 256) lineBuffer.clear();
        }
    }
}
```

Tests:
```cpp
TEST_CASE("ModeDetector: ANSI -> Setup", "[mode]") {
    ModeDetector md;
    const char* s = "\x1B[2J";
    md.feed((const uint8_t*)s, 4);
    REQUIRE(md.getMode() == AppMode::Setup);
}
TEST_CASE("ModeDetector: tagged line -> Runtime", "[mode]") {
    ModeDetector md;
    const char* s = "[BANKS] count=8\n";
    md.feed((const uint8_t*)s, strlen(s));
    REQUIRE(md.getMode() == AppMode::Runtime);
}
TEST_CASE("ModeDetector: reset on reconnect", "[mode]") {
    ModeDetector md;
    const char* s = "[INIT] Ready.\n";
    md.feed((const uint8_t*)s, strlen(s));
    REQUIRE(md.getMode() == AppMode::Runtime);
    md.onReconnect();
    REQUIRE(md.getMode() == AppMode::Unknown);
}
```

- [ ] **Commit**

```bash
git commit -am "feat(viewer): ModeDetector + Catch2 tests"
```

---

### Task 30: `ModeSwitcher` — compose runtime + setup with auto-bascule

**Files:**
- Create: `Source/ui/ModeSwitcher.{h,cpp}`
- Modify: `Source/MainComponent.{h,cpp}` (route bytes via ModeDetector, swap UIs)

```cpp
#pragma once
#include <JuceHeader.h>
#include "runtime/RuntimeMode.h"
#include "setup/SetupMode.h"
#include "../model/ModeDetector.h"

class ModeSwitcher : public juce::Component {
public:
    ModeSwitcher(Model& model, SerialReader& reader);

    AppMode getMode() const { return currentMode; }
    void setMode(AppMode m);

    void feedBytes(const std::vector<uint8_t>& bytes);

    RuntimeMode& getRuntime() { return runtimeMode; }
    SetupMode& getSetup()     { return setupMode; }

    void resized() override;
private:
    Model& model;
    SerialReader& reader;
    ModeDetector detector;

    RuntimeMode runtimeMode;
    SetupMode setupMode;

    AppMode currentMode = AppMode::Unknown;
};
```

```cpp
ModeSwitcher::ModeSwitcher(Model& m, SerialReader& r)
    : model(m), reader(r), runtimeMode(m, r) {
    addAndMakeVisible(runtimeMode);
    setupMode.setVisible(false);
    addChildComponent(setupMode);

    setupMode.onBytesOut = [this](const std::vector<uint8_t>& bytes) {
        reader.getOutputQueue().push(bytes);
    };
}

void ModeSwitcher::setMode(AppMode m) {
    if (m == currentMode) return;
    AppMode prev = currentMode;
    currentMode = m;
    runtimeMode.setVisible(m == AppMode::Runtime);
    setupMode.setVisible(m == AppMode::Setup);
    if (m == AppMode::Setup) setupMode.grabKeyboardFocus();
    resized();

    // On any transition INTO Runtime (initial boot detection OR reconnect
    // after disconnect/setup), push ?BOTH to re-hydrate the Model with a
    // full atomic snapshot. Without this, the viewer would keep stale data
    // after a replug, or miss the initial dump if the ILLPAD booted before
    // the viewer connected (USB CDC buffer not guaranteed to retain history).
    // Gated on (prev != Runtime) to avoid spurious double-pushes; gated on
    // (m == Runtime) so we never inject ?BOTH bytes into the firmware setup
    // mode InputParser (which claims the serial during setup).
    // Cross-audit 2026-05-15 findings R3/R4/I1.
    if (m == AppMode::Runtime && prev != AppMode::Runtime) {
        reader.getOutputQueue().push({'?','B','O','T','H','\n'});
    }
}

void ModeSwitcher::feedBytes(const std::vector<uint8_t>& bytes) {
    detector.feed(bytes.data(), bytes.size());
    if (detector.getMode() != currentMode) {
        setMode(detector.getMode());
    }
    if (currentMode == AppMode::Setup) {
        setupMode.feedBytes(bytes);
    }
    // Runtime bytes already flow via splitter -> parser -> Model
}

void ModeSwitcher::resized() {
    runtimeMode.setBounds(getLocalBounds());
    setupMode.setBounds(getLocalBounds());
}
```

Modify MainComponent to use ModeSwitcher in lieu of RuntimeMode directly:
```cpp
// MainComponent.h
#include "ui/ModeSwitcher.h"
private:
    std::unique_ptr<ModeSwitcher> modeSwitcher;

// MainComponent.cpp ctor (after model init)
modeSwitcher = std::make_unique<ModeSwitcher>(model, reader);
addAndMakeVisible(*modeSwitcher);

reader.setBytesCallback([this](const std::vector<uint8_t>& bytes) {
    modeSwitcher->feedBytes(bytes);
    if (modeSwitcher->getMode() == AppMode::Runtime) splitter.feed(bytes);
});

// MainComponent::resized()
modeSwitcher->setBounds(getLocalBounds());

// SerialReader reconnect -> reset ModeDetector
// In SerialReader on successful open, fire a callback that ModeSwitcher can hook to reset detector.
```

Add to SerialReader a `setReconnectCallback`:
```cpp
// SerialReader.h
std::function<void()> onReconnected;
void setReconnectCallback(std::function<void()> cb) { onReconnected = std::move(cb); }

// SerialReader.cpp after successful openPort:
if (onReconnected) juce::MessageManager::callAsync([cb = onReconnected](){ cb(); });
```

Wire in MainComponent:
```cpp
reader.setReconnectCallback([this]() {
    modeSwitcher->getRuntime();  // placeholder access
    detector_reset_via_switcher();  // delegate to ModeSwitcher
});
```

(Add a `resetDetector()` public method on ModeSwitcher that calls `detector.onReconnect()` and `setMode(AppMode::Unknown)`.)

- [ ] **Commit**

```bash
git commit -am "feat(viewer): ModeSwitcher auto-bascule between runtime/setup"
```

---

## Phase 7 — Polish & ship

### Task 31: Scale-to-fit + aspect ratio fixed 3:2

**Files:**
- Modify: `Source/MainComponent.cpp` (apply AffineTransform on resize)
- Modify: `Source/Main.cpp` (set aspect ratio on the MainWindow)

- [ ] **Step 1: Apply scale transform in MainComponent::resized()**

```cpp
void MainComponent::resized() {
    constexpr float DESIGN_W = 900.0f;
    constexpr float DESIGN_H = 600.0f;
    float scale = juce::jmin(getWidth() / DESIGN_W, getHeight() / DESIGN_H);
    modeSwitcher->setTransform(juce::AffineTransform::scale(scale));
    modeSwitcher->setBounds(0, 0, (int)DESIGN_W, (int)DESIGN_H);
}
```

- [ ] **Step 2: Constrain aspect ratio 3:2 on the window**

In `MainWindow` ctor:
```cpp
setResizable(true, false);
getConstrainer()->setFixedAspectRatio(3.0 / 2.0);
getConstrainer()->setMinimumSize(400, 266);
```

- [ ] **Step 3: Build, run, resize window**

Expected: every element (text, frames, padding) grows/shrinks proportionally. Aspect ratio preserved on resize.

- [ ] **Step 4: Commit**

```bash
git commit -am "feat(viewer): scale-to-fit on MainComponent + fixed 3:2 aspect ratio"
```

---

### Task 32: Onboarding + About panel

**Files:**
- Modify: `Source/MainComponent.{h,cpp}` (handle `firstRun` flag, show initial banner)
- Add About menu item (juce native macOS)

- [ ] **Step 1: First-run banner**

On `firstRun=true`, overlay a translucent panel "Connect your ILLPAD via USB and it will be auto-detected" with a "Got it" button. On click, set `firstRun=false`, save settings, dismiss panel.

- [ ] **Step 2: About panel via menu**

```cpp
// Main.cpp
class ILLPADViewerApp : public juce::JUCEApplication {
    void anotherInstanceStarted(const juce::String&) override {}
    // ...
    void systemRequestedQuit() override { quit(); }
};
```

For macOS native About: use `juce::PopupMenu` from menu bar or rely on JUCE's default "About" menu integration. Minimal: a simple `AlertWindow` triggered by Cmd-? or a Help menu entry, showing "ILLPAD Viewer 1.0.0 — Loïc Lachaize, 2026".

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(viewer): first-run onboarding + About panel"
```

---

### Task 33: Codesign ad-hoc + verify .app bundle

- [ ] **Step 1: Build Release**

```bash
cmake --build build --config Release
```

- [ ] **Step 2: Ad-hoc codesign**

```bash
codesign --sign - --deep --force build/ILLPADViewer_artefacts/Release/ILLPADViewer.app
```

- [ ] **Step 3: Verify**

```bash
codesign --verify --deep --verbose build/ILLPADViewer_artefacts/Release/ILLPADViewer.app
spctl --assess --verbose build/ILLPADViewer_artefacts/Release/ILLPADViewer.app || true
```

Expected: codesign verify succeeds. spctl will flag "no acceptable origin" for ad-hoc — that's normal, user can right-click → Open to bypass Gatekeeper.

- [ ] **Step 4: Manual install test**

```bash
cp -R build/ILLPADViewer_artefacts/Release/ILLPADViewer.app /Applications/
open /Applications/ILLPADViewer.app
```

Expected: app opens, ILLPAD auto-detected, runtime UI populated. Quit and verify settings persist across relaunch.

- [ ] **Step 5: Commit (no source change, but commit the README if added)**

---

### Task 34: Full E2E smoke test — spec §43 checklist

Execute the 15-step E2E checklist from the spec §43, mark every step ☐ → ✅, fix any regression discovered.

- [ ] Step 1: Lance app sans ILLPAD → "Waiting for ILLPAD" affiché.
- [ ] Step 2: Branche ILLPAD → status `● Connected` → `[BANKS]` + `[STATE]` reçus → UI populée intégralement.
- [ ] Step 3: Tourner R1 → cellule R1 PotCell met à jour + highlight flash 300ms.
- [ ] Step 4: Switch bank → marker ▶ bouge + panneau bank courante remis à jour.
- [ ] Step 5: Bank ARP : appuyer pad → status PLAY + pastille verte pulse à la subdivision.
- [ ] Step 6: Stop arp → "STOP" en gris, pastille disparue.
- [ ] Step 7: Triple-click rear button → flash rouge transport bar ~2 sec.
- [ ] Step 8: Débrancher câble → status `● Reconnecting` orange.
- [ ] Step 9: Rebrancher → auto-reconnect → ré-envoi `?BOTH` → UI re-populée.
- [ ] Step 10: Reboot ILLPAD en setup mode → ModeDetector bascule SETUP → TerminalView affiche le cockpit VT100 fidèle.
- [ ] Step 11: Naviguer dans setup avec flèches → latence imperceptible, rendu fidèle, pas de tearing.
- [ ] Step 12: Quitter setup → reboot ILLPAD → ModeDetector bascule RUNTIME.
- [ ] Step 13: Toggle always-on-top → fenêtre flotte/déflotte au-dessus d'Ableton.
- [ ] Step 14: Resize fenêtre → tout scale proportionnellement.
- [ ] Step 15: Quitter app → relancer → position fenêtre + port + always-on-top persistés.

- [ ] **Commit final**

```bash
git commit --allow-empty -m "chore: V3 smoke test E2E pass"
```

---

## Out of scope for this plan

Tout ce qui est listé dans la spec §47 (V2 list) : item menu bar, notifications natives, light theme, multi-instance, mode compact, palette user-defined, sync app↔firmware ColorSlot, tick synchro horloge maîtresse, animations avancées (glow), filtre catégorie EventLogPanel, export log, Sparkle auto-update, Developer ID notarisation, portage Windows/Linux, raccourci clavier global, tests pixel-perfect / CI auto.

---

**End of plan.**
