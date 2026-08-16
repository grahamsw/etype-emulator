// app.js — E-Type application entry point

import { Typewriter } from './typewriter.js';
import { UI } from './ui.js';
import * as Storage from './storage.js';
import { loginWithGoogle, logout, onUserChanged } from './auth.js';

// Debounce helper
function debounce(fn, ms) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), ms);
  };
}

function init() {
  // ---- Grab DOM elements ----
  const displayEl = document.getElementById('text-display');
  const inputEl = document.getElementById('hidden-input');
  const cursorEl = document.getElementById('cursor');
  const placeholderEl = document.getElementById('placeholder');
  const screenEl = document.getElementById('etype-screen');
  const wordCountEl = document.getElementById('word-count');
  const titleInputEl = document.getElementById('title-input');
  const authBtnEl = document.getElementById('btn-auth');
  const newBtnEl = document.getElementById('btn-new');
  const downloadBtnEl = document.getElementById('btn-download');
  const dialogEl = document.getElementById('new-draft-dialog');
  const confirmBtnEl = document.getElementById('btn-confirm-new');
  const cancelBtnEl = document.getElementById('btn-cancel-new');
  
  let currentUser = null;

  // ---- Initialize UI ----
  const ui = new UI({
    screen: screenEl,
    wordCount: wordCountEl,
    titleInput: titleInputEl,
    authBtn: authBtnEl,
    newBtn: newBtnEl,
    downloadBtn: downloadBtnEl,
    dialog: dialogEl,
    confirmBtn: confirmBtnEl,
    cancelBtn: cancelBtnEl,
  });
  
  // ---- Auto-save (debounced) ----
  let createdAt = new Date().toISOString();
  
  const autoSave = debounce(() => {
    Storage.save({
      text: typewriter.getText(),
      title: ui.getTitle(),
      createdAt,
    });
  }, 1500);
  
  // ---- Initialize Typewriter ----
  const typewriter = new Typewriter(displayEl, inputEl, cursorEl, placeholderEl, {
    onTextChange: (text) => {
      ui.updateWordCount(text);
      autoSave();
    },
  });
  
  // ---- Restore saved draft ----
  const saved = Storage.load();
  if (saved && saved.text) {
    typewriter.setText(saved.text);
    ui.setTitle(saved.title);
    ui.updateWordCount(saved.text);
    createdAt = saved.createdAt || createdAt;
    // Scroll to bottom after restore
    requestAnimationFrame(() => ui.scrollToBottom());
  }
  
  // ---- Auth state subscription ----
  onUserChanged((user) => {
    currentUser = user;
    ui.setAuthState(user);
  });

  // ---- Bind UI handlers ----
  ui.bindHandlers({
    onAuth: async () => {
      if (currentUser) {
        try {
          await logout();
        } catch (err) {
          console.error('Logout failed:', err);
        }
      } else {
        try {
          await loginWithGoogle();
        } catch (err) {
          console.error('Login failed:', err);
        }
      }
    },

    onNew: () => {
      // If there's text, confirm first
      const currentText = typewriter.getText();
      if (currentText.trim()) {
        ui.showNewDraftDialog(() => {
          typewriter.clear();
          ui.setTitle('Untitled Draft');
          ui.updateWordCount('');
          createdAt = new Date().toISOString();
          Storage.clearStorage();
          typewriter.focus();
        });
      } else {
        // No text, just reset
        typewriter.clear();
        ui.setTitle('Untitled Draft');
        createdAt = new Date().toISOString();
        Storage.clearStorage();
        typewriter.focus();
      }
    },
    
    onDownload: () => {
      const text = typewriter.getText();
      const title = ui.getTitle();
      if (!text.trim()) return; // Nothing to download
      Storage.downloadAsMarkdown(text, title);
    },
    
    onTitleChange: () => {
      autoSave();
    },
  });
  
  // ---- Save on page unload ----
  window.addEventListener('beforeunload', () => {
    Storage.save({
      text: typewriter.getText(),
      title: ui.getTitle(),
      createdAt,
    });
  });
  
  // ---- Focus the typewriter on load ----
  // Small delay to ensure everything is rendered
  setTimeout(() => typewriter.focus(), 100);
}

// ---- Boot ----
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
