const mqtt = require("mqtt");
const express = require("express");
const fs = require("fs").promises;
const { v4: uuidv4 } = require("uuid");

const app = express();
const PORT = process.env.PORT || 3000;
const MQTT_URL = process.env.MQTT_URL || "mqtt://localhost:1883";

const dataStore = "/app/data/";

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

app.use((req, res, next) => {
  res.header("Access-Control-Allow-Origin", "*");
  res.header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  res.header("Access-Control-Allow-Headers", "Content-Type");
  if (req.method === "OPTIONS") return res.sendStatus(204);
  next();
});

const mqttClient = mqtt.connect(MQTT_URL, { reconnectPeriod: 5000 });

mqttClient.on("connect", () => {
  console.log(`Connected to MQTT broker at ${MQTT_URL}`);
  mqttClient.subscribe("#", (err) => {
    if (err) {
      console.error("Failed to subscribe to all topics:", err.message);
    } else {
      console.log("Subscribed to all topics (#)");
    }
  });
});

mqttClient.on("error", (err) => {
  console.error("MQTT connection error:", err.message);
});

mqttClient.on("message", async (topic, message) => {
  const fileid = uuidv4();
  const filename = `${dataStore}${fileid}.txt`;
  const payload = { topic, message: message.toString() };
  try {
    await fs.writeFile(filename, JSON.stringify(payload, null, 2), "utf-8");
    console.log(`Stored message on topic "${topic}" as ${fileid}`);
  } catch (err) {
    console.error("Failed to store message:", err.message);
  }
});

app.get("/", async (req, res) => {
  const files = await fs.readdir(dataStore);
  const txtFiles = files.filter(f => f.endsWith(".txt"));
  const result = await Promise.all(txtFiles.map(async (f) => {
    const data = JSON.parse(await fs.readFile(`${dataStore}${f}`, "utf-8"));
    return { id: f.replace(".txt", ""), topic: data.topic, message: data.message };
  }));
  res.json(result);
});

app.post("/", async (req, res) => {
  if (!req.body) {
    return res.status(400).json({ error: "Request body is required" });
  }

  if (!req.body.topic || !req.body.message) {
    return res.status(400).json({ error: "Both 'topic' and 'message' fields are required" });
  }

  let fileid = uuidv4();
  const filename = `${dataStore}${fileid}.txt`;

  await fs.writeFile(filename, JSON.stringify(req.body, null, 2), "utf-8");

  res.json({ message: fileid });
});

app.get("/:fileid", async (req, res) => {
  const { fileid } = req.params;
  const filename = `${dataStore}${fileid}.txt`;

  try {
    const data = await fs.readFile(filename, "utf-8");
    res.json(JSON.parse(data));
  } catch (err) {
    if (err.code === "ENOENT") {
      return res.status(404).json({ error: "Message not found" });
    }
    res.status(500).json({ error: "Internal server error" });
  }
});

app.delete("/:fileid", async (req, res) => {
  const { fileid } = req.params;
  const filename = `${dataStore}${fileid}.txt`;

  try {
    await fs.unlink(filename);
    res.json({ message: "Message deleted successfully" });
  } catch (err) {
    if (err.code === "ENOENT") {
      return res.status(404).json({ error: "Message not found" });
    }
    res.status(500).json({ error: "Internal server error" });
  }
});

app.use((req, res, next) => {
  next(new ApiError(httpStatus.NOT_FOUND, 'Not found'));
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});