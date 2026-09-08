# LifeGuard-Graduation-Project
An IoT-integrated wearable vest for continuous real-time remote patient monitoring.

# LifeGuard: Autonomous Smart Health Monitoring Vest

LifeGuard is a smart health monitoring vest designed to provide continuous, real-time remote patient monitoring and rapid emergency response. Developed between September 2025 and July 2026 as a graduation project at the Higher Institute for Engineering and Technology at Al-Obour, the system processes continuous biometric sensor streams to alert caregivers without manual intervention.

## System Architecture & Hardware Components

The LifeGuard vest integrates wearable electronics with edge computing to ensure continuous, non-invasive monitoring:
<img width="1000" height="1000" alt="20260228_222941" src="https://github.com/user-attachments/assets/1d8e9092-76f7-49b9-9b81-0dddd1cfb55e" />
*   **Processing Unit:** ESP32 Dev Module, acting as the central hub for real-time data acquisition, non-blocking sensor processing, and secure WiFi transmission.
*   **Sensor Array:**
    *   **MLX90614:** Infrared thermometer for non-contact core body temperature.
    *   **MPU6050:** 3-axis accelerometer and gyroscope for orientation and impact detection.
    *   **MAX30105:** Optical sensor for Heart Rate (BPM) and SpO2 analysis.
    *   **AD8232:** Single-lead monitor for capturing cardiac electrical bio-potentials (ECG).
*   **Power Supply:** 3.7V Lithium Polymer (Li-Po) battery for stable, portable mobility.
*   **Vest Design:** Breathable, ergonomic fabric securing internal electronics for long-term wear.

