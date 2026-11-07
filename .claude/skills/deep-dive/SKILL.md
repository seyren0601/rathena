---
name: deep-dive
description: Source-level deep-dive investigation of one rAthena subsystem (e.g. "deep dive into the skill system", "/deep-dive status"). Produces a document under docs/subsystems/ and updates docs/DOCUMENTATION_PROGRESS.md. Use whenever the user asks to deep dive, investigate in depth, or trace a specific system.
argument-hint: <subsystem name>
---

I want to investigate the **$ARGUMENTS** subsystem in depth.

If no subsystem name was given, ask which one before starting.

Act as a senior C/C++ engineer performing a source-code-level investigation.

Do not provide a generic explanation. Base everything on the actual implementation in this repository.

## Investigation process

1. Identify all relevant source files.
2. Identify important structures, classes, global variables, and functions.
3. Identify the subsystem's initialization and shutdown sequences.
4. Trace its main execution paths, including function call chains.
5. Identify its dependencies on other subsystems.
6. Explain important algorithms and implementation decisions.
7. Explain memory ownership, object lifetimes, and concurrency where applicable.
8. Identify error handling and edge cases.
9. Trace at least one real feature from its entry point to its final result.
10. Identify safe extension points and potential customization risks.

## Documentation requirements

- Include source file paths and function names.
- Include line references whenever possible.
- Use Mermaid diagrams for complex execution flows.
- Explain unfamiliar C/C++ concepts.
- Distinguish verified facts from assumptions.
- Include practical customization examples.
- Identify questions that remain unanswered.
- Save the results under the appropriate `docs/subsystems/` directory.
- Update `DOCUMENTATION_PROGRESS.md` when finished.
- Do not modify the original source code.

At the end, provide a list of the most important functions I should personally study, ordered from beginner-friendly to advanced.

## Repository conventions (apply alongside the brief above)

- **Read the existing knowledge base first.** `docs/architecture/` already covers every subsystem at overview depth (start at `docs/architecture/README.md` and `docs/architecture/subsystems.md`; per-system docs are in `docs/architecture/gameplay/`). Build on it, link to it, and don't repeat it. If the deep dive proves something there wrong, fix it in that doc too and say so.
- **Verification markers**, same as the knowledge base: ✅ verified (with `file:line`), ⚠️ inferred, ❓ unknown.
- **Output location**: `docs/subsystems/<area>/<subsystem>.md`, where `<area>` matches the server or layer (for example `map/`, `char/`, `login/`, `web/`, `common/`, `scripting/`). Create the folder if it doesn't exist.
- **Progress file**: `docs/DOCUMENTATION_PROGRESS.md`. Create it if missing, with a table of subsystem, document link, date, status and open questions. Add or update one row per deep dive. Also add the new doc to the index in `docs/architecture/README.md`.
- Line numbers drift; quote the function name next to every line reference so it can be found again.
- Recheck line numbers with grep before writing them; never guess.
- Keep a task list while working and re-verify any ⚠️ claim you can cheaply confirm before finishing.
- Commit only when the user asks. Commit labels in this repo look like `[doc] ...`.
