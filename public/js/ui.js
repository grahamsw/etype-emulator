// ui.js — UI management for E-Type

export class UI {
  /**
   * @param {Object} elements - DOM element references
   * @param {HTMLElement} elements.screen - #etype-screen
   * @param {HTMLElement} elements.wordCount - #word-count
   * @param {HTMLInputElement} elements.titleInput - #title-input
   * @param {HTMLButtonElement} elements.newBtn - #btn-new
   * @param {HTMLButtonElement} elements.downloadBtn - #btn-download
   * @param {HTMLDialogElement} elements.dialog - #new-draft-dialog
   * @param {HTMLButtonElement} elements.confirmBtn - #btn-confirm-new
   * @param {HTMLButtonElement} elements.cancelBtn - #btn-cancel-new
   */
  constructor(elements) {
    this.screen = elements.screen;
    this.wordCount = elements.wordCount;
    this.titleInput = elements.titleInput;
    this.newBtn = elements.newBtn;
    this.copyBtn = elements.copyBtn;
    this.downloadBtn = elements.downloadBtn;
    this.gdriveBtn = elements.gdriveBtn;
    this.authBtn = elements.authBtn;
    this.authLabel = this.authBtn ? this.authBtn.querySelector('.auth-label') : null;
    this.dialog = elements.dialog;
    this.confirmBtn = elements.confirmBtn;
    this.cancelBtn = elements.cancelBtn;
    this.gdriveDialog = elements.gdriveDialog;
    this.gdriveTitleInput = elements.gdriveTitleInput;
    this.confirmGdriveBtn = elements.confirmGdriveBtn;
    this.cancelGdriveBtn = elements.cancelGdriveBtn;
    this.gdriveFolderDisplay = elements.gdriveFolderDisplay;
    this.infoBtn = elements.infoBtn;
    this.settingsBtn = elements.settingsBtn;
    this.infoDialog = elements.infoDialog;
    this.closeInfoBtn = elements.closeInfoBtn;
    this.cursorEl = elements.cursor || document.getElementById('cursor');
    this.settingsDialog = elements.settingsDialog;
    this.settingsFolderInput = elements.settingsFolderInput;
    this.settingsWordCountInput = elements.settingsWordCountInput;
    this.settingsCursorBlinkInput = elements.settingsCursorBlinkInput;
    this.settingsEnableTimestampInput = elements.settingsEnableTimestampInput;
    this.settingsTimestampFormatSelect = elements.settingsTimestampFormatSelect;
    this.settingsTimestampPositionSelect = elements.settingsTimestampPositionSelect;
    this.settingsFontSelect = elements.settingsFontSelect;
    this.settingsFontsizeSelect = elements.settingsFontsizeSelect;
    this.settingsThemeSelect = elements.settingsThemeSelect;
    this.customColorContainer = elements.customColorContainer;
    this.colorSurfaceInput = elements.colorSurfaceInput;
    this.colorBezelInput = elements.colorBezelInput;
    this.colorPaperInput = elements.colorPaperInput;
    this.colorInkInput = elements.colorInkInput;
    this.heightSlider = elements.heightSlider;
    this.heightSliderContainer = elements.heightSliderContainer;
    this.cancelSettingsBtn = elements.cancelSettingsBtn;
    this.saveSettingsBtn = elements.saveSettingsBtn;
    this.toast = elements.toast;
    this.toastMessage = elements.toastMessage;
    
    this._onNewDraftConfirm = null;
    this._onGDriveConfirm = null;
    this._onSaveSettingsConfirm = null;
    this._toastTimer = null;
  }
  
  /**
   * Update the word count display.
   * @param {string} text - Current text content.
   */
  updateWordCount(text) {
    const trimmed = text.trim();
    if (!trimmed) {
      this.wordCount.textContent = '0 words';
      return;
    }
    const count = trimmed.split(/\s+/).length;
    this.wordCount.textContent = `${count.toLocaleString()} word${count === 1 ? '' : 's'}`;
  }
  
