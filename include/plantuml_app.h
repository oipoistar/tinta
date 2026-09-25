#ifndef TINTA_PLANTUML_APP_H
#define TINTA_PLANTUML_APP_H

// PlantUML app bridge (plantuml_app.cpp): resolves the configured or
// PATH-provided PlantUML tool into App state and persists the user's
// choice - the PlantUML twin of the pandoc bridge, deliberately kept out
// of the app-free src/plantuml.cpp so the unit suite stays headless.
//
// Resolution order: the saved `plantumlPath` (an exe, or a .jar for which
// java.exe comes from PATH - there is deliberately no second java setting),
// then `plantuml.exe` on PATH, else unavailable. There are no installer
// probes: PlantUML ships as a zip, not an installer.

#include "app.h"

// Resolve the tool once per session into app.plantumlTool. Cheap after the
// first call; a missing tool never blocks startup - callers fall back to
// showing the fence source.
void plantumlResolve(App& app);

// True when a usable tool is resolved (resolves first when needed).
bool plantumlAvailable(App& app);

// The user picked a tool (exe or jar) in the settings file picker:
// remember it, re-resolve, and persist `plantumlPath` into settings.ini
void plantumlSetUserPath(App& app, const std::wstring& path);

// Lazily creates the async render queue and wires its completion callback
// to WM_APP_PLANTUML_READY (posted FROM THE WORKER THREAD), then computes
// the per-process work root under %TEMP% once. Idempotent.
void plantumlEnsureQueue(App& app);

#endif  // TINTA_PLANTUML_APP_H