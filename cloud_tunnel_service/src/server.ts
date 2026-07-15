import "dotenv/config";
import express from "express";
import http from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import { createPool } from "./db";
import { randomId, sha256Hex } from "./crypto";
import type { RowDataPacket } from "mysql2/promise";
import type {
  CloudDeliverToken,
  CloudHttpRequest,
  CloudToDeviceMessage,
  DeviceHello,
  DeviceHttpResponse,
  DeviceToCloudMessage,
} from "./protocol";

type DeviceSession = {
  ws: WebSocket;
  deviceId: string;
  sessionId: string;
  userId: number | null;
  authenticated: boolean;
  lastSeenAt: number;
};

type PendingRequest = {
  resolve: (resp: DeviceHttpResponse) => void;
  reject: (err: Error) => void;
  timeoutId: NodeJS.Timeout;
};

const PORT = Number(process.env.PORT || 8080);
const WS_PATH = process.env.WS_PATH || "/ws/device";
const SERVICE_API_KEY = process.env.SERVICE_API_KEY || "";

const db = createPool({
  // On some Ubuntu installs, "localhost" resolves to ::1 while MySQL listens only on 127.0.0.1.
  host: process.env.DB_HOST || "127.0.0.1",
  user: process.env.DB_USER || "root",
  password: process.env.DB_PASS || "",
  database: process.env.DB_NAME || "co111685_proxy",
  port: process.env.DB_PORT ? Number(process.env.DB_PORT) : 3306,
});

const sessions = new Map<string, DeviceSession>(); // deviceId -> session
const pending = new Map<string, PendingRequest>(); // requestId -> pending

// Simple rate limiter for cloud_proxy calls (per userId)
const rl = new Map<number, { count: number; resetAt: number }>();
function rateLimit(userId: number, limit: number, windowMs: number): boolean {
  const now = Date.now();
  const cur = rl.get(userId);
  if (!cur || now >= cur.resetAt) {
    rl.set(userId, { count: 1, resetAt: now + windowMs });
    return true;
  }
  if (cur.count >= limit) return false;
  cur.count += 1;
  return true;
}

function requireServiceKey(req: express.Request): boolean {
  if (!SERVICE_API_KEY) return false;
  const key = req.header("x-service-key") || "";
  return key === SERVICE_API_KEY;
}

function wsSend(ws: WebSocket, msg: CloudToDeviceMessage) {
  ws.send(JSON.stringify(msg));
}

async function dbUpsertDeviceUid(deviceId: string, fwVersion?: string) {
  // ensure row exists (unclaimed yet): user_id=0 не подходит из-за FK.
  // На этапе до claim мы не создаём esp32_devices, а используем claims/sessions.
  // Здесь только обновляем firmware/lastSeen если запись уже есть.
  await db.execute(
    "UPDATE esp32_devices SET firmware_version = COALESCE(?, firmware_version), last_seen_at = NOW(), tunnel_enabled = 1, tunnel_status = 'online' WHERE device_uid = ?",
    [fwVersion ?? null, deviceId]
  );
}

async function dbInsertSession(deviceId: string, sessionId: string, fwVersion?: string, ipInfo?: string) {
  await db.execute(
    "INSERT INTO esp32_device_sessions (device_uid, ws_session_id, connected_at, last_seen_at, fw_version, ip_info) VALUES (?, ?, NOW(), NOW(), ?, ?)",
    [deviceId, sessionId, fwVersion ?? null, ipInfo ?? null]
  );
}

async function dbTouchSession(deviceId: string, sessionId: string, ipInfo?: string) {
  await db.execute(
    "UPDATE esp32_device_sessions SET last_seen_at = NOW(), ip_info = COALESCE(?, ip_info) WHERE device_uid = ? AND ws_session_id = ?",
    [ipInfo ?? null, deviceId, sessionId]
  );
  await db.execute(
    "UPDATE esp32_devices SET last_seen_at = NOW(), tunnel_status = 'online' WHERE device_uid = ?",
    [deviceId]
  );
}

async function dbStoreClaim(deviceId: string, sessionId: string, salt: string, hash: string, expiresAtSec: number) {
  // Удалим старые/просроченные claims для устройства (чтобы не раздувать таблицу)
  await db.execute("DELETE FROM esp32_device_claims WHERE device_uid = ? AND expires_at < NOW()", [deviceId]);

  const expiresAt = new Date(expiresAtSec * 1000);
  await db.execute(
    "INSERT INTO esp32_device_claims (device_uid, claim_salt, claim_hash, issued_at, expires_at, ws_session_id) VALUES (?, ?, ?, NOW(), ?, ?)",
    [deviceId, salt, hash, expiresAt, sessionId]
  );
}

