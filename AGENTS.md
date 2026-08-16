# AGENTS.md — E-Type Typewriter Emulator

Guidelines for AI agents working on this project.

## Project Overview

E-Type is a distraction-free e-ink typewriter emulator for first drafts. It's a static SPA (HTML/CSS/JS, no framework, no build step) deployed to Firebase Hosting. The core philosophy is **forward-only writing** — no editing, no deletion, just typing.

## Architecture

### Module Structure

All application code lives in `public/`. There is **no build step** — files are served as-is via ES modules.

| File | Responsibility |
|------|---------------|
| `public/index.html` | App shell, DOM structure, element IDs |
| `public/css/index.css` | All styling, CSS custom properties, animations |
| `public/js/typewriter.js` | Core typing engine — input capture, key blocking, render delay queue, e-ink animation |
| `public/js/storage.js` | localStorage persistence, `.md` file download |
| `public/js/ui.js` | UI management — word count, dialogs, scrolling, title |
| `public/js/app.js` | Entry point — imports and wires all modules together |

### Key Design Decisions

1. **Hidden textarea pattern**: A hidden `<textarea>` captures keyboard input (handles IME, mobile keyboards). The visible text is rendered in a `<div>` (`#text-display`) using `<span>` elements for character-level animation control.

2. **Forward-only enforcement**: Blocking happens at three layers:
   - `keydown` — blocks Backspace, Delete, ArrowLeft, ArrowUp, Home, Ctrl+Z/X/A
   - `beforeinput` — blocks all `delete*` and `history*` input types
   - `paste`/`drop` events — blocked entirely

3. **E-ink delay queue**: Characters are buffered and rendered one-at-a-time with an 80ms delay between each. Each character gets a CSS `ink-settle` animation (250ms) that transitions from ghosted/blurred to solid ink.

4. **No build tooling**: This is intentional. The project should remain a zero-dependency static site. Do not introduce bundlers, transpilers, or package.json unless explicitly requested.

### DOM Element IDs (Contract)

These IDs are referenced across HTML, CSS, and JS. Do not rename without updating all three layers:

- `#app` — root container
- `#screen-container` — device frame (bezel)
- `#toolbar` — top toolbar
- `#title-input` — draft title input
- `#word-count` — word count display
- `#btn-new` — new draft button
- `#btn-download` — download button
- `#etype-screen` — e-paper viewport (scrollable)
- `#text-display` — text rendering container
- `#hidden-input` — hidden textarea for input capture
- `#cursor` — blinking block cursor
- `#placeholder` — "Just start typing…" placeholder
- `#new-draft-dialog` — native `<dialog>` for new draft confirmation
- `#btn-confirm-new` — confirm button in dialog
- `#btn-cancel-new` — cancel button in dialog

### CSS Custom Properties

The design system uses CSS custom properties on `:root`. Key tokens:

- `--paper-bg` — e-paper background (warm cream)
- `--ink-color` — text color (soft black)
- `--ghost-color` — ink-settle animation start color
- `--device-bezel` / `--device-bezel-dark` — device frame gradient
- `--surface-bg` — page background (dark charcoal)
- `--settle-duration` — e-ink character animation duration
- `--render-delay` — delay between characters appearing

## Conventions

### Code Style
- Vanilla JavaScript with ES modules (`import`/`export`)
- No TypeScript, no JSX, no framework
- Use `const`/`let`, never `var`
- Descriptive function and variable names
- JSDoc comments on public class methods and exported functions
- CSS uses BEM-ish naming (`.block--modifier`)

### Git Workflow
- All work on GitHub issues must be done on a dedicated feature branch named after the issue number and title (e.g., `1-google-identity-auth`).
- Branch from `main` before starting work.
- Conventional commit messages: `feat:`, `fix:`, `refactor:`, `docs:`, `style:`, `chore:`
- Keep commits atomic — one logical change per commit.
- Submit a Pull Request targeting `main` (using `gh pr create`) when work and manual verification are complete.

### Testing
- No test framework currently — verify manually using Firebase Hosting emulator:
  ```bash
  npx -y firebase-tools@latest emulators:start --only hosting --project <PROJECT_ID>
  ```
- Key behaviors to verify on any change:
  1. Characters appear with delay and animation
  2. Backspace, Delete, Ctrl+Z, selection are all blocked
  3. Draft persists across page refresh (localStorage)
  4. Download produces a valid `.md` file
  5. "New Draft" dialog works correctly
  6. Responsive layout on mobile viewports

### Deployment
- Firebase Hosting (static site, no SSR)
- Deploy: `npx -y firebase-tools@latest deploy --only hosting`
- Always use `npx -y firebase-tools@latest` (never bare `firebase`)

## Planned Features (Future Work)

These are on the roadmap but not yet implemented:

1. **Google Sign-In** — Authenticate with Google Identity, gate access
2. **Google Docs integration** — Save drafts to a designated Google Docs folder
3. **Multi-draft management** — List, open, switch between saved drafts
4. **Strikethrough shortcut** — `~~text~~` via keyboard shortcut for typewriter-style corrections
5. **Edit mode** — Optional toggle to allow editing (separate from drafting mode)
6. **Sound effects** — Optional typewriter key-click audio
7. **Themes** — Dark mode, sepia, high contrast

When implementing these, preserve the core principle: **the default mode is always forward-only drafting**.
