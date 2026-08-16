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
  try {
    const raw = localStorage.getItem(SETTINGS_KEY);
    if (!raw) return { driveFolder: 'etype_drafts', showWordCount: true };
    const parsed = JSON.parse(raw);
    return {
      driveFolder: parsed.driveFolder || 'etype_drafts',
      showWordCount: parsed.showWordCount !== false,
    };
  } catch (e) {
    return { driveFolder: 'etype_drafts', showWordCount: true };
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
 * Download text as a markdown (.md) file.
 * @param {string} text - The markdown content.
 * @param {string} title - Used for the filename.
 */
export function downloadAsMarkdown(text, title) {
  const filename = sanitizeFilename(title || 'untitled-draft') + '.md';
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
