// storage.js — Draft persistence and file export

const STORAGE_KEY = 'etype-draft';

/**
 * Save draft data to localStorage.
 * @param {Object} data - { text, title, createdAt, lastModified }
 */
export function save(data) {
  try {
    const payload = {
      text: data.text || '',
      title: data.title || 'Untitled Draft',
      createdAt: data.createdAt || new Date().toISOString(),
      lastModified: new Date().toISOString(),
    };
    localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
  } catch (e) {
    console.warn('E-Type: Failed to save draft to localStorage', e);
  }
}

/**
 * Load draft data from localStorage.
 * @returns {Object|null} The saved draft data, or null if none exists.
 */
export function load() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    return JSON.parse(raw);
  } catch (e) {
    console.warn('E-Type: Failed to load draft from localStorage', e);
    return null;
  }
}

const SETTINGS_KEY = 'etype-settings';

/**
 * Save app settings to localStorage.
 * @param {Object} settings - { driveFolder, theme, font }
 */
export function saveSettings(settings) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
  } catch (e) {
    console.warn('E-Type: Failed to save settings', e);
  }
}

/**
 * Load app settings from localStorage.
 * @returns {Object} Saved settings or defaults.
 */
export function loadSettings() {
  const defaults = {
    driveFolder: 'etype_drafts',
    showWordCount: true,
    cursorBlink: true,
    enableTimestamp: true,
    timestampFormat: 'YYYY-MM-DD HH-mm',
    timestampPosition: 'after',
    font: 'courier',
    fontSize: 'medium',
    theme: 'warm-cream',
    viewportHeight: 100,
    customColors: {
      surface: '#1a1a1a',
      bezel: '#d4cfc7',
      paper: '#f5f0e8',
      ink: '#2a2a2a',
    },
  };
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (!raw) return defaults;
    const parsed = JSON.parse(raw);

    // Synchronize enableTimestamp & timestampPosition 'none'
    let position = parsed.timestampPosition || defaults.timestampPosition;
    let enabled = parsed.enableTimestamp !== undefined ? parsed.enableTimestamp : defaults.enableTimestamp;
    if (!enabled) {
      position = 'none';
    } else if (position === 'none') {
      enabled = false;
    }

    return {
      ...defaults,
      ...parsed,
      enableTimestamp: enabled,
      timestampPosition: position,
      customColors: {
        ...defaults.customColors,
        ...(parsed.customColors || {}),
      },
    };
  } catch (e) {
    return defaults;
  }
}

/**
 * Clear saved draft from localStorage.
 */
export function clearStorage() {
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch (e) {
    console.warn('E-Type: Failed to clear localStorage', e);
  }
}

/**
 * Track and increment daily download sequence counter in localStorage.
 * @param {string} titleKey
 * @param {string} dateStr
 * @returns {number} Next sequence number
 */
export function getAndIncrementLocalSequence(titleKey, dateStr) {
  try {
    const key = `etype-seq-${dateStr}-${titleKey}`;
    const current = parseInt(localStorage.getItem(key) || '0', 10);
    const next = current + 1;
    localStorage.setItem(key, String(next));
    return next;
  } catch (e) {
    return 1;
  }
}

/**
 * Format a Date object according to chosen format.
 * @param {Date} [date=new Date()]
 * @param {string} [format='YYYY-MM-DD HH-mm']
 * @param {number|null} [seqNum=null]
 * @returns {string}
 */
export function formatFormattedTimestamp(date = new Date(), format = 'YYYY-MM-DD HH-mm', seqNum = null) {
  const d = date instanceof Date ? date : new Date(date);
  const pad = (n) => String(n).padStart(2, '0');

  const year = d.getFullYear();
  const month = pad(d.getMonth() + 1);
  const day = pad(d.getDate());
  const hours = pad(d.getHours());
  const minutes = pad(d.getMinutes());

  switch (format) {
    case 'YYYY-MM-DD.seq':
      return `${year}-${month}-${day}.${seqNum || 1}`;
    case 'YYYY-MM-DD':
      return `${year}-${month}-${day}`;
    case 'YYYYMMDD-HHmm':
      return `${year}${month}${day}-${hours}${minutes}`;
    case 'ISO':
      return `${year}-${month}-${day}T${hours}-${minutes}`;
    case 'YYYY-MM-DD HH-mm':
    default:
      return `${year}-${month}-${day} ${hours}-${minutes}`;
  }
}

