const express = require("express");
const coap = require("coap");

const app = express();
const PORT = process.env.PORT || 3003;

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

app.post("/", async (req, res) => {
  if (!req.body) {
    return res.status(400).json({ error: "Request body is required" });
  }

  if (!req.body.ip || !req.body.command) {
    return res.status(400).json({ error: "Both 'ip' and 'command' fields are required" });
  }

  const coapReq = coap.request({
    hostname: req.body.ip,
    pathname: `/${req.body.command.toLowerCase()}`,
    method: "POST",
  });

  coapReq.end();

  let responded = false;

  const timeout = setTimeout(() => {
    responded = true;
    res.status(504).json({ error: "CoAP request timed out" });
  }, 3000);

  coapReq.on("response", (coapRes) => {
    if (responded) return;
    clearTimeout(timeout);
    res.json({ ip: req.body.ip, status: "sent" });
  });

  coapReq.on("error", (err) => {
    if (responded) return;
    clearTimeout(timeout);
    res.status(500).json({ error: err.message });
  });
});

app.use((req, res, next) => {
  res.status(404).json({ error: "Not found" });
});

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
