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
  const copyBtnEl = document.getElementById('btn-copy');
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
  const settingsWordCountInputEl = document.getElementById('settings-show-wordcount');
  const settingsCursorBlinkInputEl = document.getElementById('settings-cursor-blink');
  const settingsEnableTimestampInputEl = document.getElementById('settings-enable-timestamp');
  const settingsTimestampFormatSelectEl = document.getElementById('settings-timestamp-format');
  const settingsTimestampPositionSelectEl = document.getElementById('settings-timestamp-position');
  const settingsFontSelectEl = document.getElementById('settings-font-select');
  const settingsFontsizeSelectEl = document.getElementById('settings-fontsize-select');
  const settingsThemeSelectEl = document.getElementById('settings-theme-select');
  const customColorContainerEl = document.getElementById('custom-color-container');
  const colorSurfaceInputEl = document.getElementById('settings-color-surface');
  const colorBezelInputEl = document.getElementById('settings-color-bezel');
  const colorPaperInputEl = document.getElementById('settings-color-paper');
  const colorInkInputEl = document.getElementById('settings-color-ink');
  const heightSliderEl = document.getElementById('height-slider');
  const cancelSettingsBtnEl = document.getElementById('btn-cancel-settings');
  const saveSettingsBtnEl = document.getElementById('btn-save-settings');

  const toastEl = document.getElementById('toast');
  const toastMessageEl = document.getElementById('toast-message');
  
  let currentUser = null;
  let appSettings = Storage.loadSettings();

  // ---- Initialize UI ----
  const ui = new UI({
    screen: screenEl,
    cursor: cursorEl,
    wordCount: wordCountEl,
    titleInput: titleInputEl,
    authBtn: authBtnEl,
    gdriveBtn: gdriveBtnEl,
    newBtn: newBtnEl,
    copyBtn: copyBtnEl,
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
    settingsWordCountInput: settingsWordCountInputEl,
    settingsCursorBlinkInput: settingsCursorBlinkInputEl,
    settingsEnableTimestampInput: settingsEnableTimestampInputEl,
    settingsTimestampFormatSelect: settingsTimestampFormatSelectEl,
    settingsTimestampPositionSelect: settingsTimestampPositionSelectEl,
    settingsFontSelect: settingsFontSelectEl,
    settingsFontsizeSelect: settingsFontsizeSelectEl,
    settingsThemeSelect: settingsThemeSelectEl,
    customColorContainer: customColorContainerEl,
    colorSurfaceInput: colorSurfaceInputEl,
    colorBezelInput: colorBezelInputEl,
    colorPaperInput: colorPaperInputEl,
    colorInkInput: colorInkInputEl,
    heightSlider: heightSliderEl,
    heightSliderContainer: document.querySelector('.bezel-slider-container'),
    cancelSettingsBtn: cancelSettingsBtnEl,
    saveSettingsBtn: saveSettingsBtnEl,
    toast: toastEl,
    toastMessage: toastMessageEl,
  });

  // Apply initial word count visibility, cursor blink, font, theme, and viewport height settings
  ui.setWordCountVisibility(appSettings.showWordCount !== false);
  ui.applyCursorBlinkSetting(appSettings.cursorBlink !== false);
  ui.applyFontSettings(appSettings.font, appSettings.fontSize);
  ui.applyThemeSettings(appSettings.theme, appSettings.customColors);
  ui.applyViewportHeight(appSettings.viewportHeight || 100);
  
  // ---- Distraction-Free Controls & Bottom Bar Visibility ----
  let mouseIdleTimer = null;

  const showControls = () => {
    document.body.classList.add('show-controls');
    if (mouseIdleTimer) clearTimeout(mouseIdleTimer);
    // Controls hang around for 3 seconds after mouse stops moving
    mouseIdleTimer = setTimeout(() => {
      const focusedElement = document.activeElement;
      const isFocusedInControls = focusedElement && focusedElement.closest('#toolbar, .floating-controls, dialog');
      if (!isFocusedInControls) {
        document.body.classList.remove('show-controls');
      }
    }, 3000);
  };

  const hideControlsImmediately = () => {
    if (mouseIdleTimer) clearTimeout(mouseIdleTimer);
    const focusedElement = document.activeElement;
    const isFocusedInControls = focusedElement && focusedElement.closest('#toolbar, .floating-controls, dialog');
    if (!isFocusedInControls) {
      document.body.classList.remove('show-controls');
    }
  };

  window.addEventListener('mousemove', () => {
    showControls();
  });

  window.addEventListener('touchstart', () => {
    showControls();
  }, { passive: true });

  // ---- Auto-save (debounced) ----
  let createdAt = new Date().toISOString();
  
  const autoSave = debounce(() => {
    Storage.save({
      text: typewriter.getText(),
      title: ui.getTitle(),
      createdAt,
    });
  }, 1500);
  
  // ---- Copy to Clipboard Handler ----
  const handleCopy = async () => {
    const text = typewriter.getText();
    if (!text) {
      ui.showToast('Draft is empty');
      return;
    }
    const success = await Storage.copyToClipboard(text);
    if (success) {
      ui.showToast('Copied draft to clipboard!');
    } else {
      ui.showToast('Failed to copy to clipboard');
    }
  };

  // ---- Initialize Typewriter ----
  const typewriter = new Typewriter(displayEl, inputEl, cursorEl, {
    onTextChange: (text) => {
      ui.updateWordCount(text);
      autoSave();
    },
    onTypingStart: () => {
      hideControlsImmediately();
    },
    onCopyRequest: () => {
      handleCopy();
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
        ui.setWordCountVisibility(appSettings.showWordCount !== false);
        ui.applyCursorBlinkSetting(appSettings.cursorBlink !== false);
        ui.applyFontSettings(appSettings.font, appSettings.fontSize);
        ui.applyThemeSettings(appSettings.theme, appSettings.customColors);
        ui.showToast(`Settings saved.`);
      });
    },

    onViewportHeightChange: (val) => {
      appSettings.viewportHeight = val;
      Storage.saveSettings(appSettings);
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
          ui.showToast(`Saving to Google Drive...`);
          const activeToken = getAccessToken() || token;
          const file = await saveDraftToDrive(activeToken, customTitle, text, folderName, appSettings);
          ui.setTitle(customTitle);
          ui.showToast(`Saved "${file.name}" to Google Drive!`);
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

    onCopy: () => {
      handleCopy();
    },
    
    onDownload: () => {
      const text = typewriter.getText();
      const title = ui.getTitle();
      if (!text.trim()) return; // Nothing to download
      Storage.downloadAsMarkdown(text, title, appSettings);
    },
    
    onTitleChange: () => {
      autoSave();
    },

    onDialogClose: () => {
      focusTypewriter();
    },
  });

  // ---- Global Focus Management (Distraction-Free Typewriter Focus) ----
  const focusTypewriter = () => {
    if (document.querySelector('dialog[open]')) return;
    typewriter.focus();
  };

  // Global click listener: clicking background canvas, bezel, or paper sets focus to typewriter
  document.addEventListener('click', (e) => {
    if (document.querySelector('dialog[open]')) return;
    const isInteractive = e.target.closest('button, input, select, textarea, a, label, dialog');
    if (!isInteractive) {
      hideControlsImmediately();
      focusTypewriter();
    }
  });

  // Global keydown listener for Copy shortcut (Ctrl+C / Cmd+C)
  document.addEventListener('keydown', (e) => {
    if (document.querySelector('dialog[open]')) return;
    const activeEl = document.activeElement;
    if (activeEl && activeEl.tagName === 'INPUT' && activeEl.id !== 'hidden-input') return;

    if ((e.ctrlKey || e.metaKey) && !e.shiftKey && e.key.toLowerCase() === 'c') {
      e.preventDefault();
      handleCopy();
    }
  });

  // Window focus listener (switching tabs or returning to app)
  window.addEventListener('focus', () => {
    focusTypewriter();
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
  setTimeout(() => focusTypewriter(), 100);
}

// ---- Boot ----
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