  /**
   * Scroll the e-paper screen to the bottom to keep cursor visible.
   */
  scrollToBottom() {
    this.screen.scrollTop = this.screen.scrollHeight;
  }
  
  /**
   * Get the current draft title.
   */
  getTitle() {
    return this.titleInput.value.trim() || 'Untitled Draft';
  }
  
  /**
   * Set the draft title.
   * @param {string} title
   */
  setTitle(title) {
    this.titleInput.value = title || 'Untitled Draft';
  }
  
  /**
   * Show the "New Draft" confirmation dialog.
   * @param {Function} onConfirm - Called if user confirms.
   */
  showNewDraftDialog(onConfirm) {
    this._onNewDraftConfirm = onConfirm;
    this.dialog.showModal();
  }
  
  /**
   * Update auth button UI according to user state.
   * @param {Object|null} user - Firebase User object
   */
  setAuthState(user) {
    if (!this.authBtn || !this.authLabel) return;
    
    if (user) {
      const displayName = user.displayName || user.email.split('@')[0];
      this.authLabel.textContent = displayName;
      this.authBtn.title = `Signed in as ${user.displayName || user.email} (click to Sign Out)`;
      this.authBtn.setAttribute('aria-label', `Signed in as ${user.displayName || user.email}, Sign Out`);
      
      // Avatar icon
      if (user.photoURL) {
        let avatar = this.authBtn.querySelector('.user-avatar');
        if (!avatar) {
          avatar = document.createElement('img');
          avatar.className = 'user-avatar';
          avatar.alt = displayName;
          const svgIcon = this.authBtn.querySelector('svg');
          if (svgIcon) svgIcon.replaceWith(avatar);
        }
        avatar.src = user.photoURL;
      }
    } else {
      this.authLabel.textContent = 'Sign In';
      this.authBtn.title = 'Sign In with Google';
      this.authBtn.setAttribute('aria-label', 'Sign In with Google');
      
      // Revert to SVG icon if avatar exists
      const avatar = this.authBtn.querySelector('.user-avatar');
      if (avatar) {
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('width', '18');
        svg.setAttribute('height', '18');
        svg.setAttribute('viewBox', '0 0 24 24');
        svg.setAttribute('fill', 'none');
        svg.setAttribute('stroke', 'currentColor');
        svg.setAttribute('stroke-width', '2');
        svg.setAttribute('stroke-linecap', 'round');
        svg.setAttribute('stroke-linejoin', 'round');
        svg.setAttribute('class', 'auth-icon');
        svg.innerHTML = '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>';
        avatar.replaceWith(svg);
      }
    }
  }

  /**
   * Show the Google Drive save dialog.
   * @param {string} defaultTitle - Initial title string
   * @param {string} folderName - Destination folder name
   * @param {Function} onConfirm - Called with (title) if confirmed
   */
  showGDriveDialog(defaultTitle, folderName, onConfirm) {
    if (!this.gdriveDialog) return;
    this._onGDriveConfirm = onConfirm;
    if (this.gdriveFolderDisplay) {
      this.gdriveFolderDisplay.textContent = folderName || 'etype_drafts';
    }
    if (this.gdriveTitleInput) {
      this.gdriveTitleInput.value = defaultTitle || 'Untitled Draft';
    }
    this.gdriveDialog.showModal();
    if (this.gdriveTitleInput) {
      this.gdriveTitleInput.select();
    }
  }

  /**
   * Show the Info modal dialog.
   */
  showInfoDialog() {
    if (this.infoDialog) {
      this.infoDialog.showModal();
    }
  }

