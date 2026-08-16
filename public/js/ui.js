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
    this.dialog = elements.dialog;
    this.confirmBtn = elements.confirmBtn;
    this.cancelBtn = elements.cancelBtn;
    
    this._onNewDraftConfirm = null;
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
   * Bind UI event handlers.
   * @param {Object} handlers
   * @param {Function} handlers.onNew - Called when "New Draft" button clicked
   * @param {Function} handlers.onDownload - Called when "Download" button clicked
   * @param {Function} handlers.onTitleChange - Called when title input changes
   */
  bindHandlers(handlers) {
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
    
    // Dialog confirm
    this.confirmBtn.addEventListener('click', () => {
      this.dialog.close();
      if (this._onNewDraftConfirm) {
        this._onNewDraftConfirm();
        this._onNewDraftConfirm = null;
      }
    });
    
    // Dialog cancel
    this.cancelBtn.addEventListener('click', () => {
      this.dialog.close();
      this._onNewDraftConfirm = null;
    });
    
    // Close dialog on backdrop click
    this.dialog.addEventListener('click', (e) => {
      if (e.target === this.dialog) {
        this.dialog.close();
        this._onNewDraftConfirm = null;
      }
    });
    
    // Close dialog on Escape (native behavior, but clean up callback)
    this.dialog.addEventListener('close', () => {
      this._onNewDraftConfirm = null;
    });
  }
}