async function dbGetDeviceOwnership(deviceId: string) {
  const [rows] = await db.execute<RowDataPacket[]>(
    "SELECT id, user_id, device_token_hash, device_token_id, tunnel_enabled FROM esp32_devices WHERE device_uid = ? LIMIT 1",
    [deviceId]
  );
  if (!rows.length) return null;
  return {
    id: Number(rows[0].id),
    userId: Number(rows[0].user_id),
    tokenHash: rows[0].device_token_hash as string | null,
    tokenId: rows[0].device_token_id as string | null,
    tunnelEnabled: !!rows[0].tunnel_enabled,
  };
}

async function authenticateDeviceHello(deviceId: string, token?: string) {
  const row = await dbGetDeviceOwnership(deviceId);
  if (!row || !row.userId || !row.tokenHash || !token) {
    return { ok: false as const, userId: null as number | null };
  }
  const h = sha256Hex(token);
  if (h !== row.tokenHash) {
    return { ok: false as const, userId: null as number | null };
  }
  return { ok: true as const, userId: row.userId };
}

function isValidDeviceId(deviceId: unknown): deviceId is string {
  return typeof deviceId === "string" && deviceId.length >= 6 && deviceId.length <= 32;
}

const app = express();
app.use(express.json({ limit: "128kb" }));

app.get("/health", (_req, res) => {
  res.json({ ok: true, online: sessions.size });
});

// Cloud-proxy -> tunnel: execute HTTP request on device via WS
app.post("/api/tunnel/request", async (req, res) => {
  if (!requireServiceKey(req)) {
    res.status(401).json({ error: "unauthorized" });
    return;
  }

  const { userId, deviceId, method, path, headers, bodyBase64, timeoutMs } = req.body || {};
  if (!Number.isFinite(Number(userId)) || !isValidDeviceId(deviceId) || typeof method !== "string" || typeof path !== "string") {
    res.status(400).json({ error: "invalid_request" });
    return;
  }
  if (!path.startsWith("/api/")) {
    res.status(400).json({ error: "path_not_allowed" });
    return;
  }
  if (path.length > 256) {
    res.status(400).json({ error: "path_too_long" });
    return;
  }

  const m = method.toUpperCase();
  const allowedMethods = new Set(["GET", "POST", "PUT", "DELETE", "PATCH"]);
  if (!allowedMethods.has(m)) {
    res.status(400).json({ error: "method_not_allowed" });
    return;
  }

  // Rate limit
  if (!rateLimit(Number(userId), 120, 60_000)) {
    res.status(429).json({ error: "rate_limited" });
    return;
  }

  // Ownership check
  const row = await dbGetDeviceOwnership(deviceId);
  if (!row || row.userId !== Number(userId) || !row.tunnelEnabled) {
    res.status(403).json({ error: "forbidden" });
    return;
  }

  const session = sessions.get(deviceId);
  if (!session || !session.authenticated) {
    res.status(503).json({ error: "device_offline" });
    return;
  }

  const requestId = randomId(12);
  const msg: CloudHttpRequest = {
    type: "http_request",
    requestId,
    method: m,
    path,
    headers: headers && typeof headers === "object" ? headers : undefined,
    bodyBase64: typeof bodyBase64 === "string" ? bodyBase64 : undefined,
  };

  const ms = Number.isFinite(Number(timeoutMs)) ? Math.max(500, Math.min(20_000, Number(timeoutMs))) : 10_000;
  const p = new Promise<DeviceHttpResponse>((resolve, reject) => {
    const timeoutId = setTimeout(() => {
      pending.delete(requestId);
      reject(new Error("timeout"));
    }, ms);
    pending.set(requestId, { resolve, reject, timeoutId });
  });

  wsSend(session.ws, msg);

  try {
    const resp = await p;
    // audit log
    // eslint-disable-next-line no-console
    console.log(`[tunnel] user=${userId} device=${deviceId} ${m} ${path} -> ${resp.status}`);
    res.status(200).json(resp);
  } catch (e: any) {
    // eslint-disable-next-line no-console
    console.log(`[tunnel] user=${userId} device=${deviceId} ${m} ${path} -> timeout`);
    res.status(504).json({ error: e?.message || "timeout" });
  }
});

// Cloud-proxy -> tunnel: finalize claim and deliver device token
app.post("/api/tunnel/claim/commit", async (req, res) => {
  if (!requireServiceKey(req)) {
    res.status(401).json({ error: "unauthorized" });
    return;
  }

  const { userId, deviceId } = req.body || {};
  if (!Number.isFinite(Number(userId)) || !isValidDeviceId(deviceId)) {
    res.status(400).json({ error: "invalid_request" });
    return;
  }

  const session = sessions.get(deviceId);
  if (!session) {
    res.status(503).json({ error: "device_offline" });
    return;
  }

  // Generate device token
  const token = randomId(32);
  const tokenId = randomId(12);
  const tokenHash = sha256Hex(token);

  // Store token hash and mark claimed/tunnel enabled
  await db.execute(
    "UPDATE esp32_devices SET tunnel_enabled = 1, claimed_at = COALESCE(claimed_at, NOW()), device_token_hash = ?, device_token_id = ?, tunnel_status = 'online' WHERE device_uid = ? AND user_id = ?",
    [tokenHash, tokenId, deviceId, Number(userId)]
  );

  const deliver: CloudDeliverToken = { type: "deliver_token", token, tokenId };
  wsSend(session.ws, deliver);

  res.json({ success: true, tokenId });
});

