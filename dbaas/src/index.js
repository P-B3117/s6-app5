const express = require("express");
const fs = require("fs").promises;
const { v4: uuidv4 } = require("uuid");

const app = express();
const PORT = process.env.PORT || 3000;

const dataStore = "/app/data/";

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

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
  let fileid = uuidv4();
  const filename = `${dataStore}${fileid}.txt`;

  if (!req.body) {
    return res.status(400).json({ error: "Request body is required" });
  }

  if (!req.body.topic || !req.body.message) {
    return res.status(400).json({ error: "Both 'topic' and 'message' fields are required" });
  }

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