  /**
   * Show the Settings modal dialog.
   * @param {Object} currentSettings - App settings
   * @param {Function} onSave - Called with (newSettings)
   */
  showSettingsDialog(currentSettings, onSave) {
    if (!this.settingsDialog) return;
    this._onSaveSettingsConfirm = onSave;
    if (this.settingsFolderInput) {
      this.settingsFolderInput.value = currentSettings.driveFolder || 'etype_drafts';
    }
    if (this.settingsWordCountInput) {
      this.settingsWordCountInput.checked = currentSettings.showWordCount !== false;
    }
    if (this.settingsCursorBlinkInput) {
      this.settingsCursorBlinkInput.checked = currentSettings.cursorBlink !== false;
    }
    if (this.settingsEnableTimestampInput) {
      this.settingsEnableTimestampInput.checked = currentSettings.enableTimestamp !== false;
    }
    if (this.settingsTimestampFormatSelect) {
      this.settingsTimestampFormatSelect.value = currentSettings.timestampFormat || 'YYYY-MM-DD HH-mm';
    }
    if (this.settingsTimestampPositionSelect) {
      const pos = (currentSettings.enableTimestamp === false || currentSettings.timestampPosition === 'none')
        ? 'none'
        : (currentSettings.timestampPosition || 'after');
      this.settingsTimestampPositionSelect.value = pos;
    }
    this._updateTimestampFormatState();

    if (this.settingsFontSelect) {
      this.settingsFontSelect.value = currentSettings.font || 'courier';
    }
    if (this.settingsFontsizeSelect) {
      this.settingsFontsizeSelect.value = currentSettings.fontSize || 'medium';
    }
    if (this.settingsThemeSelect) {
      this.settingsThemeSelect.value = currentSettings.theme || 'warm-cream';
    }

    const customColors = currentSettings.customColors || {};
    if (this.colorSurfaceInput) this.colorSurfaceInput.value = customColors.surface || '#1a1a1a';
    if (this.colorBezelInput) this.colorBezelInput.value = customColors.bezel || '#d4cfc7';
    if (this.colorPaperInput) this.colorPaperInput.value = customColors.paper || '#f5f0e8';
    if (this.colorInkInput) this.colorInkInput.value = customColors.ink || '#2a2a2a';

    this._updateCustomColorVisibility(this.settingsThemeSelect ? this.settingsThemeSelect.value : 'warm-cream');

    this.settingsDialog.showModal();
  }

  /**
   * Dynamically enables/disables timestamp format selection based on position choice.
   */
  _updateTimestampFormatState() {
    if (!this.settingsTimestampPositionSelect || !this.settingsTimestampFormatSelect) return;
    const isNone = this.settingsTimestampPositionSelect.value === 'none';
    this.settingsTimestampFormatSelect.disabled = isNone;
    const formatLabel = document.getElementById('label-timestamp-format');
    if (formatLabel) {
      if (isNone) {
        formatLabel.classList.add('disabled');
      } else {
        formatLabel.classList.remove('disabled');
      }
    }
  }

  /**
   * Apply cursor blinking setting to display cursor element.
   * @param {boolean} enabled
   */
  applyCursorBlinkSetting(enabled = true) {
    if (!this.cursorEl) return;
    if (enabled === false) {
      this.cursorEl.classList.add('no-blink');
    } else {
      this.cursorEl.classList.remove('no-blink');
    }
  }

  /**
   * Toggle visibility of custom color pickers based on theme selection.
   * @param {string} themeKey
   */
  _updateCustomColorVisibility(themeKey) {
    if (!this.customColorContainer) return;
    if (themeKey === 'custom') {
      this.customColorContainer.classList.remove('hidden');
    } else {
      this.customColorContainer.classList.add('hidden');
    }
  }

  /**
   * Calculates luminance of a hex color string and returns a contrasting text/icon color.
   * @param {string} hex
   * @returns {string} High-contrast color hex/rgba
   */
  _getContrastingColor(hex) {
    if (!hex || typeof hex !== 'string') return 'rgba(0, 0, 0, 0.75)';
    let c = hex.replace('#', '').trim();
    if (c.length === 3) {
      c = c.split('').map(x => x + x).join('');
    }
    if (c.length !== 6) return 'rgba(0, 0, 0, 0.75)';

    const r = parseInt(c.substring(0, 2), 16);
    const g = parseInt(c.substring(2, 4), 16);
    const b = parseInt(c.substring(4, 6), 16);

    const luminance = (r * 299 + g * 587 + b * 114) / 1000;
    return luminance < 140 ? 'rgba(255, 255, 255, 0.88)' : 'rgba(0, 0, 0, 0.75)';
  }

