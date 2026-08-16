// auth.js — Google Authentication via Firebase Auth SDK

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
  apiKey: "REPLACE_WITH_RESTRICTED_API_KEY",
  authDomain: "etype-emulator-app.firebaseapp.com",
  messagingSenderId: "14967080007",
};

const app = initializeApp(firebaseConfig);
export const auth = getAuth(app);

const googleProvider = new GoogleAuthProvider();

/**
 * Sign in with Google using popup.
 * @returns {Promise<import('https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js').User>}
 */
export async function loginWithGoogle() {
  try {
    const result = await signInWithPopup(auth, googleProvider);
    return result.user;
  } catch (error) {
    if (error.code !== 'auth/popup-closed-by-user') {
      console.error('E-Type Auth Error:', error);
    }
    throw error;
  }
}

/**
 * Sign out current user.
 * @returns {Promise<void>}
 */
export async function logout() {
  try {
    await signOut(auth);
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
  return onAuthStateChanged(auth, callback);
}
