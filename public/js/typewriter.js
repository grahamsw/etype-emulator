// typewriter.js — Core e-ink typewriter engine

const RENDER_DELAY = 80;      // ms between characters appearing on screen
const SETTLE_DURATION = 250;  // ms for ink-settle CSS animation

export class Typewriter {
  /**
   * @param {HTMLElement} displayEl - #text-display
   * @param {HTMLTextAreaElement} inputEl - #hidden-input
   * @param {HTMLElement} cursorEl - #cursor
   * @param {Object} options
   * @param {Function} options.onTextChange - callback(text) on every change
   * @param {Function} options.onFirstChar - callback() on first character typed
   */
  constructor(displayEl, inputEl, cursorEl, options = {}) {
    this.displayEl = displayEl;
    this.inputEl = inputEl;
    this.cursorEl = cursorEl;
    this.onTextChange = options.onTextChange || null;
    this.onFirstChar = options.onFirstChar || null;
    this.onTypingStart = options.onTypingStart || null;
    
    this.text = '';           // canonical text buffer (append-only during typing)
    this.queue = [];          // characters waiting to be rendered
    this.isProcessing = false;
    this.hasTyped = false;
    
    this._bindEvents();
  }
  
  // ---- Event Binding ----
  
  _bindEvents() {
    this.inputEl.addEventListener('keydown', (e) => this._onKeyDown(e));
    this.inputEl.addEventListener('beforeinput', (e) => this._onBeforeInput(e));
    this.inputEl.addEventListener('input', (e) => this._onInput(e));
    
    // Prevent cursor repositioning via mouse/touch on the textarea
    this.inputEl.addEventListener('mousedown', (e) => {
      // Allow focus but force cursor to end
      requestAnimationFrame(() => this._forceCursorToEnd());
    });
    
    // Block context menu on textarea
    this.inputEl.addEventListener('contextmenu', (e) => e.preventDefault());
    
    // Clicking the e-paper screen focuses the hidden textarea
    this.displayEl.addEventListener('click', () => this.focus());
    
    // Block selection on textarea by always forcing to end
    this.inputEl.addEventListener('select', () => this._forceCursorToEnd());
    
    // Block paste — typewriter doesn't have paste
    this.inputEl.addEventListener('paste', (e) => e.preventDefault());
    
    // Block drop
    this.inputEl.addEventListener('drop', (e) => e.preventDefault());
  }
  
  // ---- Key Blocking ----
  
  _onKeyDown(e) {
    if (this.onTypingStart) {
      this.onTypingStart();
    }

    // Block destructive / backward-movement keys
    const blockedKeys = ['Backspace', 'Delete', 'ArrowLeft', 'ArrowUp', 'Home'];
    if (blockedKeys.includes(e.key)) {
      e.preventDefault();
      return;
    }
    
    // Block Ctrl/Cmd + Z (undo), X (cut), A (select all)
    if ((e.ctrlKey || e.metaKey) && !e.shiftKey) {
      const blocked = ['z', 'x', 'a'];
      if (blocked.includes(e.key.toLowerCase())) {
        e.preventDefault();
        return;
      }
    }
    
    // Block Ctrl/Cmd + Shift + Z (redo)
    if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      return;
    }
    
    // Tab inserts spaces (for markdown indentation) instead of changing focus
    if (e.key === 'Tab') {
      e.preventDefault();
      // Insert 2 spaces
      const spaces = '  ';
      // Manually insert into textarea at cursor position (which is always at end)
      this.inputEl.value += spaces;
      this.text = this.inputEl.value;
      for (const ch of spaces) {
        this.queue.push(ch);
      }
      this._processQueue();
      this._notifyChange();
    }
  }
  
  _onBeforeInput(e) {
    const blockedTypes = [
      'deleteContentBackward',
      'deleteContentForward',
      'deleteByCut',
      'deleteByDrag',
      'deleteContent',
      'deleteSoftLineBackward',
      'deleteSoftLineForward',
      'deleteHardLineBackward',
      'deleteHardLineForward',
      'deleteWordBackward',
      'deleteWordForward',
      'historyUndo',
      'historyRedo',
    ];
    if (blockedTypes.includes(e.inputType)) {
      e.preventDefault();
    }
  }
  
  // ---- Input Capture ----
  
  _onInput(e) {
    const currentValue = this.inputEl.value;
    
    // Detect added characters (our text is append-only)
    if (currentValue.length > this.text.length) {
      const added = currentValue.slice(this.text.length);
      this.text = currentValue;
      
      for (const char of added) {
        this.queue.push(char);
      }
      this._processQueue();
      this._notifyChange();
      
    } else if (currentValue.length < this.text.length) {
      // Something tried to delete — restore our canonical text
      this.inputEl.value = this.text;
    }
    
    this._forceCursorToEnd();
  }
  
  // ---- Rendering Queue ----
  
  _processQueue() {
    if (this.isProcessing || this.queue.length === 0) return;
    this.isProcessing = true;
    
    const processNext = () => {
      if (this.queue.length === 0) {
        this.isProcessing = false;
        return;
      }
      
      const char = this.queue.shift();
      this._renderChar(char);
      
      // Schedule next character with e-ink delay
      setTimeout(processNext, RENDER_DELAY);
    };
    
    // Render first character immediately
    processNext();
  }
  
  _renderChar(char) {
    if (!this.hasTyped) {
      this.hasTyped = true;
      if (this.onFirstChar) {
        this.onFirstChar();
      }
    }
    
    const span = document.createElement('span');
    span.className = 'char char--new';
    span.textContent = char;
    
    // Insert before cursor
    this.displayEl.insertBefore(span, this.cursorEl);
    
    // Remove animation class after it completes
    setTimeout(() => {
      span.classList.remove('char--new');
    }, SETTLE_DURATION);
    
    // Auto-scroll to keep cursor visible
    this.cursorEl.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
  
  // ---- Public API ----
  
  getText() {
    return this.text;
  }
  
  /**
   * Set text programmatically (e.g., restoring from storage).
   * Renders instantly without animation.
   */
  setText(text) {
    this.text = text;
    this.inputEl.value = text;
    
    // Clear existing rendered characters (preserve cursor)
    const cursor = this.cursorEl;
    
    // Remove all children except cursor
    while (this.displayEl.firstChild) {
      if (this.displayEl.firstChild === cursor) break;
      this.displayEl.removeChild(this.displayEl.firstChild);
    }
    
    // Insert restored text as a plain text node (no animation)
    if (text) {
      this.hasTyped = true;
      const textNode = document.createTextNode(text);
      this.displayEl.insertBefore(textNode, cursor);
    } else {
      this.hasTyped = false;
    }
    
    this._forceCursorToEnd();
    this._notifyChange();
  }
  
  clear() {
    this.text = '';
    this.queue = [];
    this.isProcessing = false;
    this.inputEl.value = '';
    this.hasTyped = false;
    
    // Remove all children except cursor
    while (this.displayEl.firstChild) {
      if (this.displayEl.firstChild === this.cursorEl) break;
      this.displayEl.removeChild(this.displayEl.firstChild);
    }
    
    this._notifyChange();
  }
  
  focus() {
    this.inputEl.focus();
    this._forceCursorToEnd();
  }
  
  // ---- Internal Helpers ----
  
  _forceCursorToEnd() {
    const len = this.inputEl.value.length;
    this.inputEl.setSelectionRange(len, len);
  }
  
  _notifyChange() {
    if (this.onTextChange) {
      this.onTextChange(this.text);
    }
  }
}