  /**
   * Apply preset theme or custom colors to the root document with contrast safety.
   * @param {string} [themeKey='warm-cream']
   * @param {Object} [customColors]
   */
  applyThemeSettings(themeKey = 'warm-cream', customColors = {}) {
    const root = document.documentElement;

    const presetPalettes = {
      'warm-cream': {
        surface: '#1a1a1a',
        bezel: '#d4cfc7',
        paper: '#f5f0e8',
        ink: '#2a2a2a',
      },
      'dark-mode': {
        surface: '#121212',
        bezel: '#282b2e',
        paper: '#1e2022',
        ink: '#e2e2e2',
      },
      'sepia': {
        surface: '#282019',
        bezel: '#ded3c4',
        paper: '#f4ecd8',
        ink: '#3c2f23',
      },
      'high-contrast': {
        surface: '#000000',
        bezel: '#d0d0d0',
        paper: '#ffffff',
        ink: '#000000',
      },
    };

    let palette = presetPalettes[themeKey];
    if (themeKey === 'custom' || !palette) {
      palette = {
        surface: customColors.surface || '#1a1a1a',
        bezel: customColors.bezel || '#d4cfc7',
        paper: customColors.paper || '#f5f0e8',
        ink: customColors.ink || '#2a2a2a',
      };
    }

    const iconColor = this._getContrastingColor(palette.surface);
    const toolbarTextColor = this._getContrastingColor(palette.bezel);

    root.style.setProperty('--surface-bg', palette.surface);
    root.style.setProperty('--device-bezel', palette.bezel);
    root.style.setProperty('--device-bezel-dark', palette.bezel);
    root.style.setProperty('--paper-bg', palette.paper);
    root.style.setProperty('--ink-color', palette.ink);
    root.style.setProperty('--cursor-color', palette.ink);
    root.style.setProperty('--icon-color', iconColor);
    root.style.setProperty('--toolbar-text', toolbarTextColor);
  }

  /**
   * Apply font family and font size to text display.
   * @param {string} [fontKey='courier']
   * @param {string} [fontSizeKey='medium']
   */
  applyFontSettings(fontKey = 'courier', fontSizeKey = 'medium') {
    const display = document.getElementById('text-display');
    if (!display) return;

    const fontFamilies = {
      'courier': "'Courier Prime', 'Courier New', Courier, monospace",
      'special-elite': "'Special Elite', 'Courier New', Courier, monospace",
      'cutive-mono': "'Cutive Mono', 'Courier New', Courier, monospace",
      'anonymous-pro': "'Anonymous Pro', monospace",
      'space-mono': "'Space Mono', monospace",
      'fira-code': "'Fira Code', monospace",
    };

    const fontSizes = {
      'small': '0.9rem',
      'medium': '1.05rem',
      'large': '1.25rem',
      'xlarge': '1.5rem',
    };

    const family = fontFamilies[fontKey] || fontFamilies['courier'];
    const size = fontSizes[fontSizeKey] || fontSizes['medium'];

    display.style.setProperty('--active-font', family);
    display.style.setProperty('--active-font-size', size);
  }

  /**
   * Apply typing viewport height percentage (15% to 100%).
   * @param {number} percentage - 15 to 100
   */
  applyViewportHeight(percentage = 100) {
    const pct = Math.max(15, Math.min(100, Number(percentage) || 100));
    const vhHeight = (0.72 * pct).toFixed(2);
    if (this.screen) {
      this.screen.style.height = `${vhHeight}vh`;
      this.scrollToBottom();
    }
    if (this.heightSliderContainer) {
      this.heightSliderContainer.style.height = `${vhHeight}vh`;
    }
    if (this.heightSlider) {
      this.heightSlider.value = String(pct);
    }
  }

  /**
   * Toggle visibility of the word count in the status bar.
   * @param {boolean} visible
   */
  setWordCountVisibility(visible) {
    if (!this.wordCount) return;
    if (visible) {
      this.wordCount.classList.remove('hidden');
    } else {
      this.wordCount.classList.add('hidden');
    }
  }

