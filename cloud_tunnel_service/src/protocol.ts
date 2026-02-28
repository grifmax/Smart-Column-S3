export type DeviceHello = {
  type: "hello";
  deviceId: string; // 12 hex (или другое), хранится как device_uid
  fwVersion?: string;
  token?: string;
  // Claim: device генерирует PIN и присылает salt+hash+expiresAt
  claimSalt?: string;
  claimHash?: string;
  claimExpiresAt?: number; // unix seconds
  ipInfo?: string;
};

export type DeviceHeartbeat = {
  type: "heartbeat";
  deviceId: string;
  uptime?: number;
  rssi?: number;
  ipInfo?: string;
};

export type CloudAuthRequired = {
  type: "auth_required";
  reason?: string;
};

export type CloudAuthOk = {
  type: "auth_ok";
  userId?: number;
};

export type CloudDeliverToken = {
  type: "deliver_token";
  token: string;
  tokenId: string;
};

export type CloudHttpRequest = {
  type: "http_request";
  requestId: string;
  method: string;
  path: string;
  headers?: Record<string, string>;
  bodyBase64?: string;
};

export type DeviceHttpResponse = {
  type: "http_response";
  requestId: string;
  status: number;
  headers?: Record<string, string>;
  bodyBase64?: string;
  error?: string;
};

export type DeviceToCloudMessage = DeviceHello | DeviceHeartbeat | DeviceHttpResponse;
export type CloudToDeviceMessage = CloudAuthRequired | CloudAuthOk | CloudDeliverToken | CloudHttpRequest;