// Optional: list online devices for a user
app.get("/api/tunnel/devices/online", async (req, res) => {
  if (!requireServiceKey(req)) {
    res.status(401).json({ error: "unauthorized" });
    return;
  }
  const userId = Number(req.query.userId);
  if (!Number.isFinite(userId)) {
    res.status(400).json({ error: "invalid_request" });
    return;
  }

  const [rows] = await db.execute<RowDataPacket[]>(
    "SELECT device_uid, last_seen_at, tunnel_status FROM esp32_devices WHERE user_id = ? AND tunnel_enabled = 1 ORDER BY last_seen_at DESC",
    [userId]
  );

  res.json({
    devices: rows.map((r) => ({
      deviceId: r.device_uid,
      lastSeenAt: r.last_seen_at,
      status: r.tunnel_status,
      online: sessions.has(String(r.device_uid)),
    })),
  });
});

const server = http.createServer(app);

const wss = new WebSocketServer({ server, path: WS_PATH });

wss.on("connection", (ws) => {
  const sessionId = randomId(12);
  let deviceId: string | null = null;
  let authed = false;
  let userId: number | null = null;

  const helloTimer = setTimeout(() => {
    if (!deviceId) {
      ws.close(1008, "hello timeout");
    }
  }, 5_000);

  ws.on("message", async (buf) => {
    let msg: DeviceToCloudMessage;
    try {
      msg = JSON.parse(buf.toString("utf8"));
    } catch {
      return;
    }

    if (msg.type === "hello") {
      const hello = msg as DeviceHello;
      if (!isValidDeviceId(hello.deviceId)) {
        ws.close(1008, "invalid deviceId");
        return;
      }
      deviceId = hello.deviceId;
      clearTimeout(helloTimer);

      // create/replace session
      const now = Date.now();
      const existing = sessions.get(deviceId);
      if (existing && existing.ws !== ws) {
        try {
          existing.ws.close(1012, "replaced");
        } catch {}
      }

      // Authentication
      const auth = await authenticateDeviceHello(deviceId, hello.token);
      authed = auth.ok;
      userId = auth.userId;

      sessions.set(deviceId, {
        ws,
        deviceId,
        sessionId,
        userId,
        authenticated: authed,
        lastSeenAt: now,
      });

      try {
        await dbInsertSession(deviceId, sessionId, hello.fwVersion, hello.ipInfo);
      } catch (e) {
        // ignore DB errors for MVP
        // eslint-disable-next-line no-console
        console.error("dbInsertSession error", e);
      }

      try {
        await dbUpsertDeviceUid(deviceId, hello.fwVersion);
      } catch (e) {
        // ignore
      }

      // Store claim if provided
      if (hello.claimSalt && hello.claimHash && typeof hello.claimExpiresAt === "number") {
        try {
          // Compatibility: если device не знает epoch-time, может прислать TTL в секундах
          const nowSec = Math.floor(Date.now() / 1000);
          const expiresAt =
            hello.claimExpiresAt < 1_600_000_000 ? nowSec + hello.claimExpiresAt : hello.claimExpiresAt;
          await dbStoreClaim(deviceId, sessionId, hello.claimSalt, hello.claimHash, expiresAt);
        } catch (e) {
          // eslint-disable-next-line no-console
          console.error("dbStoreClaim error", e);
        }
      }

      if (authed) {
        wsSend(ws, { type: "auth_ok", userId: userId ?? undefined });
      } else {
        wsSend(ws, { type: "auth_required", reason: "no_or_invalid_token" });
      }
      return;
    }

    if (!deviceId) {
      return;
    }

    // Heartbeat
    if (msg.type === "heartbeat") {
      const s = sessions.get(deviceId);
      if (s) s.lastSeenAt = Date.now();
      try {
        await dbTouchSession(deviceId, sessionId, (msg as any).ipInfo);
      } catch {}
      return;
    }

    // Response for pending request
    if (msg.type === "http_response") {
      const resp = msg as DeviceHttpResponse;
      const p = pending.get(resp.requestId);
      if (p) {
        clearTimeout(p.timeoutId);
        pending.delete(resp.requestId);
        p.resolve(resp);
      }
      return;
    }
  });

  ws.on("close", async () => {
    clearTimeout(helloTimer);
    if (deviceId) {
      const s = sessions.get(deviceId);
      if (s && s.ws === ws) {
        sessions.delete(deviceId);
        try {
          await db.execute("UPDATE esp32_devices SET tunnel_status = 'offline' WHERE device_uid = ?", [deviceId]);
        } catch {}
      }
    }
  });
});

server.listen(PORT, () => {
  // eslint-disable-next-line no-console
  console.log(`cloud_tunnel_service listening on :${PORT} (ws path ${WS_PATH})`);
});