  /**
   * Display a floating toast notification.
   * @param {string} message
   * @param {number} [duration=3500]
   */
  showToast(message, duration = 3500) {
    if (!this.toast || !this.toastMessage) return;
    this.toastMessage.textContent = message;
    this.toast.classList.remove('hidden');

    if (this._toastTimer) clearTimeout(this._toastTimer);
    this._toastTimer = setTimeout(() => {
      this.toast.classList.add('hidden');
    }, duration);
  }

  /**
   * Bind UI event handlers.
   * @param {Object} handlers
   * @param {Function} handlers.onNew - Called when "New Draft" button clicked
   * @param {Function} handlers.onDownload - Called when "Download" button clicked
   * @param {Function} handlers.onGDrive - Called when "Save to Google Drive" button clicked
   * @param {Function} handlers.onTitleChange - Called when title input changes
   * @param {Function} handlers.onAuth - Called when auth button clicked
   * @param {Function} handlers.onSaveSettings - Called with ({ driveFolder, showWordCount })
   */
  bindHandlers(handlers) {
    // Auth button
    if (this.authBtn) {
      this.authBtn.addEventListener('click', () => {
        if (handlers.onAuth) handlers.onAuth();
      });
    }

    // Google Drive button
    if (this.gdriveBtn) {
      this.gdriveBtn.addEventListener('click', () => {
        if (handlers.onGDrive) handlers.onGDrive();
      });
    }

    // Info button
    if (this.infoBtn) {
      this.infoBtn.addEventListener('click', () => {
        this.showInfoDialog();
      });
    }

    // Settings button
    if (this.settingsBtn) {
      this.settingsBtn.addEventListener('click', () => {
        if (handlers.onRequestSettings) handlers.onRequestSettings();
      });
    }

    // Toggle custom color pickers visibility when theme select changes
    if (this.settingsThemeSelect) {
      this.settingsThemeSelect.addEventListener('change', (e) => {
        this._updateCustomColorVisibility(e.target.value);
      });
    }

    // Toggle timestamp format select disabled state when timestamp position changes
    if (this.settingsTimestampPositionSelect) {
      this.settingsTimestampPositionSelect.addEventListener('change', () => {
        this._updateTimestampFormatState();
      });
    }

    // Bezel viewport height slider
    if (this.heightSlider) {
      this.heightSlider.addEventListener('input', (e) => {
        const val = Number(e.target.value) || 100;
        this.applyViewportHeight(val);
        if (handlers.onViewportHeightChange) {
          handlers.onViewportHeightChange(val);
        }
      });
    }

    // Close Info dialog
    if (this.closeInfoBtn && this.infoDialog) {
      this.closeInfoBtn.addEventListener('click', () => {
        this.infoDialog.close();
      });
      this.infoDialog.addEventListener('click', (e) => {
        if (e.target === this.infoDialog) this.infoDialog.close();
      });
    }

    // Settings Dialog save & cancel
    if (this.saveSettingsBtn && this.settingsDialog) {
      this.saveSettingsBtn.addEventListener('click', () => {
        const driveFolder = (this.settingsFolderInput ? this.settingsFolderInput.value : '').trim() || 'etype_drafts';
        const showWordCount = this.settingsWordCountInput ? this.settingsWordCountInput.checked : true;
        const cursorBlink = this.settingsCursorBlinkInput ? this.settingsCursorBlinkInput.checked : true;
        const timestampPosition = this.settingsTimestampPositionSelect ? this.settingsTimestampPositionSelect.value : 'after';
        const enableTimestamp = timestampPosition !== 'none';
        const timestampFormat = this.settingsTimestampFormatSelect ? this.settingsTimestampFormatSelect.value : 'YYYY-MM-DD HH-mm';
        const font = this.settingsFontSelect ? this.settingsFontSelect.value : 'courier';
        const fontSize = this.settingsFontsizeSelect ? this.settingsFontsizeSelect.value : 'medium';
        const theme = this.settingsThemeSelect ? this.settingsThemeSelect.value : 'warm-cream';
        const customColors = {
          surface: this.colorSurfaceInput ? this.colorSurfaceInput.value : '#1a1a1a',
          bezel: this.colorBezelInput ? this.colorBezelInput.value : '#d4cfc7',
          paper: this.colorPaperInput ? this.colorPaperInput.value : '#f5f0e8',
          ink: this.colorInkInput ? this.colorInkInput.value : '#2a2a2a',
        };

        const cb = this._onSaveSettingsConfirm;
        this._onSaveSettingsConfirm = null;
        this.settingsDialog.close();
        if (cb) {
          cb({
            driveFolder,
            showWordCount,
            cursorBlink,
            enableTimestamp,
            timestampFormat,
            timestampPosition,
            font,
            fontSize,
            theme,
            customColors,
          });
        }
      });
    }

    if (this.cancelSettingsBtn && this.settingsDialog) {
      this.cancelSettingsBtn.addEventListener('click', () => {
        this._onSaveSettingsConfirm = null;
        this.settingsDialog.close();
      });
      this.settingsDialog.addEventListener('click', (e) => {
        if (e.target === this.settingsDialog) {
          this._onSaveSettingsConfirm = null;
          this.settingsDialog.close();
        }
      });
      this.settingsDialog.addEventListener('close', () => {
        this._onSaveSettingsConfirm = null;
      });
    }

    // New Draft button → show dialog
    this.newBtn.addEventListener('click', () => {
      if (handlers.onNew) handlers.onNew();
    });
    
    // Copy button
    if (this.copyBtn) {
      this.copyBtn.addEventListener('click', () => {
        if (handlers.onCopy) handlers.onCopy();
      });
    }

    // Download button
    this.downloadBtn.addEventListener('click', () => {
      if (handlers.onDownload) handlers.onDownload();
    });
    
    // Title input change
    this.titleInput.addEventListener('input', () => {
      if (handlers.onTitleChange) handlers.onTitleChange(this.getTitle());
    });
    
    // New draft Dialog confirm
    this.confirmBtn.addEventListener('click', () => {
      const cb = this._onNewDraftConfirm;
      this._onNewDraftConfirm = null;
      this.dialog.close();
      if (cb) {
        cb();
      }
    });
    
    // New draft Dialog cancel
    this.cancelBtn.addEventListener('click', () => {
      this._onNewDraftConfirm = null;
      this.dialog.close();
    });
    
    // Close new draft dialog on backdrop click
    this.dialog.addEventListener('click', (e) => {
      if (e.target === this.dialog) {
        this._onNewDraftConfirm = null;
        this.dialog.close();
      }
    });
    
    // Close new draft dialog on Escape
    this.dialog.addEventListener('close', () => {
      this._onNewDraftConfirm = null;
    });

    // Google Drive Dialog confirm
    if (this.confirmGdriveBtn) {
      this.confirmGdriveBtn.addEventListener('click', () => {
        const title = (this.gdriveTitleInput ? this.gdriveTitleInput.value : '').trim();
        const cb = this._onGDriveConfirm;
        this._onGDriveConfirm = null;
        this.gdriveDialog.close();
        if (cb) {
          cb(title || this.getTitle());
        }
      });
    }

    // Google Drive Dialog cancel
    if (this.cancelGdriveBtn) {
      this.cancelGdriveBtn.addEventListener('click', () => {
        this._onGDriveConfirm = null;
        this.gdriveDialog.close();
      });
    }

    if (this.gdriveDialog) {
      this.gdriveDialog.addEventListener('click', (e) => {
        if (e.target === this.gdriveDialog) {
          this._onGDriveConfirm = null;
          this.gdriveDialog.close();
        }
      });
    }

    // Bind close events on all dialogs to refocus typewriter
    const allDialogs = [this.dialog, this.gdriveDialog, this.infoDialog, this.settingsDialog];
    allDialogs.forEach((d) => {
      if (d) {
        d.addEventListener('close', () => {
          if (handlers.onDialogClose) handlers.onDialogClose();
        });
      }
    });
  }
}
