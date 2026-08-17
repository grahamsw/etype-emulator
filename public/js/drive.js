// drive.js — Google Drive API integration for etype_drafts folder and uploads

import { clearAccessToken } from './auth.js';
import { processFilename } from './storage.js';

const DRIVE_API_BASE = 'https://www.googleapis.com/drive/v3';
const UPLOAD_API_BASE = 'https://www.googleapis.com/upload/drive/v3';
const FOLDER_NAME = 'etype_drafts';

let cachedFolderId = null;
let cachedFolderName = null;

/**
 * Searches for or creates a nested folder path in Google Drive.
 * e.g. "Hemingway/old man" -> creates "Hemingway" then "old man" inside it.
 * @param {string} accessToken - Google OAuth Access Token
 * @param {string} [folderPath='etype_drafts'] - Folder path
 * @returns {Promise<string>} Google Drive Folder ID of the deepest folder
 */
export async function getOrCreateDraftsFolder(accessToken, folderPath = FOLDER_NAME) {
  const normalizedPath = (folderPath || FOLDER_NAME).replace(/\\/g, '/').trim();
  const folderParts = normalizedPath.split('/').map(p => p.trim()).filter(Boolean);

  if (folderParts.length === 0) folderParts.push(FOLDER_NAME);

  if (!accessToken) {
    throw new Error('No Google access token available. Please sign in with Google.');
  }

  let currentParentId = 'root';

  for (const folderName of folderParts) {
    const query = encodeURIComponent(`name='${folderName}' and '${currentParentId}' in parents and mimeType='application/vnd.google-apps.folder' and trashed=false`);
    const searchUrl = `${DRIVE_API_BASE}/files?q=${query}&fields=files(id,name)`;

    const response = await fetch(searchUrl, {
      headers: {
        'Authorization': `Bearer ${accessToken}`
      }
    });

    if (response.status === 401) {
      clearAccessToken();
      resetDriveCache();
      throw new Error('Google authorization expired. Please sign in again.');
    }

    if (!response.ok) {
      const errText = await response.text();
      throw new Error(`Google Drive API error (${response.status}): ${errText}`);
    }

    const data = await response.json();
    if (data.files && data.files.length > 0) {
      currentParentId = data.files[0].id;
    } else {
      // Create folder under currentParentId
      const bodyPayload = {
        name: folderName,
        mimeType: 'application/vnd.google-apps.folder'
      };
      if (currentParentId !== 'root') {
        bodyPayload.parents = [currentParentId];
      }

      const createResponse = await fetch(`${DRIVE_API_BASE}/files`, {
        method: 'POST',
        headers: {
          'Authorization': `Bearer ${accessToken}`,
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(bodyPayload)
      });

      if (createResponse.status === 401) {
        clearAccessToken();
        resetDriveCache();
        throw new Error('Google authorization expired. Please sign in again.');
      }

      if (!createResponse.ok) {
        const errText = await createResponse.text();
        throw new Error(`Google Drive API error (${createResponse.status}): ${errText}`);
      }

      const createdData = await createResponse.json();
      currentParentId = createdData.id;
    }
  }

  return currentParentId;
}

/**
 * Saves a markdown draft to Google Drive.
 * @param {string} accessToken - Google OAuth Access Token
 * @param {string} title - File title/path (can contain subfolders, e.g. "old man/and the sea")
 * @param {string} content - Markdown draft content
 * @param {string} [rootFolderName='etype_drafts'] - Root folder name from settings
 * @param {Object} [settings] - App settings for timestamp formatting
 * @returns {Promise<{id: string, name: string, webViewLink: string}>} Uploaded file details
 */
export async function saveDraftToDrive(accessToken, title, content, rootFolderName = FOLDER_NAME, settings = {}) {
  const { subfolderPath, filename } = processFilename(title, settings);
  const targetFolderPath = [rootFolderName, subfolderPath].filter(Boolean).join('/');

  const folderId = await getOrCreateDraftsFolder(accessToken, targetFolderPath);

  const metadata = {
    name: filename,
    mimeType: 'text/markdown',
    parents: [folderId]
  };

  const boundary = 'foo_bar_baz';
  const delimiter = `\r\n--${boundary}\r\n`;
  const closeDelimiter = `\r\n--${boundary}--`;

  const multipartRequestBody =
    delimiter +
    'Content-Type: application/json; charset=UTF-8\r\n\r\n' +
    JSON.stringify(metadata) +
    delimiter +
    'Content-Type: text/markdown; charset=UTF-8\r\n\r\n' +
    content +
    closeDelimiter;

  const uploadUrl = `${UPLOAD_API_BASE}/files?uploadType=multipart&fields=id,name,webViewLink`;

  const response = await fetch(uploadUrl, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${accessToken}`,
      'Content-Type': `multipart/related; boundary=${boundary}`
    },
    body: multipartRequestBody
  });

  if (response.status === 401) {
    clearAccessToken();
    resetDriveCache();
    throw new Error('Google authorization expired. Please sign in again.');
  }

  if (!response.ok) {
    const errText = await response.text();
    throw new Error(`Failed to upload file to Google Drive: ${errText}`);
  }

  return await response.json();
}

/**
 * Resets cached folder ID on user sign out.
 */
export function resetDriveCache() {
  cachedFolderId = null;
}
