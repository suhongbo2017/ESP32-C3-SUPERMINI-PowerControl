import Fastify from "fastify";
import crypto from "node:crypto";
import { hashPassword, validUsername, verifyPassword } from "./auth.js";
import type { DashboardUser } from "./auth.js";
import { CommandError, CommandService } from "./commands/service.js";
import type { FastifyInstance } from "fastify";
import { PairingError, PairingService } from "./pairing/service.js";
import { validateDeviceSnapshot } from "./protocol/messages.js";
import type { PairingRepository } from "./pairing/repository.js";

const dashboardHtml = `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PC Power Cloud</title><style>
*{box-sizing:border-box}body{margin:0;background:#111416;color:#e8eeee;font:14px/1.5 ui-sans-serif,system-ui,"Microsoft YaHei",sans-serif}header{position:fixed;inset:0 auto 0 0;width:236px;background:#191d1f;border-right:1px solid #2b3234;color:#f4f7f7;padding:28px 18px;display:flex;flex-direction:column;z-index:2}h1{font-size:18px;letter-spacing:.2px;margin:0 0 42px;padding:0 12px}header:after{content:"远程设备控制";color:#7f8d8f;font-size:12px;padding:12px;border-top:1px solid #2b3234}main{margin-left:236px;max-width:1240px;padding:34px 40px 60px}button,input{font:inherit}button{border:0;border-radius:6px;padding:9px 14px;background:#16a085;color:#fff;font-weight:700;cursor:pointer;transition:background .15s,transform .15s}button:hover{background:#1bb596}button:active{transform:translateY(1px)}button:disabled{opacity:.5;cursor:wait}.secondary{background:#293234;color:#c9d3d3}.secondary:hover{background:#354144}.danger{background:#c95748}.danger:hover{background:#df6252}.bar{display:flex;justify-content:space-between;align-items:center;margin:0 0 20px}.bar strong{font-size:24px;font-weight:650;letter-spacing:-.4px}.table-wrap{overflow:auto;background:#1a1f21;border:1px solid #2b3436;border-radius:9px;box-shadow:0 12px 30px #0003}table{width:100%;border-collapse:collapse;min-width:760px}th,td{text-align:left;padding:15px 16px;border-bottom:1px solid #2b3436}th{font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:#819093;background:#202729}tr:last-child td{border-bottom:0}tbody tr:hover{background:#202729}.diag{display:grid;grid-template-columns:repeat(2,minmax(130px,1fr));gap:6px 22px;font-size:12px;color:#91a0a1}.diag b{color:#e6eeee;font-weight:600}.online{color:#36d6a0;font-weight:750}.offline{color:#e68b7f;font-weight:700}.muted{color:#829093}.section{margin-top:34px}h2{font-size:16px;font-weight:650;margin:0 0 14px}.notice{min-height:24px;color:#e58c7e;margin:12px 0}.login{max-width:390px;margin:12vh auto;padding:30px;background:#1a1f21;border:1px solid #30393b;border-radius:10px;box-shadow:0 20px 50px #0005}.login h2{font-size:21px;margin-bottom:24px}label{display:block;margin:14px 0 7px;color:#aab7b8}input{width:100%;padding:11px 12px;color:#eaf2f2;background:#111416;border:1px solid #3a4547;border-radius:6px;outline:none}input:focus{border-color:#16a085;box-shadow:0 0 0 3px #16a08522}#app[hidden],#login[hidden]{display:none}@media(max-width:720px){header{position:static;width:auto;height:auto;padding:18px 20px;display:flex;flex-direction:row;align-items:center;border-right:0;border-bottom:1px solid #2b3234}h1{margin:0;padding:0;font-size:17px}header:after{display:none}header button{margin:0 0 0 auto}main{margin:0;padding:24px 16px 40px}.bar{align-items:flex-start;gap:16px}.bar strong{font-size:21px}.table-wrap{border-radius:8px}table{min-width:0}thead{display:none}tbody,tr,td{display:block;width:100%}tbody tr{padding:14px 16px;border-bottom:1px solid #2b3436}tbody tr:last-child{border-bottom:0}td{padding:4px 0;border:0}td:first-child{padding-bottom:8px;font-size:15px}td:nth-child(2){position:absolute;margin-top:4px;margin-left:92px;width:auto}td:nth-child(3),td:nth-child(5){color:#829093;font-size:12px}td:nth-child(4){margin:10px 0}.diag{grid-template-columns:repeat(2,minmax(0,1fr));gap:5px 10px}.danger{width:100%;margin-top:8px}.section{margin-top:28px}.section .table-wrap td:nth-child(2){position:static;margin:0;width:auto;word-break:break-all}.section .table-wrap td{font-size:12px}}

/* Cloudflare-inspired console theme */
:root{color-scheme:light}body{background:#f5f6f7;color:#1d2733;font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI","Microsoft YaHei",sans-serif}header{width:224px;background:#fff;color:#1d2733;border-right:1px solid #e5e7eb;padding:24px 16px;box-shadow:2px 0 8px #1d27330a}h1{font-size:19px;font-weight:700;color:#f48120;padding:0 12px;margin:0 0 34px}header:after{content:"云端设备控制";color:#7b8794;border-top:1px solid #e5e7eb;padding:14px 12px}.nav-label{display:block;margin:0 8px;padding:10px 12px;background:#fff4eb;border-left:3px solid #f48120;color:#b95608;font-weight:650;border-radius:3px}main{max-width:none;padding:32px 42px 64px}.bar{margin-bottom:24px}.bar strong{color:#1d2733;font-size:25px;font-weight:650}.table-wrap{background:#fff;border:1px solid #e5e7eb;border-radius:4px;box-shadow:0 1px 2px #1d27330a}table{min-width:820px}th{color:#596777;background:#f8f9fa;border-bottom:1px solid #dfe3e8;font-size:11px;letter-spacing:.06em}th,td{padding:16px;border-bottom-color:#edf0f2}tbody tr:hover{background:#fffaf6}.diag{color:#667483}.diag b{color:#263442}.online{color:#16825d}.offline{color:#c23b30}.muted{color:#6b7785}.secondary{background:#fff;color:#344454;border:1px solid #cbd2d9}.secondary:hover{background:#f5f7f8}.danger{background:#f48120}.danger:hover{background:#d9690c}button{border-radius:3px;background:#f48120}button:hover{background:#d9690c}.notice{color:#c23b30}.login{background:#fff;border:1px solid #e1e5e9;border-radius:4px;box-shadow:0 8px 24px #1d273314}.login h2{color:#1d2733}label{color:#536273}input{color:#1d2733;background:#fff;border:1px solid #b9c2cc;border-radius:3px}input:focus{border-color:#f48120;box-shadow:0 0 0 3px #f4812022}@media(max-width:720px){header{background:#fff;border-bottom-color:#e5e7eb}main{background:#f5f6f7;padding:24px 16px 40px}.table-wrap{box-shadow:0 1px 2px #1d27330a}tbody tr{border-bottom-color:#edf0f2}td:nth-child(3),td:nth-child(5){color:#6b7785}}
</style></head><body><header><h1>PC Power Cloud</h1><div class="nav-label">控制台</div><button id="logout" class="secondary" hidden>退出登录</button></header><main><section id="login" class="login"><h2>管理员登录</h2><form id="loginForm"><label for="username">用户名</label><input id="username" autocomplete="username"><label for="password">密码</label><input id="password" type="password" autocomplete="current-password"><label for="token">应急管理令牌</label><input id="token" type="password" autocomplete="off"><button style="margin-top:14px">登录</button></form><p class="muted">用户名密码登录无需每次复制服务器令牌。</p><p id="loginError" class="notice"></p></section><section id="app" hidden><div class="bar"><div><strong>设备概览</strong><div class="muted" id="updated">—</div></div><button id="refresh" class="secondary" title="刷新设备和命令">刷新数据</button></div><div class="table-wrap"><table><thead><tr><th>设备 ID</th><th>状态</th><th>最后在线</th><th>设备诊断</th><th>配对时间</th><th>操作</th></tr></thead><tbody id="devices"></tbody></table></div><section class="section"><h2>用户管理</h2><div class="table-wrap"><form id="createUserForm" style="padding:16px;display:flex;gap:8px;flex-wrap:wrap"><input id="newUser" placeholder="用户名" autocomplete="off" style="max-width:180px"><input id="newPassword" placeholder="初始密码（至少 12 位）" type="password" autocomplete="new-password" style="max-width:260px"><button id="createUser" type="submit">创建用户</button></form><table><thead><tr><th>用户名</th><th>状态</th><th>创建时间</th><th>操作</th></tr></thead><tbody id="users"></tbody></table></div></section><section class="section"><h2>最近命令</h2><div class="table-wrap"><table><thead><tr><th>设备</th><th>命令 ID</th><th>状态</th><th>创建时间</th><th>完成时间</th></tr></thead><tbody id="commands"></tbody></table></div></section><p id="notice" class="notice"></p></section></main><script>
const $=id=>document.getElementById(id);let csrf="";function esc(s){return String(s??"").replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}async function api(url,options={}){const headers={...(options.headers||{}),"x-csrf-token":csrf};const r=await fetch(url,{...options,headers,credentials:"same-origin"});if(r.status===401){csrf="";$('app').hidden=true;$('login').hidden=false;$('logout').hidden=true;throw Error("登录已过期，请重新登录")}const text=await r.text();if(!r.ok)throw Error(text||('HTTP '+r.status));return text?JSON.parse(text):null}
$('loginForm').onsubmit=async e=>{e.preventDefault();$('loginError').textContent="";try{const r=await fetch('/api/dashboard/login',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({token:$('token').value,username:$('username').value,password:$('password').value}),credentials:'same-origin'});if(!r.ok)throw Error('用户名或密码无效');csrf=(await r.json()).csrfToken;$('token').value="";$('password').value="";$('login').hidden=true;$('app').hidden=false;$('logout').hidden=false;await refresh()}catch(err){$('loginError').textContent=err.message}};
async function refresh(){try{const [devices,commands,users]=await Promise.all([api('/api/dashboard/devices'),api('/api/dashboard/commands'),api('/api/dashboard/users')]);$('devices').innerHTML=devices.length?devices.map(d=>'<tr><td><strong>'+esc(d.deviceId)+'</strong>'+(d.revokedAt?' <span class="muted">已撤销</span>':'')+'</td><td class="'+(d.online?'online':'offline')+'">'+(d.online?'在线':'离线')+'</td><td>'+(d.lastSeenAt?new Date(d.lastSeenAt).toLocaleString():'从未上线')+'</td><td>'+(d.diagnostics?'<div class="diag"><span>设备 IP <b>'+esc(d.diagnostics.deviceIp)+'</b></span><span>主机 IP <b>'+esc(d.diagnostics.hostIp||'未设置')+'</b></span><span>Wi-Fi <b>'+esc(d.diagnostics.wifi==='connected'?'已连接':'未连接')+'</b></span><span>SSID <b>'+esc(d.diagnostics.ssid)+'</b></span><span>温度 <b>'+(d.diagnostics.sensorAvailable?esc(d.diagnostics.temperature)+' °C':'不可用')+'</b></span><span>湿度 <b>'+(d.diagnostics.sensorAvailable?esc(d.diagnostics.humidity)+' %':'不可用')+'</b></span><span>LED <b>'+({high:'灯灭',low:'灯亮',blinking:'灯闪烁'}[d.diagnostics.powerLedState]||'未知')+'</b></span><span>Ping <b>'+({reachable:'通',unreachable:'不通',unknown:'未知'}[d.diagnostics.reachability]||'未知')+'</b></span></div>':'<span class="muted">等待设备上报</span>')+'</td><td>'+new Date(d.pairedAt).toLocaleString()+'</td><td>'+(d.online&&!d.revokedAt?'<button class="danger" data-device="'+esc(d.deviceId)+'">模拟按下电源键</button>':'—')+'</td></tr>').join(''):'<tr><td colspan="5" class="muted">暂无已配对设备</td></tr>';$('users').innerHTML=users.length?users.map(u=>'<tr><td><strong>'+esc(u.username)+'</strong></td><td class="'+(u.enabled?'online':'offline')+'">'+(u.enabled?'启用':'已禁用')+'</td><td>'+new Date(u.createdAt).toLocaleString()+'</td><td><button class="secondary" data-user="'+esc(u.username)+'" data-action="password">重置密码</button> <button class="danger" data-user="'+esc(u.username)+'" data-action="delete">删除</button></td></tr>').join(''):'<tr><td colspan="4" class="muted">暂无用户</td></tr>';$('commands').innerHTML=commands.length?commands.map(c=>'<tr><td>'+esc(c.deviceId)+'</td><td><code>'+esc(c.commandId)+'</code></td><td>'+esc(c.status)+'</td><td>'+new Date(c.issuedAt).toLocaleString()+'</td><td>'+(c.completedAt?new Date(c.completedAt).toLocaleString():'—')+'</td></tr>').join(''):'<tr><td colspan="5" class="muted">暂无命令</td></tr>';$('updated').textContent='更新于 '+new Date().toLocaleTimeString();$('notice').textContent=''}catch(err){$('notice').textContent=err.message}}
$('createUserForm').onsubmit=async e=>{e.preventDefault();const username=$('newUser').value.trim(),password=$('newPassword').value;if(!/^[a-zA-Z0-9._-]{3,40}$/.test(username)){ $('notice').textContent='用户名需为 3-40 个字母、数字、点、下划线或短横线';return }if(password.length<12||password.length>256){$('notice').textContent='密码长度必须为 12-256 位';return}const button=$('createUser');button.disabled=true;$('notice').textContent='正在创建用户…';try{await api('/api/dashboard/users',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({username,password})});$('newUser').value='';$('newPassword').value='';$('notice').textContent='用户已创建';await refresh()}catch(err){$('notice').textContent='创建失败：'+err.message}finally{button.disabled=false}};
$('users').onclick=async e=>{const b=e.target.closest('button[data-user]');if(!b)return;const username=b.dataset.user;if(b.dataset.action==='delete'){if(!confirm('确认删除用户 '+username+'？'))return;try{await api('/api/dashboard/users/'+encodeURIComponent(username),{method:'DELETE'});await refresh()}catch(err){$('notice').textContent=err.message}}else{const password=prompt('输入 '+username+' 的新密码（至少 12 位）：');if(password===null)return;try{await api('/api/dashboard/users/'+encodeURIComponent(username),{method:'PATCH',headers:{'content-type':'application/json'},body:JSON.stringify({password})});$('notice').textContent='密码已更新';await refresh()}catch(err){$('notice').textContent=err.message}}};
$('devices').onclick=async e=>{const b=e.target.closest('button[data-device]');if(!b)return;const id=b.dataset.device;if(!confirm('确认向设备 '+id+' 发送一次电源键短按？这会真实触发电脑电源键。'))return;b.disabled=true;try{const result=await api('/api/dashboard/devices/'+encodeURIComponent(id)+'/power-press',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({idempotencyKey:crypto.randomUUID()})});$('notice').textContent='命令已创建，状态：'+result.status+'。命令 15 秒后过期，不会自动重试。';await refresh()}catch(err){$('notice').textContent=err.message}finally{b.disabled=false}};
$('refresh').onclick=refresh;$('logout').onclick=async()=>{try{await api('/api/dashboard/logout',{method:'POST'})}catch{}csrf="";$('app').hidden=true;$('login').hidden=false;$('logout').hidden=true};</script></body></html>`;