/**
 * Parse raw title/path and format filename & subfolder.
 * @param {string} rawPath - e.g. "old man/and the sea" or "untitled"
 * @param {Object} [settings] - App settings
 * @param {Date} [date=new Date()]
 * @param {number|null} [overrideSeq=null] - Sequence number override
 * @returns {{ subfolderPath: string, cleanTitle: string, filename: string }}
 */
export function processFilename(rawPath, settings = {}, date = new Date(), overrideSeq = null) {
  const currentSettings = {
    enableTimestamp: true,
    timestampFormat: 'YYYY-MM-DD HH-mm',
    timestampPosition: 'after',
    ...settings,
  };

  const normalized = (rawPath || 'untitled-draft').replace(/\\/g, '/').trim();
  const parts = normalized.split('/').map(p => p.trim()).filter(Boolean);

  let rawTitle = 'untitled-draft';
  let subfolderParts = [];

  if (parts.length > 1) {
    rawTitle = parts[parts.length - 1];
    subfolderParts = parts.slice(0, parts.length - 1);
  } else if (parts.length === 1) {
    rawTitle = parts[0];
  }

  const cleanTitle = sanitizeFilename(rawTitle) || 'draft';
  const subfolderPath = subfolderParts.join('/');

  let nameWithoutExt = cleanTitle;
  if (currentSettings.enableTimestamp && currentSettings.timestampPosition !== 'none') {
    let seqNum = overrideSeq;
    if (currentSettings.timestampFormat === 'YYYY-MM-DD.seq' && seqNum === null) {
      const pad = (n) => String(n).padStart(2, '0');
      const dateStr = `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
      seqNum = getAndIncrementLocalSequence(cleanTitle, dateStr);
    }

    const ts = formatFormattedTimestamp(date, currentSettings.timestampFormat, seqNum);
    if (currentSettings.timestampPosition === 'before') {
      nameWithoutExt = `${ts}-${cleanTitle}`;
    } else {
      nameWithoutExt = `${cleanTitle}-${ts}`;
    }
  }

  return {
    subfolderPath,
    cleanTitle,
    filename: `${nameWithoutExt}.md`,
  };
}

/**
 * Download text as a markdown (.md) file.
 * @param {string} text - The markdown content.
 * @param {string} title - Used for the filename.
 * @param {Object} [settings] - App settings for timestamp formatting
 */
export function downloadAsMarkdown(text, title, settings = {}) {
  const { filename } = processFilename(title, settings);
  const blob = new Blob([text], { type: 'text/markdown;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.style.display = 'none';
  document.body.appendChild(link);
  link.click();
  
  // Cleanup
  setTimeout(() => {
    URL.revokeObjectURL(url);
    document.body.removeChild(link);
  }, 100);
}

/**
 * Sanitize a string for use as a filename.
 */
function sanitizeFilename(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9\s-]/g, '')
    .replace(/\s+/g, '-')
    .replace(/-+/g, '-')
    .slice(0, 60) || 'draft';
}

/**
 * Copy text to system clipboard.
 * @param {string} text
 * @returns {Promise<boolean>}
 */
export async function copyToClipboard(text) {
  if (!text) return false;
  try {
    if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch (err) {
    console.warn('E-Type: navigator.clipboard.writeText failed, attempting fallback', err);
  }
  try {
    const textarea = document.createElement('textarea');
    textarea.value = text;
    textarea.setAttribute('readonly', '');
    textarea.style.position = 'fixed';
    textarea.style.top = '0';
    textarea.style.left = '-9999px';
    document.body.appendChild(textarea);
    textarea.focus();
    textarea.select();
    textarea.setSelectionRange(0, textarea.value.length);
    const success = document.execCommand('copy');
    document.body.removeChild(textarea);
    return success;
  } catch (e) {
    console.warn('E-Type: execCommand copy fallback failed', e);
    return false;
  }
}
