// flow.js — Flow Mode state machine and pacing controller

export class FlowController {
  /**
   * @param {Object} options
   * @param {Function} [options.onTick] - Called every second with ({ remainingSeconds, totalSeconds, progress })
   * @param {Function} [options.onWarningChange] - Called with (boolean) when idle enters/exits warning zone
   * @param {Function} [options.onDecayChar] - Called repeatedly to delete a character when decay is active
   * @param {Function} [options.onComplete] - Called when timer reaches 0 with session statistics
   * @param {Function} [options.onAbort] - Called if session is cancelled early
   */
  constructor(options = {}) {
    this.onTick = options.onTick || null;
    this.onWarningChange = options.onWarningChange || null;
    this.onDecayChar = options.onDecayChar || null;
    this.onComplete = options.onComplete || null;
    this.onAbort = options.onAbort || null;

    this.isActive = false;
    this.durationMinutes = 5;
    this.targetWpm = 30;
    this.eraseSpeed = 3;
    this.folderName = 'flow_writings';

    this.totalSeconds = 0;
    this.remainingSeconds = 0;
    this.timerInterval = null;
    this.checkInterval = null;
    this.decayInterval = null;

    this.lastKeystrokeTime = 0;
    this.gracePeriodMs = 3000;
    this.decayIntervalMs = 250;
    this.isWarning = false;
    this.isDecaying = false;

    this.sessionStartTime = null;
    this.keystrokeCount = 0;
  }

  /**
   * Calculate grace period in milliseconds based on target WPM.
   * Higher WPM means less allowed pause between keystrokes.
   * @param {number} wpm - Target WPM (10 to 80)
   * @returns {number} Milliseconds before decay starts
   */
  static calculateGracePeriod(wpm) {
    const safeWpm = Math.max(10, Math.min(80, Number(wpm) || 30));
    // At 30 WPM: (60 / 30) * 1.5 = 3.0s grace period
    // At 60 WPM: (60 / 60) * 1.5 = 1.5s grace period
    // At 15 WPM: (60 / 15) * 1.5 = 6.0s grace period
    const seconds = Math.max(1.5, (60 / safeWpm) * 1.5);
    return Math.round(seconds * 1000);
  }

  /**
   * Calculate character decay interval in milliseconds based on Erase Speed level (1 to 5).
   * Level 1 (Slow): 1000ms (~1 char/sec)
   * Level 2 (Relaxed): 500ms (~2 chars/sec)
   * Level 3 (Normal): 250ms (~4 chars/sec)
   * Level 4 (Fast): 166ms (~6 chars/sec)
   * Level 5 (Rapid): 125ms (~8 chars/sec)
   * @param {number} speed - 1 to 5
   * @returns {number} Milliseconds per character deletion
   */
  static calculateDecayIntervalMs(speed) {
    const safeSpeed = Math.max(1, Math.min(5, Number(speed) || 3));
    const rates = { 1: 1000, 2: 500, 3: 250, 4: 166, 5: 125 };
    return rates[safeSpeed] || 250;
  }

  /**
   * Start a new Flow session.
   * @param {Object} config
   * @param {number} config.durationMinutes - 1 to 10
   * @param {number} config.targetWpm - 10 to 80
   * @param {number} [config.eraseSpeed=3] - 1 to 5
   * @param {string} [config.folderName='flow_writings']
   */
  start(config = {}) {
    this.stop(false);

    this.durationMinutes = Math.max(1, Math.min(10, Number(config.durationMinutes) || 5));
    this.targetWpm = Math.max(10, Math.min(80, Number(config.targetWpm) || 30));
    this.eraseSpeed = Math.max(1, Math.min(5, Number(config.eraseSpeed) || 3));
    this.folderName = (config.folderName || 'flow_writings').trim() || 'flow_writings';

    this.totalSeconds = this.durationMinutes * 60;
    this.remainingSeconds = this.totalSeconds;
    this.gracePeriodMs = FlowController.calculateGracePeriod(this.targetWpm);
    this.decayIntervalMs = FlowController.calculateDecayIntervalMs(this.eraseSpeed);

    this.isActive = true;
    this.lastKeystrokeTime = Date.now();
    this.sessionStartTime = Date.now();
    this.keystrokeCount = 0;
    this.isWarning = false;
    this.isDecaying = false;

    if (this.onTick) {
      this.onTick({
        remainingSeconds: this.remainingSeconds,
        totalSeconds: this.totalSeconds,
        progress: 0,
        targetWpm: this.targetWpm,
      });
    }

    // 1-second countdown timer
    this.timerInterval = setInterval(() => this._handleTimerTick(), 1000);

    // Fast loop (every 100ms) to monitor keystroke idle time
    this.checkInterval = setInterval(() => this._checkIdleState(), 100);
  }

