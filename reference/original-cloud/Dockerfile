FROM node:22-alpine

WORKDIR /app
COPY app/package.json ./package.json
RUN npm install --omit=dev
COPY app/server.js ./server.js
COPY app/schema.sql ./schema.sql
COPY app/seed.js ./seed.js
COPY app/public ./public

ENV NODE_ENV=production
EXPOSE 8310
CMD ["node", "server.js"]
