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
    this.settingsDialog = elements.settingsDialog;
    this.settingsFolderInput = elements.settingsFolderInput;
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
   * @param {Object} currentSettings - { driveFolder }
   * @param {Function} onSave - Called with (newSettings)
   */
  showSettingsDialog(currentSettings, onSave) {
    if (!this.settingsDialog) return;
    this._onSaveSettingsConfirm = onSave;
    if (this.settingsFolderInput) {
      this.settingsFolderInput.value = currentSettings.driveFolder || 'etype_drafts';
    }
    this.settingsDialog.showModal();
  }

  /**
   * Toggle distraction-free auto-hide of toolbar and floating controls.
   * @param {boolean} hide - True to hide UI, false to reveal UI
   */
  setAutohide(hide) {
    if (hide) {
      document.body.classList.add('autohide-active');
    } else {
      document.body.classList.remove('autohide-active');
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
   * @param {Function} handlers.onSaveSettings - Called with ({ driveFolder })
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
        const cb = this._onSaveSettingsConfirm;
        this._onSaveSettingsConfirm = null;
        this.settingsDialog.close();
        if (cb) {
          cb({ driveFolder });
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