  /**
   * Called whenever the user strikes a key on the typewriter.
   */
  recordKeystroke() {
    if (!this.isActive) return;

    this.lastKeystrokeTime = Date.now();
    this.keystrokeCount++;

    // Immediately stop decay and warning if active
    if (this.isDecaying) {
      this._stopDecaying();
    }
    if (this.isWarning) {
      this.isWarning = false;
      if (this.onWarningChange) this.onWarningChange(false);
    }
  }

  /**
   * Stop or abort the current session.
   * @param {boolean} [completed=false]
   */
  stop(completed = false) {
    if (!this.isActive) return;

    this.isActive = false;
    this._clearTimers();
    this._stopDecaying();

    if (this.isWarning) {
      this.isWarning = false;
      if (this.onWarningChange) this.onWarningChange(false);
    }

    if (!completed && this.onAbort) {
      this.onAbort();
    }
  }

  // ---- Internal Lifecycle & Checks ----

  _handleTimerTick() {
    if (!this.isActive) return;

    this.remainingSeconds--;

    const progress = Math.min(1, (this.totalSeconds - this.remainingSeconds) / this.totalSeconds);

    if (this.onTick) {
      this.onTick({
        remainingSeconds: this.remainingSeconds,
        totalSeconds: this.totalSeconds,
        progress,
        targetWpm: this.targetWpm,
      });
    }

    if (this.remainingSeconds <= 0) {
      this._handleComplete();
    }
  }

  _checkIdleState() {
    if (!this.isActive) return;

    const idleTime = Date.now() - this.lastKeystrokeTime;
    const warningThreshold = this.gracePeriodMs * 0.7; // 70% of grace period

    // Enter or exit warning phase
    if (idleTime >= warningThreshold && idleTime < this.gracePeriodMs) {
      if (!this.isWarning) {
        this.isWarning = true;
        if (this.onWarningChange) this.onWarningChange(true);
      }
    } else if (idleTime < warningThreshold) {
      if (this.isWarning) {
        this.isWarning = false;
        if (this.onWarningChange) this.onWarningChange(false);
      }
    }

    // Enter decay phase
    if (idleTime >= this.gracePeriodMs && !this.isDecaying) {
      this._startDecaying();
    }
  }

  _startDecaying() {
    this.isDecaying = true;
    if (this.onWarningChange) this.onWarningChange(true);

    // Decay rate based on configured erase speed
    if (this.decayInterval) clearInterval(this.decayInterval);
    this.decayInterval = setInterval(() => {
      if (!this.isActive || !this.isDecaying) {
        this._stopDecaying();
        return;
      }
      if (this.onDecayChar) {
        const hasMore = this.onDecayChar();
        if (hasMore === false) {
          // No more characters to decay
          this._stopDecaying();
        }
      }
    }, this.decayIntervalMs);
  }

  _stopDecaying() {
    this.isDecaying = false;
    if (this.decayInterval) {
      clearInterval(this.decayInterval);
      this.decayInterval = null;
    }
  }

  _handleComplete() {
    this.isActive = false;
    this._clearTimers();
    this._stopDecaying();

    if (this.isWarning) {
      this.isWarning = false;
      if (this.onWarningChange) this.onWarningChange(false);
    }

    if (this.onComplete) {
      this.onComplete({
        durationMinutes: this.durationMinutes,
        targetWpm: this.targetWpm,
        folderName: this.folderName,
        keystrokes: this.keystrokeCount,
        durationSeconds: this.totalSeconds,
      });
    }
  }

  _clearTimers() {
    if (this.timerInterval) {
      clearInterval(this.timerInterval);
      this.timerInterval = null;
    }
    if (this.checkInterval) {
      clearInterval(this.checkInterval);
      this.checkInterval = null;
    }
    if (this.decayInterval) {
      clearInterval(this.decayInterval);
      this.decayInterval = null;
    }
  }
}
