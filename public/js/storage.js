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
