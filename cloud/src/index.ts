import { serve } from "@hono/node-server";
import { config } from "./config.js";
import { openDatabase, closeDatabase } from "./db/index.js";
import { createApp } from "./app.js";
import { PRODUCT_NAME, isMockBilling } from "./stripe/billing.js";

async function main() {
  await openDatabase();
  const app = createApp();

  console.log(
    `${PRODUCT_NAME} API listening on http://${config.host}:${config.port} (billing=${isMockBilling() ? "mock" : "stripe"})`,
  );

  serve({
    fetch: app.fetch,
    port: config.port,
    hostname: config.host,
  });

  const shutdown = () => {
    closeDatabase();
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
