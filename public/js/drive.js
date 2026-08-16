// drive.js — Google Drive API integration for etype_drafts folder and uploads

import { clearAccessToken } from './auth.js';

const DRIVE_API_BASE = 'https://www.googleapis.com/drive/v3';
const UPLOAD_API_BASE = 'https://www.googleapis.com/upload/drive/v3';
const FOLDER_NAME = 'etype_drafts';

let cachedFolderId = null;
let cachedFolderName = null;

/**
 * Searches for the specified folder in the user's Google Drive.
 * Creates it if it does not exist.
 * @param {string} accessToken - Google OAuth Access Token
 * @param {string} [folderName='etype_drafts'] - Target folder name
 * @returns {Promise<string>} Google Drive Folder ID
 */
export async function getOrCreateDraftsFolder(accessToken, folderName = FOLDER_NAME) {
  const targetFolder = folderName.trim() || FOLDER_NAME;
  if (cachedFolderId && cachedFolderName === targetFolder) return cachedFolderId;

  if (!accessToken) {
    throw new Error('No Google access token available. Please sign in with Google.');
  }

  // 1. Search for existing folder
  const query = encodeURIComponent(`name='${targetFolder}' and mimeType='application/vnd.google-apps.folder' and trashed=false`);
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
    cachedFolderId = data.files[0].id;
    cachedFolderName = targetFolder;
    return cachedFolderId;
  }

  // 2. Create the folder if missing
  const createResponse = await fetch(`${DRIVE_API_BASE}/files`, {
    method: 'POST',
    headers: {
      'Authorization': `Bearer ${accessToken}`,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      name: targetFolder,
      mimeType: 'application/vnd.google-apps.folder'
    })
  });

  if (createResponse.status === 401) {
    clearAccessToken();
    resetDriveCache();
    throw new Error('Google authorization expired. Please sign in again.');
  }

  if (!createResponse.ok) {
    const errText = await createResponse.text();
    throw new Error(`Failed to create '${targetFolder}' folder in Google Drive: ${errText}`);
  }

  const createdFolder = await createResponse.json();
  cachedFolderId = createdFolder.id;
  cachedFolderName = targetFolder;
  return cachedFolderId;
}

/**
 * Saves a markdown draft to Google Drive.
 * @param {string} accessToken - Google OAuth Access Token
 * @param {string} title - File title/name
 * @param {string} content - Markdown draft content
 * @param {string} [folderName='etype_drafts'] - Target folder name
 * @returns {Promise<{id: string, name: string, webViewLink: string}>} Uploaded file details
 */
export async function saveDraftToDrive(accessToken, title, content, folderName = FOLDER_NAME) {
  const folderId = await getOrCreateDraftsFolder(accessToken, folderName);
  const filename = (title.trim() || 'Untitled Draft') + '.md';

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
