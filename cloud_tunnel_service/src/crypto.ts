import crypto from "node:crypto";

export function randomId(bytes = 16): string {
  return crypto.randomBytes(bytes).toString("hex");
}

export function sha256Hex(input: string): string {
  return crypto.createHash("sha256").update(input, "utf8").digest("hex");
}

