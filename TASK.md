# **Vox Project Description**

Vox is a secure, privacy-first messaging platform where people can chat without worrying about servers snooping on them. 
All conversations (1:1, groups, channels) are **end-to-end encrypted**, meaning only the participants can read messages — not the server, not admins. 
Servers are self-hosted, so you can run your own instance and control your data. 
There’s no email/phone verification — just a username and password to sign up. 
Messaging stays within each server’s network (no cross-server federation).

## **Functional Requirements**

These are the main principles of Vox, which reason other requirements.

### **Security & Core Architecture**

1. **End-to-End Encryption (E2EE)**

   * All messages (text/media) must be encrypted client-side before sending.
   * Servers never see plaintext.
   * Keys are generated and stored only on client devices.

2. **Zero Server-Side Key Storage**

   * Servers cannot store or reconstruct private encryption keys.
   * Key backups (if any) are encrypted with a user password and stored only on clients or optional user-controlled storage.

3. **Self-Hosted Server Support**

   * Anyone can deploy and run their own Vox server.
   * Deployment scripts and docs for popular environments (Docker/VM/etc).
   * Configurable server settings (max group size, storage limits, logging policies).

4. **Local-Only Messaging Scope**

   * Messaging is available only between users on the same server instance.
   * No federation between servers.

### **Messaging**

1. **Direct Messages**

   * One-to-one chat with E2EE.
   * Message status: sent, delivered, read (locally stored).

2. **Group Chats**

   * Create groups with multiple members.
   * Admin controls: add/remove participants.
   * Group messages encrypted end to end.

3. **Broadcast Channels**

   * One-to-many channels (admins post, members read).
   * No replies.
   * All content encrypted for members only.
   * Member list is not stored.

4. **Media & File Sharing**

   * Send images, videos, files.
   * All attachments encrypted end to end.

---

## **Client-Side Functional Requirements**

What the app itself must do.

### **Account & Identity**

1. **Username & Password Registration**

   * No email/phone required.
   * Username uniqueness per server.
   * Password hashed locally before sending to server.

2. **Login & Session Management**

   * Secure login flow (tokens, refresh tokens).
   * Persistent local session storage.

### **Key Management & Encryption Workflows**

1. **Key Generation**

   * Generate user encryption key pairs on device.
   * Secure storage of private keys on device only.

2. **Key Exchange**

   * Client handles encrypted key exchange for chat sessions.
   * Perfect Forward Secrecy.

3. **Key Rotation**

   * Support key regeneration with forward secrecy logic.

### **User Experience & UI**

1. **Search**

   * Local client search in messages & contacts.

2. **Notifications**

   * Push notifications with encrypted content previews (optional).

3. **Settings**

   * Privacy options (block users, etc).
   * Security options (password change, etc).

---

## **Server-Side Functional Requirements**

What the server must do and enforce.

### **Basic Server Functions**

1. **Authentication**

   * Store username + securely hashed password.
   * Issue session tokens to clients.
   * No email/phone verification.

2. **Message Relay & Persistence (Encrypted)**

   * Store **only encrypted messages**.
   * Deliver messages to clients when they connect.
   * Message expiry settings (optional retention).

3. **Group Management**

   * Maintain group member lists.
   * Ensure only group participants receive messages.

4. **Channel Management**

   * Track channel members/subscriptions.
   * Serve encrypted channel posts.

### **Server APIs**

1. **User Endpoints**

   * Register, Login, Logout.

2. **Chat Endpoints**

   * Send/Receive encrypted messages.
   * Sync chat history.
   * Media upload/download (encrypted at rest).

3. **Group/Channel Endpoints**

   * Create/Delete groups/channels.
   * Add/Remove members.
   * Fetch group metadata.

### **Administration & Logging**

1. **Server Admin Dashboard**

   * View server status and user counts.
   * Delete user accounts.
   * Shown at server terminal.
   * Optional logs (no decrypted content).

2. **Logs**

   * Access logs, error logs.
   * No message content logging.

