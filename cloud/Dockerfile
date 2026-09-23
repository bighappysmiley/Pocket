# Pocket Cloud API — production Node image
FROM node:20-bookworm-slim AS deps
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

FROM node:20-bookworm-slim AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY tsconfig.json ./
COPY src ./src
RUN npm run build

FROM node:20-bookworm-slim AS runner
WORKDIR /app
ENV NODE_ENV=production
ENV PORT=8787
ENV HOST=0.0.0.0
ENV DATABASE_PATH=/data/pocket-cloud.sqlite
RUN mkdir -p /data && chown node:node /data
COPY --from=deps /app/node_modules ./node_modules
COPY --from=build /app/dist ./dist
COPY package.json ./
USER node
EXPOSE 8787
VOLUME ["/data"]
CMD ["node", "dist/index.js"]