export function buildServer(pairing: PairingService, repository: PairingRepository, adminToken: string): FastifyInstance {
  if (adminToken.length < 32) throw new Error("PC_POWER_ADMIN_TOKEN must contain at least 32 characters");
  const app = Fastify({ logger: { redact: ["req.headers.authorization", "req.body.pairingCode"] } });
  const commands = new CommandService(repository);
  const userStore = repository as PairingRepository & {
    findUser: (username: string) => DashboardUser | null;
    listUsers: () => Array<Omit<DashboardUser, "passwordHash">>;
    upsertUser: (user: DashboardUser) => void;
    setUserEnabled: (username: string, enabled: boolean, updatedAt: string) => boolean;
    deleteUser: (username: string) => boolean;
  };
  const sessions = new Map<string, number>();
  const sessionUsers = new Map<string, string>();
  const sessionLifetimeMs = 8 * 60 * 60_000;
  const requireDashboard = async (request: import("fastify").FastifyRequest, reply: import("fastify").FastifyReply) => {
    const cookie = request.headers.cookie ?? "";
    const sessionId = cookie.match(/(?:^|;\s*)pc_power_session=([^;]+)/)?.[1];
    const csrf = request.headers["x-csrf-token"];
    const expiresAt = sessionId ? sessions.get(sessionId) : undefined;
    if (!sessionId || !expiresAt || expiresAt <= Date.now() || !csrf || !crypto.timingSafeEqual(Buffer.from(String(csrf)), Buffer.from(sessionId))) {
      if (sessionId) sessions.delete(sessionId);
      return reply.code(401).send({ error: "unauthorized" });
    }
  };
  const requireDevice = async (request: import("fastify").FastifyRequest<{ Params: { deviceId: string } }>, reply: import("fastify").FastifyReply) => {
    const authorization = request.headers.authorization;
    const credential = authorization?.startsWith("Bearer ") ? authorization.slice(7) : "";
    if (!pairing.authenticateDevice(request.params.deviceId, credential)) return reply.code(401).send({ error: "unauthorized" });
  };
  const requireAdmin = async (request: import("fastify").FastifyRequest, reply: import("fastify").FastifyReply) => {
    const authorization = request.headers.authorization;
    const expected = `Bearer ${adminToken}`;
    if (authorization !== expected) return reply.code(401).send({ error: "unauthorized" });
  };

  app.post("/api/device/pair", async (request, reply) => {
    try {
      const result = await pairing.pairDevice(request.body);
      return reply.code(201).send(result);
    } catch (error) {
      if (error instanceof PairingError) return reply.code(error.statusCode).send({ error: error.message });
      throw error;
    }
  });

  app.get("/", async (_request, reply) => reply.type("text/html; charset=utf-8").send(dashboardHtml));

  app.post<{ Body: { token?: string; username?: string; password?: string } }>("/api/dashboard/login", async (request, reply) => {
    const provided = typeof request.body?.token === "string" ? request.body.token : "";
    const actual = Buffer.from(provided);
    const expected = Buffer.from(adminToken);
    const tokenValid = actual.length === expected.length && crypto.timingSafeEqual(actual, expected);
    const username = typeof request.body?.username === "string" ? request.body.username : "";
    const password = typeof request.body?.password === "string" ? request.body.password : "";
    const user = username ? userStore.findUser(username) : null;
    const userValid = Boolean(user?.enabled && verifyPassword(password, user.passwordHash));
    if (!tokenValid && !userValid) return reply.code(401).send({ error: "unauthorized" });
    const sessionId = crypto.randomBytes(32).toString("base64url");
    sessions.set(sessionId, Date.now() + sessionLifetimeMs);
    sessionUsers.set(sessionId, tokenValid ? "token" : username);
    return reply.header("Set-Cookie", `pc_power_session=${sessionId}; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=28800`)
      .send({ csrfToken: sessionId });
  });

  app.post("/api/dashboard/logout", { preHandler: requireDashboard }, async (request, reply) => {
    const sessionId = (request.headers.cookie ?? "").match(/(?:^|;\s*)pc_power_session=([^;]+)/)?.[1];
    if (sessionId) { sessions.delete(sessionId); sessionUsers.delete(sessionId); }
    return reply.header("Set-Cookie", "pc_power_session=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0").code(204).send();
  });

  app.get("/api/dashboard/users", { preHandler: requireDashboard }, async () => userStore.listUsers());

  app.post<{ Body: { username?: string; password?: string } }>("/api/dashboard/users", { preHandler: requireDashboard }, async (request, reply) => {
    const username = typeof request.body?.username === "string" ? request.body.username : "";
    const password = typeof request.body?.password === "string" ? request.body.password : "";
    if (!validUsername(username) || password.length < 12 || password.length > 256) return reply.code(400).send({ error: "username must be 3-40 characters and password must be 12-256 characters" });
    const now = new Date().toISOString();
    try {
      userStore.upsertUser({ username, passwordHash: hashPassword(password), enabled: true, createdAt: now, updatedAt: now });
      return reply.code(201).send({ username, enabled: true });
    } catch (error) {
      request.log.error({ err: error }, "failed to save dashboard user");
      return reply.code(500).send({ error: "failed to save user" });
    }
  });

  app.patch<{ Params: { username: string }; Body: { password?: string; enabled?: boolean } }>("/api/dashboard/users/:username", { preHandler: requireDashboard }, async (request, reply) => {
    const current = userStore.findUser(request.params.username);
    if (!current) return reply.code(404).send({ error: "user not found" });
    const password = typeof request.body?.password === "string" ? request.body.password : null;
    if (password !== null && (password.length < 12 || password.length > 256)) return reply.code(400).send({ error: "password must be 12-256 characters" });
    if (password !== null) userStore.upsertUser({ ...current, passwordHash: hashPassword(password), updatedAt: new Date().toISOString() });
    if (typeof request.body?.enabled === "boolean") userStore.setUserEnabled(request.params.username, request.body.enabled, new Date().toISOString());
    return reply.send({ username: request.params.username, updated: true });
  });

  app.delete<{ Params: { username: string } }>("/api/dashboard/users/:username", { preHandler: requireDashboard }, async (request, reply) => {
    const sessionId = (request.headers.cookie ?? "").match(/(?:^|;\\s*)pc_power_session=([^;]+)/)?.[1];
    if (sessionId && sessionUsers.get(sessionId) === request.params.username) return reply.code(400).send({ error: "cannot delete current user" });
    return userStore.deleteUser(request.params.username) ? reply.code(204).send() : reply.code(404).send({ error: "user not found" });
  });

  app.get("/api/dashboard/devices", { preHandler: requireDashboard }, async () => {
    const now = Date.now();
    return repository.listDevices().map(({ credentialHash: _secret, ...device }) => ({ ...device, online: Boolean(device.lastSeenAt && now - Date.parse(device.lastSeenAt) <= 45_000) }));
  });

  app.get<{ Querystring: { deviceId?: string } }>("/api/dashboard/commands", { preHandler: requireDashboard }, async (request) => repository.listCommands(request.query.deviceId, 50));

  app.post<{ Params: { deviceId: string }; Body: { csrfToken?: string; idempotencyKey?: string } }>("/api/dashboard/devices/:deviceId/power-press", { preHandler: requireDashboard }, async (request, reply) => {
    try { return reply.code(201).send(commands.createPowerPress(request.params.deviceId, request.body?.idempotencyKey ?? crypto.randomUUID())); }
    catch (error) { if (error instanceof CommandError) return reply.code(error.statusCode).send({ error: error.message }); throw error; }
  });

  app.post("/api/admin/pairing-codes", { preHandler: requireAdmin }, async (_request, reply) => {
    return reply.code(201).send(await pairing.createPairingCode());
  });

  app.delete<{ Params: { deviceId: string } }>("/api/admin/devices/:deviceId/credential", { preHandler: requireAdmin }, async (request, reply) => {
    const revoked = pairing.revokeDevice(request.params.deviceId);
    return revoked ? reply.code(204).send() : reply.code(404).send({ error: "device not found" });
  });

  app.post<{ Params: { deviceId: string }; Body: { idempotencyKey?: string } }>("/api/admin/devices/:deviceId/power-press", { preHandler: requireAdmin }, async (request, reply) => {
    try {
      const idempotencyKey = request.body?.idempotencyKey ?? crypto.randomUUID();
      return reply.code(201).send(commands.createPowerPress(request.params.deviceId, idempotencyKey));
    } catch (error) {
      if (error instanceof CommandError) return reply.code(error.statusCode).send({ error: error.message });
      throw error;
    }
  });

  app.post<{ Params: { deviceId: string } }>("/api/device/:deviceId/seen", { preHandler: requireDevice }, async (request, reply) => {
    const body = request.body;
    const diagnostics = validateDeviceSnapshot(body) && body.deviceId === request.params.deviceId ? {
      reportedAt: body.reportedAt, deviceIp: body.deviceIp, hostIp: body.hostIp,
      wifi: body.wifi, ssid: body.ssid, reachability: body.reachability,
      sensorAvailable: body.sensorAvailable, temperature: body.temperature, humidity: body.humidity,
      powerLedState: body.powerLedState,
    } : undefined;
    const seen = commands.reportSeen(request.params.deviceId, new Date().toISOString(), diagnostics);
    return seen ? reply.send({ status: "ok" }) : reply.code(401).send({ error: "unauthorized" });
  });

  app.get<{ Params: { deviceId: string } }>("/api/device/:deviceId/commands/next", { preHandler: requireDevice }, async (request) => {
    const command = commands.claim(request.params.deviceId);
    return { command: command ? { ...command, kind: "power_press" } : null };
  });

  app.post<{ Params: { deviceId: string } }>("/api/device/:deviceId/commands/result", { preHandler: requireDevice }, async (request, reply) => {
    try { return reply.send({ accepted: commands.complete(request.params.deviceId, request.body) }); }
    catch (error) { if (error instanceof CommandError) return reply.code(error.statusCode).send({ error: error.message }); throw error; }
  });

  app.get<{ Params: { commandId: string } }>("/api/admin/commands/:commandId", { preHandler: requireAdmin }, async (request, reply) => {
    const command = repository.findCommand(request.params.commandId);
    return command ? reply.send(command) : reply.code(404).send({ error: "command not found" });
  });

  app.get("/health", async () => ({ status: "ok" }));
  app.addHook("onClose", async () => {
    const closable = repository as PairingRepository & { close?: () => void };
    closable.close?.();
  });
  return app;
}
