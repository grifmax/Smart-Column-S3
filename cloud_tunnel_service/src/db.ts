import mysql from "mysql2/promise";

export type DbConfig = {
  host: string;
  user: string;
  password: string;
  database: string;
  port?: number;
};

export function createPool(cfg: DbConfig) {
  return mysql.createPool({
    host: cfg.host,
    user: cfg.user,
    password: cfg.password,
    database: cfg.database,
    port: cfg.port ?? 3306,
    waitForConnections: true,
    connectionLimit: 10,
    maxIdle: 5,
    idleTimeout: 60_000,
    enableKeepAlive: true,
    keepAliveInitialDelay: 0,
  });
}

