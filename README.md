# E-Type — E-Ink Typewriter Emulator

A distraction-free drafting tool that emulates an e-ink typewriter. Write forward, never look back.

## Concept

E-Type enforces the discipline of **forward-only writing**. There is no backspace, no delete, no undo, no selecting text. You type, and the words appear on a warm e-paper screen with a subtle ink-settling delay — just like a real typewriter. This is a tool for **first drafts**, where the goal is to get words on the page without the temptation to edit.

## Features

- **Forward-only input** — Backspace, Delete, Ctrl+Z, text selection, cut, and paste are all blocked
- **E-ink rendering** — Characters appear with a configurable delay (~80ms) and a settling animation that mimics e-paper refresh
- **E-paper aesthetic** — Warm cream screen, Courier Prime font, device bezel frame, dark surround
- **Markdown format** — Write in markdown for simple formatting without taking hands off the keyboard
- **Auto-save** — Drafts persist in localStorage automatically (debounced every 1.5s)
- **Download as `.md`** — Export your draft as a markdown file at any time
- **Editable title** — Name your draft in the toolbar
- **Live word count** — Always visible in the toolbar
- **Responsive** — Works on desktop and mobile

## Tech Stack

- **Pure HTML/CSS/JS** — No framework, no build step, no dependencies
- **ES Modules** — Clean module architecture
- **Firebase Hosting** — Static site deployment
- **Google Fonts** — Courier Prime typeface

## Project Structure

```
etype-emulator/
├── public/                  # Served by Firebase Hosting
│   ├── index.html           # App shell
│   ├── css/
│   │   └── index.css        # Design system + e-ink theming
│   └── js/
│       ├── app.js           # Entry point, wires modules together
│       ├── typewriter.js    # Core typing engine (input capture, key blocking, delay queue)
│       ├── storage.js       # localStorage persistence + .md file download
│       └── ui.js            # Word count, dialogs, scroll management
├── firebase.json            # Hosting configuration
├── .firebaserc              # Firebase project alias
└── README.md
```

## Local Development

```bash
# Serve locally with Firebase emulator
npx -y firebase-tools@latest emulators:start --only hosting --project <PROJECT_ID>

# Or just open public/index.html directly in a browser
```

## Deployment

```bash
# Login to Firebase (first time only)
npx -y firebase-tools@latest login

# Set active project
npx -y firebase-tools@latest use <PROJECT_ID>

# Deploy
npx -y firebase-tools@latest deploy --only hosting
```

## Roadmap

- [ ] Google Sign-In authentication
- [ ] Save drafts to Google Docs folder
- [ ] Multiple draft management (open/switch between drafts)
- [ ] Optional typewriter sound effects
- [ ] Strikethrough shortcut (`~~text~~`) for typewriter-style corrections
- [ ] Edit mode toggle for revisions
- [ ] Dark mode / sepia mode toggle
- [ ] Session statistics (writing time, words per minute)

## License

MIT
