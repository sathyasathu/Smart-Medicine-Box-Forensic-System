SMART MEDICINE BOX - FORENSIC WEB DASHBOARD

Open index.html. For best results, serve the folder with VS Code Live Server.

1. Open index.html.
2. Expand Firebase project configuration.
3. Paste the Firebase Web API key for project smartmedicinebox-68dbc.
4. Use the Firebase Authentication email/password already used by the ESP32.
5. The dashboard reads:
   /MedicineBox/Status
   /MedicineBox/Logs

Features:
- Live Firebase status
- Medicine status
- Schedule owner inferred from logs
- Real-time forensic event stream
- Event severity classification
- Timeline
- SHA-256 hash-chain verification using the same record format as the uploaded ESP32 code
- JSON and CSV evidence export

No Firebase password is stored in the HTML.
The API key is intentionally entered in the browser; Firebase Web API keys are client configuration, not user passwords.
