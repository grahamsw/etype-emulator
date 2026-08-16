// app.js — E-Type application entry point

import { Typewriter } from './typewriter.js';
import { UI } from './ui.js';
import * as Storage from './storage.js';
import { loginWithGoogle, logout, onUserChanged, getAccessToken } from './auth.js';
import { saveDraftToDrive, resetDriveCache } from './drive.js';

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
  const screenEl = document.getElementById('etype-screen');
  const screenContainerEl = document.getElementById('screen-container');
  const wordCountEl = document.getElementById('word-count');
  const titleInputEl = document.getElementById('title-input');
  const authBtnEl = document.getElementById('btn-auth');
  const gdriveBtnEl = document.getElementById('btn-gdrive');
  const newBtnEl = document.getElementById('btn-new');
  const downloadBtnEl = document.getElementById('btn-download');
  const dialogEl = document.getElementById('new-draft-dialog');
  const confirmBtnEl = document.getElementById('btn-confirm-new');
  const cancelBtnEl = document.getElementById('btn-cancel-new');

  const gdriveDialogEl = document.getElementById('gdrive-dialog');
  const gdriveTitleInputEl = document.getElementById('gdrive-title-input');
  const confirmGdriveBtnEl = document.getElementById('btn-confirm-gdrive');
  const cancelGdriveBtnEl = document.getElementById('btn-cancel-gdrive');
  const gdriveFolderDisplayEl = document.getElementById('gdrive-folder-display');

  const infoBtnEl = document.getElementById('btn-info');
  const settingsBtnEl = document.getElementById('btn-settings');
  const infoDialogEl = document.getElementById('info-dialog');
  const closeInfoBtnEl = document.getElementById('btn-close-info');
  const settingsDialogEl = document.getElementById('settings-dialog');
  const settingsFolderInputEl = document.getElementById('settings-drive-folder');
  const cancelSettingsBtnEl = document.getElementById('btn-cancel-settings');
  const saveSettingsBtnEl = document.getElementById('btn-save-settings');

  const toastEl = document.getElementById('toast');
  const toastMessageEl = document.getElementById('toast-message');
  
  let currentUser = null;
  let appSettings = Storage.loadSettings();

  // ---- Initialize UI ----
  const ui = new UI({
    screen: screenEl,
    wordCount: wordCountEl,
    titleInput: titleInputEl,
    authBtn: authBtnEl,
    gdriveBtn: gdriveBtnEl,
    newBtn: newBtnEl,
    downloadBtn: downloadBtnEl,
    dialog: dialogEl,
    confirmBtn: confirmBtnEl,
    cancelBtn: cancelBtnEl,
    gdriveDialog: gdriveDialogEl,
    gdriveTitleInput: gdriveTitleInputEl,
    confirmGdriveBtn: confirmGdriveBtnEl,
    cancelGdriveBtn: cancelGdriveBtnEl,
    gdriveFolderDisplay: gdriveFolderDisplayEl,
    infoBtn: infoBtnEl,
    settingsBtn: settingsBtnEl,
    infoDialog: infoDialogEl,
    closeInfoBtn: closeInfoBtnEl,
    settingsDialog: settingsDialogEl,
    settingsFolderInput: settingsFolderInputEl,
    cancelSettingsBtn: cancelSettingsBtnEl,
    saveSettingsBtn: saveSettingsBtnEl,
    toast: toastEl,
    toastMessage: toastMessageEl,
  });
  
  // ---- Distraction-Free Auto-Hide Handling ----
  let autohideTimer = null;

  const revealUI = () => {
    ui.setAutohide(false);
  };

  const hideUI = () => {
    ui.setAutohide(true);
  };

  // Mouse movement auto-hide rules
  document.addEventListener('mousemove', (e) => {
    // If mouse is outside screen container (outer margins / edges), reveal controls
    const rect = screenContainerEl ? screenContainerEl.getBoundingClientRect() : null;
    if (rect) {
      const isInsidePaper = (
        e.clientX >= rect.left &&
        e.clientX <= rect.right &&
        e.clientY >= rect.top &&
        e.clientY <= rect.bottom
      );
      if (!isInsidePaper) {
        revealUI();
        return;
      }
    }

    // Inside paper area while typing -> auto-hide UI
    if (typewriter.hasTyped) {
      hideUI();
    }
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
  const typewriter = new Typewriter(displayEl, inputEl, cursorEl, {
    onTextChange: (text) => {
      ui.updateWordCount(text);
      autoSave();
    },
    onTypingStart: () => {
      hideUI();
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
    if (!user) {
      resetDriveCache();
      revealUI();
    }
  });

  // ---- Bind UI handlers ----
  ui.bindHandlers({
    onAuth: async () => {
      if (currentUser) {
        try {
          await logout();
          resetDriveCache();
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

    onRequestSettings: () => {
      ui.showSettingsDialog(appSettings, (newSettings) => {
        appSettings = newSettings;
        Storage.saveSettings(newSettings);
        ui.showToast(`Settings saved. Folder: ${appSettings.driveFolder}`);
      });
    },

    onGDrive: async () => {
      let token = getAccessToken();

      // If not logged in or token missing, authenticate immediately on button click gesture
      if (!currentUser || !token) {
        try {
          ui.showToast('Signing in with Google...');
          const authResult = await loginWithGoogle();
          token = authResult ? authResult.accessToken : getAccessToken();
        } catch (err) {
          if (err && err.code !== 'auth/popup-closed-by-user') {
            ui.showToast(`Auth Error: ${err.message || 'Sign in failed'}`);
          }
          return;
        }
      }

      if (!token) {
        ui.showToast('Google Sign-In is required to save to Google Drive.');
        return;
      }

      const text = typewriter.getText();
      if (!text.trim()) {
        ui.showToast('Draft is empty. Type a few words before saving!');
        return;
      }

      const folderName = appSettings.driveFolder || 'etype_drafts';

      // Open Save to Google Drive modal dialog
      ui.showGDriveDialog(ui.getTitle(), folderName, async (customTitle) => {
        try {
          ui.showToast(`Saving to ${folderName} folder in Google Drive...`);
          const activeToken = getAccessToken() || token;
          const file = await saveDraftToDrive(activeToken, customTitle, text, folderName);
          ui.setTitle(customTitle);
          ui.showToast(`Saved "${file.name}" to ${folderName} in Google Drive!`);
        } catch (err) {
          console.error('Google Drive save error:', err);
          ui.showToast(`Drive Error: ${err.message || 'Failed to save'}`);
        }
      });
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
          revealUI();
          typewriter.focus();
        });
      } else {
        // No text, just reset
        typewriter.clear();
        ui.setTitle('Untitled Draft');
        createdAt = new Date().toISOString();
        Storage.clearStorage();
        revealUI();
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
  setTimeout(() => typewriter.focus(), 100);
}

// ---- Boot ----
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
