const express = require("express");
const mqtt = require("mqtt");

const app = express();
const PORT = process.env.PORT || 3001;
const MQTT_URL = process.env.MQTT_URL || "mqtt://localhost:1883";

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

const mqttClient = mqtt.connect(MQTT_URL, { reconnectPeriod: 5000 });

mqttClient.on("connect", () => {
  console.log(`Connected to MQTT broker at ${MQTT_URL}`);
});

mqttClient.on("error", (err) => {
  console.error("MQTT connection error:", err.message);
});

app.post("/", async (req, res) => {
  if (!req.body) {
    return res.status(400).json({ error: "Request body is required" });
  }

  if (!req.body.topic || !req.body.message) {
    return res.status(400).json({ error: "Both 'topic' and 'message' fields are required" });
  }

  if (!mqttClient.connected) {
    return res.status(503).json({ error: "MQTT broker not connected" });
  }

  mqttClient.publish(req.body.topic, JSON.stringify(req.body.message), (err) => {
    if (err) {
      return res.status(500).json({ error: "Failed to publish message" });
    }
    res.json({ topic: req.body.topic, status: "published" });
  });
});

app.use((req, res, next) => {
  next(new ApiError(httpStatus.NOT_FOUND, 'Not found'));
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
