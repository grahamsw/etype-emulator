// auth.js — Google Authentication via Firebase Auth SDK & OAuth Scopes

import { initializeApp } from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js';
import { 
  getAuth, 
  signInWithPopup, 
  signOut, 
  GoogleAuthProvider, 
  onAuthStateChanged 
} from 'https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js';

// Firebase Web Configuration
const firebaseConfig = {
  projectId: "etype-emulator-app",
  appId: "1:14967080007:web:7957aaeeb05d0c5d589d62",
  storageBucket: "etype-emulator-app.firebasestorage.app",
  apiKey: "AIzaSyCny1COQMFSzPrMK4COeXgVseCxbgSAEPE",
  authDomain: "etype-emulator-app.firebaseapp.com",
  messagingSenderId: "14967080007",
};

const app = initializeApp(firebaseConfig);
export const auth = getAuth(app);

const googleProvider = new GoogleAuthProvider();
// Request Google Drive file scope for saving drafts to etype_drafts folder
googleProvider.addScope('https://www.googleapis.com/auth/drive.file');

let currentAccessToken = localStorage.getItem('etype_gdrive_token') || null;

/**
 * Sign in with Google using popup.
 * @returns {Promise<{user: import('https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js').User, accessToken: string|null}>}
 */
export async function loginWithGoogle() {
  try {
    const result = await signInWithPopup(auth, googleProvider);
    const credential = GoogleAuthProvider.credentialFromResult(result);
    if (credential && credential.accessToken) {
      currentAccessToken = credential.accessToken;
      localStorage.setItem('etype_gdrive_token', currentAccessToken);
    }
    return { user: result.user, accessToken: currentAccessToken };
  } catch (error) {
    if (error.code !== 'auth/popup-closed-by-user') {
      console.error('E-Type Auth Error:', error);
    }
    throw error;
  }
}

/**
 * Get current Google OAuth access token.
 * @returns {string|null}
 */
export function getAccessToken() {
  if (!currentAccessToken) {
    currentAccessToken = localStorage.getItem('etype_gdrive_token');
  }
  return currentAccessToken;
}

/**
 * Clear cached access token (e.g. on 401 error).
 */
export function clearAccessToken() {
  currentAccessToken = null;
  localStorage.removeItem('etype_gdrive_token');
}

/**
 * Sign out current user.
 * @returns {Promise<void>}
 */
export async function logout() {
  try {
    await signOut(auth);
    clearAccessToken();
  } catch (error) {
    console.error('E-Type Logout Error:', error);
    throw error;
  }
}

/**
 * Subscribe to auth state changes.
 * @param {Function} callback - (user: User|null) => void
 * @returns {Function} Unsubscribe function
 */
export function onUserChanged(callback) {
  return onAuthStateChanged(auth, (user) => {
    if (!user) {
      clearAccessToken();
    }
    callback(user);
  });
}
