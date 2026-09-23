import { mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, resolve, join } from "node:path";
import { createRequire } from "node:module";
import initSqlJs, { type Database as SqlJsDatabase, type SqlJsStatic } from "sql.js";
import { SCHEMA_SQL } from "./schema.js";
import { config } from "../config.js";

export type Db = SqlJsDatabase;

let db: Db | null = null;
let dbPath = "";
let persistTimer: ReturnType<typeof setTimeout> | null = null;

const require = createRequire(import.meta.url);

async function loadSqlJs(): Promise<SqlJsStatic> {
  const entry = require.resolve("sql.js");
  const wasmPath = join(dirname(entry), "sql-wasm.wasm");
  return initSqlJs({ locateFile: () => wasmPath });
}

export async function openDatabase(path = config.databasePath): Promise<Db> {
  if (db) return db;
  dbPath = resolve(path);
  mkdirSync(dirname(dbPath), { recursive: true });

  const SQL = await loadSqlJs();
  if (existsSync(dbPath)) {
    const fileBuffer = readFileSync(dbPath);
    db = new SQL.Database(fileBuffer);
  } else {
    db = new SQL.Database();
  }
  db.run("PRAGMA foreign_keys = ON;");
  db.exec(SCHEMA_SQL);
  persistNow();
  return db;
}

export function getDb(): Db {
  if (!db) throw new Error("Database not opened");
  return db;
}

export function persistNow(): void {
  if (!db || !dbPath) return;
  const data = db.export();
  writeFileSync(dbPath, Buffer.from(data));
}

/** Debounced persist after writes. */
export function schedulePersist(): void {
  if (persistTimer) clearTimeout(persistTimer);
  persistTimer = setTimeout(() => {
    persistTimer = null;
    persistNow();
  }, 50);
}

export function closeDatabase(): void {
  if (persistTimer) {
    clearTimeout(persistTimer);
    persistTimer = null;
  }
  if (db) {
    persistNow();
    db.close();
    db = null;
  }
}

type SqlValue = string | number | null | Uint8Array;

export function run(sql: string, params: SqlValue[] = []): void {
  const database = getDb();
  database.run(sql, params);
  schedulePersist();
}

export function get<T extends Record<string, unknown>>(
  sql: string,
  params: SqlValue[] = [],
): T | undefined {
  const database = getDb();
  const stmt = database.prepare(sql);
  stmt.bind(params);
  if (!stmt.step()) {
    stmt.free();
    return undefined;
  }
  const row = stmt.getAsObject() as T;
  stmt.free();
  return row;
}

export function all<T extends Record<string, unknown>>(
  sql: string,
  params: SqlValue[] = [],
): T[] {
  const database = getDb();
  const stmt = database.prepare(sql);
  stmt.bind(params);
  const rows: T[] = [];
  while (stmt.step()) {
    rows.push(stmt.getAsObject() as T);
  }
  stmt.free();
  return rows;
}

/** Reset DB for tests (in-memory). */
export async function openMemoryDatabase(): Promise<Db> {
  if (db) {
    closeDatabase();
  }
  const SQL = await loadSqlJs();
  db = new SQL.Database();
  dbPath = "";
  db.run("PRAGMA foreign_keys = ON;");
  db.exec(SCHEMA_SQL);
  return db;
}