![LifeGuard hardware](https://github.com/user-attachments/assets/ab43cd9c-2e11-4bbb-8308-0ca4f4475a09)

## Principle of Operation

The ESP32 continuously polls sensor data via I2C and analog buses. To eliminate cloud latency, the ESP32 performs local filtering and mathematical computations at the edge. Processed variables are structured into lightweight JSON payloads and transmitted via secure HTTPS POST requests to a public n8n cloud server. n8n orchestrates the workflow by updating the web dashboard, logging baselines into Google Sheets, and routing metrics to Gemini AI.

## Core Features

*   **Real-Time Vitals Tracking:** Continuous telemetry of temperature, heart rate, and blood oxygen levels.
*   **ECG Acquisition:** Detailed cardiac bio-potential charts for remote rhythm review.
*   **Edge-Based Fall Detection:** Instantaneous recognition of physical impacts using the resultant acceleration vector:

    `NetAcc = sqrt(a.x^2 + a.y^2 + a.z^2)`

*   **AI-Powered Summaries:** Gemini AI evaluates physiological trends to generate human-readable clinical overviews.
*   **Automated Emergency Alerting:** Immediate, context-rich push notifications dispatched to caregivers via Telegram upon detecting anomalies or falls.

## Practical Applications

*   Elderly and remote home care monitoring.
*   Post-operative rehabilitation tracking.
*   Decentralized wireless telemetry for hospital wards.

## Cloud Automation & System Workflows (n8n Integration)

The backend architecture is containerized using Docker and orchestrated via Kubernetes for high availability, with Firebase providing real-time data synchronization of patient vitals and historical baselines. The cloud automation itself runs through n8n as two independent asynchronous workflows, to prevent data congestion:

**Workflow 1 — Data Ingestion, Archiving & Emergency Triage**
*   **Webhook Trigger:** Receives HTTP POST payloads from the ESP32 every two seconds with a non-blocking response.
*   **Data Processing:** A JavaScript Code Node sanitizes inputs and calculates the Net Acceleration Vector.
*   **Historical Archiving:** Automatically appends sanitized data into a Google Sheets Electronic Health Record (EHR).
*   **AI-Assisted Triage:** Uses Gemini AI to evaluate vital anomalies and generate urgent first-aid instructions.
*   **Telegram Alerts:** Pushes context-rich emergency directives instantly to caregivers via a Telegram Bot.

\`\`\`javascript
// Data sanitization and fall detection threshold logic
const payload = $input.item.json.body || $input.item.json;
const bodyTemp = parseFloat(payload.temp_obj) || 0;
const acc_x = parseFloat(payload.acc_x) || 0;
const acc_y = parseFloat(payload.acc_y) || 0;
const acc_z = parseFloat(payload.acc_z) || 0;
const heart_rate = parseInt(payload.heart_rate) || 0;
const spo2 = parseInt(payload.spo2) || 0;

const netAcc = Math.sqrt((acc_x * acc_x) + (acc_y * acc_y) + (acc_z * acc_z));
let motion_state = "Stable";
let is_emergency = false;

if (netAcc > 25.0 || heart_rate > 120 || (heart_rate < 40 && heart_rate > 0) || spo2 < 90) {
    motion_state = (netAcc > 25.0) ? "FALL DETECTED!" : "Critical Vitals";
    is_emergency = true;
} else if (netAcc > 16.0) {
    motion_state = "Abnormal Shaking";
} else if (netAcc >= 7.5 && netAcc <= 12.5) {
    motion_state = "Stable";
} else {
    motion_state = "Moving";
}

return {
    json: {
        timestamp: new Date().toISOString(),
        temperature: bodyTemp.toFixed(1),
        heart_rate: heart_rate,
        spo2: spo2,
        net_acceleration: parseFloat(netAcc.toFixed(2)),
        motion_state: motion_state,
        is_emergency: is_emergency
    }
}
\`\`\`

![n8n workflow](https://github.com/user-attachments/assets/45838025-5d33-408c-861e-9cfb11f75eba)

**Workflow 2 — Mobile Application REST API**
Acts as a customized REST API endpoint (`Webhook GET`) that queries Google Sheets, filters strictly for the latest telemetry record using an Item Lists node, and returns a lightweight single-object JSON payload to the mobile app.

## Mobile Application & Web Dashboard

The system features multi-platform interfaces for caregivers to monitor patient health status in real-time.

*   **Flutter Mobile Application:** A cross-platform mobile app serving as a synchronized portable monitoring interface, mirroring the ESP32 web interface layout, with instant push notifications and a detailed vitals dashboard.
    *   **Telemetry Parsing:** Ingests structured JSON data mapping heart rate, SpO2, temperature, tri-axial acceleration, and ECG bio-potentials. Fall states are dynamically derived if missing from payloads using acceleration threshold formulas.
    *   **Connectivity Modes:** Supports Demo/Simulation Mode (offline synthetic data), HTTP Polling (REST API integration via n8n), and WebSocket streaming modes.
    *   **EHR Integration:** Pulls historical data directly via public Google Sheet CSV export URLs (`.../spreadsheets/d/<ID>/export?format=csv&gid=<GID>`) without requiring complex OAuth flows.
    *   **Reliability Features:** Includes Watchdog Timer monitoring for link loss detection, a Clinical Alert Engine for vital thresholds, Full-Screen Fall Detection override alerts, and persistent local configuration via `shared_preferences`.
    *   **Mobile App Repository:** [GitHub Repository](https://github.com/mo0hamedh/grad-proj)
*   **Web Dashboard:** A lightweight web application designed for rapid edge rendering and desktop monitoring.
    *   **Live Web App:** [Web App Link](https://med-application-2-879550-67d7a.web.app/)
    *   **Preview:** [Preview Link](https://karim8833.github.io/SmartHealthMonitor/)

<img src="https://github.com/user-attachments/assets/41e46649-a3c8-4001-8240-7fd70289553f" width="300" alt="Mobile app dashboard" />
## Application Source Code

Below is the core implementation of the Flutter mobile application:

\`\`\`dart
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'screens/dashboard_screen.dart';
import 'services/sensor_service.dart';
import 'theme/app_theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setSystemUIOverlayStyle(SystemUiOverlayStyle.light);

  final service = SensorService();
  await service.loadConfig();
  service.connect();

  runApp(PatientMonitorApp(service: service));
}

class PatientMonitorApp extends StatelessWidget {
  const PatientMonitorApp({super.key, required this.service});

  final SensorService service;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Patient Monitor',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.dark,
      home: DashboardScreen(service: service),
    );
  }
}


}
\`\`\`
