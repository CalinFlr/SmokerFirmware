#pragma once

#include <string_view>

namespace smoker::platform::web_assets {

inline constexpr std::string_view login_html = R"LOGIN(<!doctype html>
<html lang="ro"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark light"><title>Fumuri · autentificare</title><link rel="icon" href="data:,"><link rel="stylesheet" href="/login.css"><script src="/login.js" defer></script></head>
<body><main><div class="brand" aria-label="Fumuri"><span aria-hidden="true">F</span><div><p>CONTROL LOCAL PENTRU AFUMĂTOARE</p><h1>Fumuri</h1></div></div><section><p class="eyebrow">REȚEAUA LOCALĂ STA</p><h2>Autentificare dispozitiv</h2><p>Introdu doar parola dispozitivului pentru a accesa configurarea și controlul local.</p><form method="post" action="/login"><label>Parola dispozitivului<span class="password-field"><input id="login-password" name="password" type="password" minlength="8" maxlength="63" autocomplete="current-password" required autofocus><button id="toggle-password" class="password-toggle" type="button" aria-controls="login-password" aria-pressed="false">Arată</button></span></label><button type="submit">Intră în Fumuri</button></form><p class="warning"><b>HTTP fără TLS</b><br>Autentificarea și cookie-ul de sesiune nu sunt criptate end-to-end.</p></section></main></body></html>)LOGIN";

inline constexpr std::string_view login_error_html = R"LOGIN(<!doctype html>
<html lang="ro"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark light"><title>Fumuri · autentificare</title><link rel="icon" href="data:,"><link rel="stylesheet" href="/login.css"><script src="/login.js" defer></script></head>
<body><main><div class="brand" aria-label="Fumuri"><span aria-hidden="true">F</span><div><p>CONTROL LOCAL PENTRU AFUMĂTOARE</p><h1>Fumuri</h1></div></div><section><p class="eyebrow">PORTAL LOCAL</p><h2>Autentificare dispozitiv</h2><p class="error" role="alert">Parola nu este corectă.</p><form method="post" action="/login"><label>Parola dispozitivului<span class="password-field"><input id="login-password" name="password" type="password" minlength="8" maxlength="63" autocomplete="current-password" required autofocus><button id="toggle-password" class="password-toggle" type="button" aria-controls="login-password" aria-pressed="false">Arată</button></span></label><button type="submit">Încearcă din nou</button></form><p class="warning"><b>HTTP fără TLS</b><br>Autentificarea poate fi observată de alți clienți aflați pe aceeași rețea locală.</p></section></main></body></html>)LOGIN";

inline constexpr std::string_view login_rate_limited_html = R"LOGIN(<!doctype html>
<html lang="ro"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark light"><title>Fumuri · autentificare</title><link rel="icon" href="data:,"><link rel="stylesheet" href="/login.css"><script src="/login.js" defer></script></head>
<body><main><div class="brand" aria-label="Fumuri"><span aria-hidden="true">F</span><div><p>CONTROL LOCAL PENTRU AFUMĂTOARE</p><h1>Fumuri</h1></div></div><section><p class="eyebrow">PORTAL LOCAL</p><h2>Prea multe încercări</h2><p class="error" role="alert">Autentificarea este limitată temporar pentru acest client. Așteaptă și încearcă din nou.</p><form method="post" action="/login"><label>Parola dispozitivului<span class="password-field"><input id="login-password" name="password" type="password" minlength="8" maxlength="63" autocomplete="current-password" required><button id="toggle-password" class="password-toggle" type="button" aria-controls="login-password" aria-pressed="false">Arată</button></span></label><button type="submit">Încearcă din nou</button></form></section></main></body></html>)LOGIN";

inline constexpr std::string_view login_css = R"CSS(:root{color-scheme:light;--ember:#ee7d3b;--paper:#f2eee5;--ink:#171918;--surface:#fbf8f1;--muted:#6d736d;--line:#d7d1c7;--focus:#1674d1;font:16px/1.45 Arial,Helvetica,sans-serif}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at 80% 10%,#ee7d3b22,transparent 32%),var(--paper);color:var(--ink)}main{width:min(100% - 28px,460px);padding:24px}.brand{display:flex;align-items:center;gap:12px;margin-bottom:18px}.brand>span{width:48px;height:48px;display:grid;place-items:center;border-radius:50%;background:var(--ember);color:#fff;font:600 1.6rem Georgia,"Times New Roman",serif}.brand p,.eyebrow{margin:0;color:var(--muted);font-size:.68rem;font-weight:800;letter-spacing:.11em}.brand h1{margin:0;font:500 2rem/1 Georgia,"Times New Roman",serif}section{padding:24px;border:1px solid var(--line);border-radius:24px;background:var(--surface);box-shadow:0 18px 55px #6c625b22}h2{margin:.4rem 0 1rem;font:500 2rem/1.05 Georgia,"Times New Roman",serif}section>p{color:var(--muted)}label{display:grid;gap:7px;margin:16px 0;font-size:.82rem;font-weight:700}.password-field{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px}input{min-height:48px;width:100%;border:1px solid var(--line);border-radius:13px;background:var(--paper);padding:11px 13px;font:16px Arial,Helvetica,sans-serif;color:var(--ink)}input:focus-visible,button:focus-visible{outline:3px solid var(--focus);outline-offset:2px}button{min-height:48px;width:100%;border:1px solid var(--ember);border-radius:13px;background:var(--ember);padding:10px 15px;color:#fff;font:800 16px Arial,Helvetica,sans-serif}.password-toggle{min-width:82px;width:auto;border-color:var(--line);background:transparent;color:var(--ink)}.warning{margin:18px 0 0;padding:12px;border:1px solid #c6b78066;border-radius:13px;background:#c6b78010;font-size:.78rem}.error{padding:11px;border:1px solid #bb572866;border-radius:12px;background:#bb572812;color:#9d3e22}@media(prefers-color-scheme:dark){:root{color-scheme:dark;--paper:#111411;--ink:#f2eee5;--surface:#1e211e;--muted:#9da39c;--line:#343934}})CSS";

inline constexpr std::string_view login_js = R"JS('use strict';
const password=document.querySelector('#login-password');
const toggle=document.querySelector('#toggle-password');
if(password&&toggle){toggle.addEventListener('click',()=>{const reveal=password.type==='password';password.type=reveal?'text':'password';toggle.textContent=reveal?'Ascunde':'Arată';toggle.setAttribute('aria-pressed',String(reveal));password.focus()})}
)JS";

inline constexpr std::string_view setup_html = R"SETUP(<!doctype html>
<html lang="ro"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark light"><title>Fumuri · configurare Wi-Fi</title><link rel="icon" href="data:,"><link rel="stylesheet" href="/setup.css"><script src="/setup.js" defer></script></head>
<body><main><header><span aria-hidden="true">F</span><div><p>COMMISSIONING · NUMAI WI-FI</p><h1>Conectează Fumuri</h1></div></header><section><p>Acest SoftAP public permite exclusiv alegerea rețelei locale. Nu expune temperaturi, sesiuni, heater, snapshoturi sau comenzi.</p><div id="state" class="state">Pregătim scanarea rețelelor de 2,4 GHz…</div><button id="scan" type="button">Scanează din nou</button><div id="networks" class="networks" aria-live="polite"></div><form id="setup-form"><label>SSID<input id="ssid" maxlength="32" autocomplete="off" required></label><label>Parola WPA2/WPA3<input id="wifi-password" type="password" minlength="8" maxlength="63" autocomplete="new-password" required></label><button>Salvează și conectează</button></form><p class="note">Sunt acceptate numai rețele WPA2/WPA3 Personal cu parolă de 8–63 caractere. După conectare, SoftAP-ul dispare; deschide adresa <strong id="hostname">smoker.local</strong> din rețeaua locală și autentifică-te cu parola dispozitivului.</p><p id="message" role="status"></p></section></main></body></html>)SETUP";

inline constexpr std::string_view setup_css = R"CSS(:root{color-scheme:light;--ember:#ee7d3b;--paper:#f2eee5;--ink:#171918;--surface:#fbf8f1;--muted:#6d736d;--line:#d7d1c7;--focus:#1674d1;font:16px/1.45 Arial,Helvetica,sans-serif}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 85% 5%,#ee7d3b22,transparent 30%),var(--paper);color:var(--ink)}main{width:min(100% - 28px,680px);margin:auto;padding:28px 0}header{display:flex;align-items:center;gap:13px;margin-bottom:18px}header>span{width:50px;height:50px;display:grid;place-items:center;border-radius:16px;background:var(--ink);color:var(--paper);font:700 25px Georgia,serif}header p{margin:0;color:var(--muted);font-size:.68rem;font-weight:800;letter-spacing:.12em}h1{margin:0;font:500 2rem Georgia,serif}section{padding:24px;border:1px solid var(--line);border-radius:24px;background:var(--surface)}label{display:grid;gap:7px;margin:15px 0;font-weight:700;font-size:.84rem}input,button{min-height:48px;border:1px solid var(--line);border-radius:13px;padding:10px 13px;font:16px Arial,sans-serif}input{width:100%;background:var(--paper);color:var(--ink)}button{background:var(--ember);border-color:var(--ember);color:#fff;font-weight:800;cursor:pointer}button:focus-visible,input:focus-visible{outline:3px solid var(--focus);outline-offset:2px}#scan{width:100%;margin:10px 0}.networks{display:grid;gap:7px;margin:10px 0}.network{width:100%;display:flex;justify-content:space-between;text-align:left;background:transparent;color:var(--ink)}.network:disabled{opacity:.55;border-style:dashed}.network small{color:var(--muted)}form{margin-top:20px;padding-top:14px;border-top:1px solid var(--line)}form button{width:100%}.state,.note,#message{padding:11px;border-radius:12px;background:#83a89e16;color:var(--muted)}#message:empty{display:none}@media(prefers-color-scheme:dark){:root{color-scheme:dark;--paper:#111411;--ink:#f2eee5;--surface:#1e211e;--muted:#9da39c;--line:#343934}})CSS";

inline constexpr std::string_view setup_js = R"JS('use strict';
const $=selector=>document.querySelector(selector);let pollTimer=null;let networks=[];
async function request(path,options={}){const headers={...(options.headers||{})};if(options.body)headers['Content-Type']='application/json';const response=await fetch(path,{cache:'no-store',credentials:'same-origin',...options,headers});let data={};try{data=await response.json()}catch{}if(!response.ok)throw new Error(data.error||`HTTP ${response.status}`);return data}
function render(data){networks=data.networks||[];$('#networks').innerHTML=networks.length?networks.map((network,index)=>`<button class="network" data-index="${index}" type="button" ${network.supported?'':'disabled'}><span><b></b><br><small></small></span><span>${network.rssi_dbm} dBm</span></button>`).join(''):'<p>Nicio rețea vizibilă; poți introduce manual un SSID WPA2/WPA3.</p>';document.querySelectorAll('[data-index]').forEach(button=>{const network=networks[Number(button.dataset.index)];button.querySelector('b').textContent=network.ssid;button.querySelector('small').textContent=`${network.security}${network.supported?'':' · nesuportată'} · canal ${network.channel}`;button.onclick=()=>{$('#ssid').value=network.ssid;$('#wifi-password').focus()}});$('#state').textContent=data.state==='scanning'?'Scanare în curs…':data.state==='error'?'Scanarea a eșuat; introdu SSID-ul manual.':`${networks.length} rețele vizibile`;clearTimeout(pollTimer);if(data.state==='scanning')pollTimer=setTimeout(pollScan,800)}
async function pollScan(){try{render(await request('/api/v1/network/scan'))}catch(error){$('#message').textContent=error.message}}
async function scan(){try{await request('/api/v1/network/scan',{method:'POST'});await pollScan()}catch(error){$('#message').textContent=error.message}}
async function status(){try{const data=await request('/api/v1/setup/status');$('#hostname').textContent=`${data.hostname}.local`;if(data.sta_connected)$('#state').textContent=`Conectat. Deschide ${data.hostname}.local` }catch{}}
$('#scan').onclick=scan;$('#setup-form').onsubmit=async event=>{event.preventDefault();const ssid=$('#ssid').value;const password=$('#wifi-password').value;if(!ssid||ssid.length>32)return $('#message').textContent='SSID-ul trebuie să aibă 1–32 caractere.';if(password.length<8||password.length>63)return $('#message').textContent='Parola WPA2/WPA3 trebuie să aibă 8–63 caractere.';try{await request('/api/v1/setup/network',{method:'PUT',body:JSON.stringify({ssid,wifi_password:password})});$('#message').textContent='Configurația a fost salvată. SoftAP-ul se va închide după conectarea STA.';$('#wifi-password').value='';setTimeout(status,1200)}catch(error){$('#message').textContent=error.message}};void status();void scan();
)JS";

inline constexpr std::string_view index_html = R"HTML(<!doctype html>
<html lang="ro"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark light"><title>Fumuri · control local</title><link rel="icon" href="data:,"><link rel="stylesheet" href="/app.css"></head>
<body><header class="site-header"><a class="brand" href="/" aria-label="Fumuri"><span aria-hidden="true">F</span><div><p>CONTROL LOCAL PENTRU AFUMĂTOARE</p><h1>Fumuri</h1></div></a><div class="header-actions"><span id="connection" class="status-pill offline"><i></i> API indisponibil</span><button id="logout" class="secondary" type="button">Ieșire</button><label class="theme-control"><span>Temă</span><select id="theme" aria-label="Tema interfeței"><option value="system">Sistem</option><option value="light">Luminos</option><option value="dark">Întunecat</option></select></label></div></header>
<main id="page"><section class="notices" aria-label="Avertismente"><article class="notice simulation"><b>I/O simulat</b><span>Temperaturile și heater-ul nu provin încă de la senzori sau SSR real.</span></article><article id="default-password" class="notice password hidden"><b>Parolă dispozitiv inițială</b><span>Parola implicită este smoker257500; schimbarea ei este recomandată.</span></article><article class="notice security"><b>HTTP fără TLS</b><span>Loginul local și cookie-ul de sesiune pot fi observate în rețeaua locală.</span></article><article class="notice storage"><b>NVS necriptat</b><span>Credentialele Wi‑Fi sunt stocate necriptat în M12.</span></article></section>

<section id="dashboard" class="dashboard" aria-labelledby="live-title"><div class="section-heading"><div><p class="eyebrow">SESIUNE LIVE · I/O SIMULAT</p><h2 id="live-title">Controlul afumării</h2></div><span id="session-pill" class="status-pill neutral"><i></i><span>IDLE</span></span></div>
<div class="live-grid"><article class="temperature-card"><div class="temp-copy"><p>Camera afumătorii</p><strong id="chamber">—<small>°C</small></strong><span id="chamber-target">Țintă —</span></div><div id="heater-ring" class="heater-ring"><div><small>HEATER</small><strong id="heater">0%</strong><span>simulat</span></div></div><div class="session-facts"><div><small>Stare</small><strong id="session-status">IDLE</strong></div><div><small>Timer</small><strong id="timer">Inactiv</strong></div><div><small>Motiv stop</small><strong id="stop-reason">—</strong></div></div></article>
<article class="card session-card"><div class="card-title"><div><p class="eyebrow">O SINGURĂ ETAPĂ V0</p><h3>Sesiune</h3></div><span class="ornament">01</span></div><p class="muted">Pornirea este explicită. Nicio comandă din UI nu poate porni heater-ul în afara controlului și siguranței locale.</p><label>Țintă cameră la pornire <span class="input-shell"><input id="start-target" type="number" inputmode="decimal" min="0" max="150" step="0.5" value="110"><b>°C</b></span></label><div class="button-row"><button id="start" class="primary">Pornește sesiunea</button><button id="stop" class="danger">Oprește</button></div><form id="chamber-form" class="inline-form"><label>Țintă activă <span class="input-shell"><input id="active-target" type="number" inputmode="decimal" step="0.5"><b>°C</b></span></label><button>Aplică</button></form></article></div>

<div class="content-grid"><article class="card"><div class="card-title"><div><p class="eyebrow">MĂSURĂTORI</p><h3>Sonde alimentare</h3></div><span id="probe-count" class="count">0</span></div><div id="probes" class="probe-list"><p class="empty">Nicio sondă în snapshot.</p></div></article><article class="card"><div class="card-title"><div><p class="eyebrow">ATENȚIE</p><h3>Alarme și fault</h3></div><span class="ornament sage">!</span></div><div id="fault" class="fault-box ok">Niciun fault activ.</div><div id="alarms" class="alarm-list"><p class="empty">Nicio alarmă activă.</p></div></article></div></section>

<section id="history-panel" class="network card history" aria-labelledby="history-title"><div class="section-heading"><div><p class="eyebrow">ISTORIC LOCAL · FLASH INTERN</p><h2 id="history-title">Sesiuni și telemetrie</h2><p class="muted">Mostre la 60 de secunde și schimbări importante. Timpul relativ rămâne disponibil fără Internet.</p></div><span id="history-pill" class="status-pill neutral"><i></i><span>Se încarcă</span></span></div><div class="history-toolbar"><label>Sesiune<select id="history-session" aria-label="Selectează sesiunea"><option value="">Nicio sesiune</option></select></label><p id="history-summary" class="muted">Istoricul se încarcă…</p></div><div id="history-warning" class="api-error hidden" role="status"></div><div class="history-chart-wrap"><canvas id="history-chart" width="900" height="330" aria-label="Grafic istoric temperatură și heater" role="img"></canvas><p id="history-empty" class="empty">Nu există încă date pentru afișare.</p></div><div class="history-legend" aria-label="Legendă"><span class="chamber">Cameră</span><span class="target">Țintă</span><span class="probe-series">Sondă aliment</span><span class="heater-series">Heater</span><span class="marker-series">Timer / alarmă / fault / final</span></div></section>

<section id="network-panel" class="network card" aria-labelledby="network-title"><div class="section-heading"><div><p id="network-kicker" class="eyebrow">DISPOZITIV ȘI REȚEA</p><h2 id="network-title">Conectare Wi‑Fi</h2><p id="network-intro" class="muted">Alege o rețea WPA2/WPA3 Personal de 2,4 GHz sau introdu SSID-ul manual.</p></div><span id="network-pill" class="status-pill neutral"><i></i><span>Se încarcă</span></span></div><div class="network-grid"><div class="scan-column"><div class="subheading"><div><h3>Rețele disponibile</h3><p id="scan-summary" class="muted">Scanarea pornește automat.</p></div><button id="scan-again" class="secondary">Scanează din nou</button></div><div id="scan-error" class="api-error hidden" role="alert"></div><div id="networks" class="network-list" aria-live="polite"><div class="scan-placeholder"><i></i><span>Pregătim scanarea…</span></div></div></div><form id="network-form" class="provision-form"><div><p class="eyebrow">CONFIGURARE MANUALĂ DISPONIBILĂ</p><h3>Credentiale STA</h3></div><label>Numele rețelei (SSID)<span class="input-shell"><input id="ssid" maxlength="32" autocomplete="off" required></span></label><label>Parola WPA2/WPA3<span class="input-shell"><input id="wifi-password" type="password" minlength="8" maxlength="63" autocomplete="new-password" required></span></label><button class="primary">Salvează și conectează</button><small class="auth-note">Sunt acceptate numai WPA2/WPA3 Personal. Parolele nu sunt returnate de API.</small></form></div><footer class="device-strip"><div><small>STA</small><strong id="sta-state">Neconectat</strong></div><div><small>SOFTAP COMMISSIONING</small><strong id="ap-state">Se verifică</strong></div><div><small>ADRESĂ LOCALĂ</small><strong id="hostname">—</strong></div></footer></section>
<section id="firmware-panel" class="network card firmware" aria-labelledby="firmware-title"><div class="section-heading"><div><p class="eyebrow">ACTUALIZARE MANUALĂ · HTTPS</p><h2 id="firmware-title">Actualizare firmware</h2><p class="muted">Verificarea folosește release-ul public Fumuri. Instalarea cere sesiunea oprită, repornește controlerul și poate întrerupe temporar această pagină.</p></div><span id="firmware-pill" class="status-pill neutral"><i></i><span>IDLE</span></span></div><div class="firmware-grid"><div class="firmware-versions"><div><small>VERSIUNE CURENTĂ</small><strong id="firmware-current">—</strong></div><div><small>VERSIUNE DISPONIBILĂ</small><strong id="firmware-available">—</strong></div></div><div><div class="firmware-progress" role="progressbar" aria-label="Progres actualizare" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><i id="firmware-progress"></i></div><p id="firmware-state" class="muted">Actualizările automate sunt dezactivate.</p><p id="firmware-error" class="api-error hidden" role="alert"></p><div class="button-row"><button id="firmware-check" class="secondary" type="button">Verifică actualizări</button><button id="firmware-install" class="primary" type="button" disabled>Instalează</button></div><small class="auth-note">Oprește sesiunea înainte de instalare. După reboot, reconectează-te dacă pagina nu revine automat. Secure Boot și criptarea flash nu fac parte din M13.</small></div></div></section>
<section class="network card" aria-labelledby="password-title"><div class="section-heading"><div><p class="eyebrow">AUTENTIFICARE LOCALĂ</p><h2 id="password-title">Schimbă parola dispozitivului</h2><p class="muted">Schimbarea parolei închide sesiunea curentă și cere autentificare din nou.</p></div></div><form id="password-form" class="provision-form"><label>Parola curentă<span class="input-shell"><input id="current-password" type="password" minlength="8" maxlength="63" autocomplete="current-password" required></span></label><label>Parola nouă<span class="input-shell"><input id="new-password" type="password" minlength="8" maxlength="63" autocomplete="new-password" required></span></label><button class="primary">Schimbă parola</button></form></section>
<p id="message" class="toast" role="status" aria-live="polite"></p></main><script src="/app.js" defer></script></body></html>)HTML";

inline constexpr std::string_view app_css = R"CSS(:root{color-scheme:light;--ember:#ee7d3b;--ember-deep:#bb5728;--sage:#83a89e;--gold:#c6b780;--paper:#f2eee5;--ink:#171918;--app:#f4f0e7;--app2:#ece7de;--surface:#fbf8f1;--surface2:#e9e4da;--surface3:#ded8ce;--text:#1c1f1c;--muted:#6d736d;--soft:#4e534e;--line:#d7d1c7;--line2:#c6c0b6;--button:#1d201d;--button-text:#f3eee5;--shadow:#6c625b22;--focus:#1674d1;--heater:0%;font:16px/1.45 Arial,Helvetica,sans-serif}
@media(prefers-color-scheme:dark){:root:not([data-theme]){color-scheme:dark;--app:#111411;--app2:#171a17;--surface:#1e211e;--surface2:#242824;--surface3:#2b2f2b;--text:#f2eee5;--muted:#9da39c;--soft:#c5c8c1;--line:#343934;--line2:#424842;--button:#ebe5da;--button-text:#1b1d1b;--shadow:#08090788}}
:root[data-theme=dark]{color-scheme:dark;--app:#111411;--app2:#171a17;--surface:#1e211e;--surface2:#242824;--surface3:#2b2f2b;--text:#f2eee5;--muted:#9da39c;--soft:#c5c8c1;--line:#343934;--line2:#424842;--button:#ebe5da;--button-text:#1b1d1b;--shadow:#08090788}:root[data-theme=light]{color-scheme:light}
*{box-sizing:border-box}html{min-height:100%;background:var(--app)}body{min-height:100%;margin:0;color:var(--text);background:radial-gradient(circle at 88% 0,#ee7d3b18 0 17%,transparent 17.3%),radial-gradient(circle at 3% 55%,#83a89e14 0 18%,transparent 18.3%),var(--app);font-family:Arial,Helvetica,sans-serif}button,input,select{font:inherit;color:inherit}button,select{cursor:pointer}button,input,select{min-height:44px}input,select{font-size:16px}button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:3px solid var(--focus);outline-offset:3px}.hidden{display:none!important}
.site-header,main{width:min(1180px,calc(100% - 32px));margin:auto}main{display:flex;flex-direction:column}.site-header{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:28px 0 20px}.brand{display:flex;align-items:center;gap:13px;color:inherit;text-decoration:none}.brand>span{width:50px;height:50px;display:grid;place-items:center;border-radius:16px;background:var(--ink);color:var(--paper);font:700 25px Georgia,"Times New Roman",serif;box-shadow:inset 0 0 0 1px #fff1}.brand p,.eyebrow{margin:0 0 3px;color:var(--muted);font-size:.69rem;font-weight:800;letter-spacing:.14em}.brand h1{margin:0;font:500 2rem/.95 Georgia,"Times New Roman",serif;letter-spacing:-.04em}.header-actions{display:flex;align-items:center;gap:10px}.theme-control{display:flex;align-items:center;gap:8px;padding-left:12px;border-left:1px solid var(--line)}.theme-control>span{position:absolute;clip:rect(0 0 0 0)}.theme-control select{border:1px solid var(--line);border-radius:999px;background:var(--surface);padding:0 34px 0 14px}
.status-pill{min-height:34px;display:inline-flex;align-items:center;gap:7px;padding:7px 11px;border:1px solid color-mix(in srgb,var(--sage) 35%,var(--line));border-radius:999px;background:color-mix(in srgb,var(--sage) 13%,var(--surface));font-size:.76rem;font-weight:800;white-space:nowrap}.status-pill i{width:7px;height:7px;border-radius:50%;background:var(--sage);box-shadow:0 0 0 4px color-mix(in srgb,var(--sage) 16%,transparent)}.status-pill.offline,.status-pill.fault{border-color:#d56c5059;background:#d56c5015}.status-pill.offline i,.status-pill.fault i{background:var(--ember)}.status-pill.neutral i{background:var(--gold)}
.notices{order:-2;display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-bottom:18px}.notice{display:grid;gap:3px;min-height:68px;padding:11px 13px;border:1px solid var(--line);border-radius:14px;background:var(--surface);font-size:.76rem}.notice b{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em}.notice span{color:var(--muted);line-height:1.35}.notice.simulation{border-color:#83a89e55;background:#83a89e12}.notice.password{border-color:#ee7d3b66;background:#ee7d3b12}.notice.security,.notice.storage{border-color:#c6b78066;background:#c6b78010}
.dashboard,.network{margin-bottom:18px}.section-heading{display:flex;align-items:flex-start;justify-content:space-between;gap:16px;margin-bottom:14px}.section-heading h2{margin:0;font:500 clamp(1.7rem,4vw,2.5rem)/1 Georgia,"Times New Roman",serif;letter-spacing:-.035em}.section-heading .muted{margin:.6rem 0 0;max-width:600px}.live-grid{display:grid;grid-template-columns:minmax(0,1.45fr) minmax(290px,.75fr);gap:14px}.temperature-card,.card{border:1px solid var(--line);border-radius:24px;background:color-mix(in srgb,var(--surface) 96%,transparent);box-shadow:0 15px 40px var(--shadow)}.temperature-card{min-height:360px;display:grid;grid-template-columns:1fr 190px;gap:24px;align-items:center;padding:clamp(22px,5vw,42px);overflow:hidden;background:radial-gradient(circle at 92% 7%,#ee7d3b24 0 19%,transparent 19.4%),linear-gradient(145deg,var(--surface2),var(--surface))}.temp-copy p{margin:0 0 8px;color:var(--muted);font-size:.8rem;font-weight:800;letter-spacing:.1em;text-transform:uppercase}.temp-copy>strong{display:block;font:500 clamp(4.2rem,9vw,7rem)/.85 Georgia,"Times New Roman",serif;letter-spacing:-.07em}.temp-copy>strong small{font-size:.3em;color:var(--muted);letter-spacing:0}.temp-copy>span{display:inline-block;margin-top:16px;padding:7px 10px;border-radius:999px;background:var(--surface);color:var(--soft);font-weight:800}.heater-ring{width:180px;aspect-ratio:1;display:grid;place-items:center;border-radius:50%;background:conic-gradient(var(--ember) var(--heater),var(--line) 0);box-shadow:inset 0 0 0 1px #ffffff12}.heater-ring:before{content:"";grid-area:1/1;width:145px;aspect-ratio:1;border-radius:50%;background:var(--surface);box-shadow:0 9px 25px var(--shadow)}.heater-ring>div{z-index:1;grid-area:1/1;display:grid;text-align:center}.heater-ring small,.heater-ring span{color:var(--muted);font-size:.66rem;font-weight:800;letter-spacing:.1em}.heater-ring strong{font:500 2.2rem Georgia,"Times New Roman",serif}.session-facts{grid-column:1/-1;display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.session-facts div{display:grid;gap:3px;padding:12px;border:1px solid var(--line);border-radius:14px;background:color-mix(in srgb,var(--surface) 75%,transparent)}.session-facts small{color:var(--muted);font-size:.68rem}.session-facts strong{font:500 1rem Georgia,"Times New Roman",serif}
.card{padding:20px}.card-title,.subheading{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.card-title h3,.subheading h3,.provision-form h3{margin:0;font:500 1.45rem Georgia,"Times New Roman",serif}.ornament,.count{width:36px;height:36px;display:grid;place-items:center;border-radius:12px;background:var(--ember);color:#fff;font:600 1rem Georgia,"Times New Roman",serif}.ornament.sage{background:var(--sage)}.count{width:auto;min-width:36px;padding:0 10px;background:var(--surface3);color:var(--text)}.muted,.empty{color:var(--muted)}label{display:grid;gap:7px;margin:14px 0;color:var(--soft);font-size:.82rem;font-weight:700}label small{font-weight:400;color:var(--muted)}.input-shell{min-height:48px;display:flex;align-items:center;border:1px solid var(--line2);border-radius:13px;background:var(--app);overflow:hidden}.input-shell:focus-within{border-color:var(--focus)}.input-shell input{width:100%;min-width:0;border:0;outline:0;background:none;padding:11px 13px}.input-shell b{padding:0 13px;color:var(--muted)}button{min-height:44px;border:1px solid var(--line2);border-radius:13px;background:var(--surface2);padding:10px 15px;font-weight:800}button:hover{filter:brightness(1.06)}button:disabled{cursor:not-allowed;opacity:.48}.primary{border-color:var(--ember);background:var(--ember);color:#fff}.primary:hover{background:var(--ember-deep)}.danger{border-color:#b75c454f;background:#b75c4512;color:#d56f52}.secondary{background:transparent}.button-row{display:grid;grid-template-columns:1fr auto;gap:8px}.inline-form{display:grid;grid-template-columns:1fr auto;align-items:end;gap:8px;margin-top:9px}.inline-form label{margin:0}
.content-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}.probe-list,.alarm-list{display:grid}.probe{display:grid;grid-template-columns:minmax(110px,1fr) auto;gap:12px;padding:14px 0;border-top:1px solid var(--line)}.probe:first-child{border-top:0}.probe-info{display:grid;grid-template-columns:40px 1fr;gap:10px;align-items:center}.probe-no{width:38px;height:38px;display:grid;place-items:center;border-radius:50%;background:var(--sage);color:#fff;font:500 1rem Georgia,"Times New Roman",serif}.probe-info b{display:block}.probe-info span{color:var(--muted);font-size:.8rem}.probe-reading{font:500 1.5rem Georgia,"Times New Roman",serif;text-align:right}.probe-controls{grid-column:1/-1;display:grid;grid-template-columns:1fr repeat(3,auto);gap:7px;align-items:end}.probe-controls label{margin:0}.probe-controls button{padding-inline:11px;font-size:.78rem}.alarm,.fault-row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:12px 0;border-top:1px solid var(--line)}.alarm:first-child{border-top:0}.alarm span{color:var(--soft);font-size:.82rem}.fault-box{padding:12px;border-radius:13px;background:#d56c5015;border:1px solid #d56c5040}.fault-box.ok{background:#83a89e12;border-color:#83a89e40;color:var(--muted)}
.network{padding:clamp(20px,4vw,32px)}.network-grid{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(300px,.75fr);gap:22px}.subheading p{margin:.3rem 0 0;font-size:.8rem}.network-list{display:grid;gap:7px;margin-top:13px}.network-option{width:100%;display:grid;grid-template-columns:1fr auto;align-items:center;gap:10px;text-align:left;background:var(--surface)}.network-option.selected{border-color:var(--ember);box-shadow:inset 0 0 0 1px var(--ember)}.network-option.unsupported{border-style:dashed}.network-option b{display:block;overflow:hidden;text-overflow:ellipsis}.network-option small{color:var(--muted)}.signal{display:flex;align-items:end;gap:2px;height:20px}.signal i{width:4px;border-radius:2px;background:var(--line2)}.signal i:nth-child(1){height:5px}.signal i:nth-child(2){height:9px}.signal i:nth-child(3){height:14px}.signal i:nth-child(4){height:19px}.signal i.on{background:var(--sage)}.scan-placeholder{min-height:130px;display:grid;place-items:center;align-content:center;gap:11px;border:1px dashed var(--line2);border-radius:16px;color:var(--muted)}.scan-placeholder i{width:25px;height:25px;border:3px solid var(--line2);border-top-color:var(--ember);border-radius:50%;animation:spin .9s linear infinite}.api-error{margin-top:12px;padding:10px 12px;border:1px solid #d56c504f;border-radius:12px;background:#d56c5012;color:#d8785d}.provision-form{padding:18px;border-radius:18px;background:var(--surface2)}.provision-form>button{width:100%}.auth-note{display:block;margin-top:12px;color:var(--muted);line-height:1.5}.auth-note code{color:var(--ember)}.device-strip{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:20px;padding-top:18px;border-top:1px solid var(--line)}.device-strip div{display:grid;gap:2px}.device-strip small{color:var(--muted);font-size:.65rem;font-weight:800;letter-spacing:.09em}.device-strip strong{overflow-wrap:anywhere;font:500 .92rem Georgia,"Times New Roman",serif}.toast{position:sticky;z-index:10;bottom:14px;min-height:0;width:max-content;max-width:100%;margin:0 auto 16px;padding:0;border-radius:999px;background:var(--ink);color:var(--paper);box-shadow:0 10px 35px #0004;transition:.2s}.toast:not(:empty){padding:10px 16px}.setup-mode #network-panel{border-color:#ee7d3b55;box-shadow:0 18px 55px #bb572825}.setup-mode #network-kicker:after{content:" · CONFIGURARE NECESARĂ";color:var(--ember)}
.firmware-grid{display:grid;grid-template-columns:minmax(230px,.65fr) minmax(0,1.35fr);gap:22px}.firmware-versions{display:grid;grid-template-columns:1fr 1fr;gap:8px}.firmware-versions div{display:grid;gap:5px;padding:16px;border-radius:16px;background:var(--surface2)}.firmware-versions small{color:var(--muted);font-size:.65rem;font-weight:800;letter-spacing:.09em}.firmware-versions strong{font:500 1.4rem Georgia,"Times New Roman",serif}.firmware-progress{height:12px;overflow:hidden;border-radius:999px;background:var(--surface3)}.firmware-progress i{display:block;width:0;height:100%;background:linear-gradient(90deg,var(--sage),var(--ember));transition:width .25s}.firmware .button-row{grid-template-columns:1fr 1fr}.firmware .button-row button{width:100%}
.history-toolbar{display:grid;grid-template-columns:minmax(240px,.45fr) minmax(0,1fr);align-items:end;gap:18px}.history-toolbar label{margin:0}.history-toolbar select{width:100%;border:1px solid var(--line2);border-radius:13px;background:var(--app);padding:8px 38px 8px 12px}.history-toolbar p{margin:0 0 10px}.history-chart-wrap{position:relative;min-height:330px;margin-top:16px;border:1px solid var(--line);border-radius:17px;background:var(--surface2);overflow:hidden}.history-chart-wrap canvas{display:block;width:100%;height:330px}.history-chart-wrap .empty{position:absolute;inset:0;display:grid;place-items:center;margin:0}.history-legend{display:flex;flex-wrap:wrap;gap:8px 16px;margin-top:11px;color:var(--muted);font-size:.72rem}.history-legend span:before{content:"";display:inline-block;width:18px;height:3px;margin:0 6px 2px 0;border-radius:3px;background:var(--ember)}.history-legend .target:before{background:var(--gold)}.history-legend .probe-series:before{background:var(--sage)}.history-legend .heater-series:before{background:#8d70c9}.history-legend .marker-series:before{width:7px;height:7px;background:var(--text)}
@keyframes spin{to{transform:rotate(360deg)}}
@media(max-width:900px){.notices{grid-template-columns:1fr 1fr}.live-grid,.network-grid,.firmware-grid{grid-template-columns:1fr}.session-card{order:2}.content-grid{grid-template-columns:1fr}.setup-mode #network-panel{order:-1}.temperature-card{min-height:320px}}
@media(max-width:600px){.site-header,main{width:min(100% - 22px,1180px)}.site-header{align-items:flex-start;padding-top:18px}.brand p{display:none}.brand>span{width:44px;height:44px}.brand h1{font-size:1.75rem}.header-actions{display:grid;justify-items:end}.theme-control{padding-left:0;border-left:0}.notices{grid-template-columns:1fr}.notice{min-height:0}.temperature-card{grid-template-columns:1fr 126px;gap:12px;padding:20px 16px}.heater-ring{width:126px}.heater-ring:before{width:102px}.heater-ring strong{font-size:1.55rem}.session-facts{grid-template-columns:1fr 1fr}.session-facts div:last-child{grid-column:1/-1}.card,.network{padding:16px;border-radius:19px}.button-row,.inline-form{grid-template-columns:1fr}.probe{grid-template-columns:1fr auto}.probe-controls{grid-template-columns:1fr 1fr}.probe-controls label{grid-column:1/-1}.probe-controls button{width:100%}.subheading{display:grid}.subheading button{width:100%}.device-strip{grid-template-columns:1fr}.section-heading h2{font-size:1.8rem}.history-toolbar{grid-template-columns:1fr}.history-chart-wrap,.history-chart-wrap canvas{min-height:280px;height:280px}}
)CSS";

inline constexpr std::string_view app_js = R"JS('use strict';

const $ = selector => document.querySelector(selector);
const $$ = selector => document.querySelectorAll(selector);
const probeDrafts = new Map();
const pendingCommands = new Map();
let latestNetworks = [];
let latestScan = {state: 'idle', networks: [], truncated: false};
let networkTouched = false;
let selectedNetworkSecurity = null;
let scanPoll = null;
let activeTargetPending = null;
let activeTargetDraftVersion = 0;
let maximumTemperature = 150;
let latestSessionStatus = 'IDLE';
let historySessions = [];
let historyObservations = [];
let selectedHistoryId = '';
let historyLoadToken = 0;
const historyPointBudget = 1200;

const statusLabels = {
    IDLE: 'În așteptare', RUNNING: 'În desfășurare',
    STOPPED: 'Oprită', FAULT: 'Fault'
};
const stopLabels = {
    NONE: '—', USER: 'Utilizator', TIMER_COMPLETED: 'Timer finalizat',
    FAULT: 'Fault', RECOVERY_NOT_ALLOWED: 'Recuperare refuzată'
};
const alarmLabels = {
    PROBE_TARGET_REACHED: 'Ținta sondei a fost atinsă',
    PROBE_DISCONNECTED: 'Sondă deconectată',
    TIMER_COMPLETED: 'Timer finalizat'
};
const wifiErrorLabels = {
    network_not_found: 'rețeaua nu a fost găsită',
    signal_too_weak: 'semnalul este prea slab',
    authentication_failed: 'parola a fost respinsă',
    authentication_or_security_failed: 'autentificarea sau tipul de securitate a eșuat',
    handshake_timeout: 'negocierea securității a expirat',
    association_failed: 'asocierea Wi‑Fi a eșuat',
    connection_lost: 'conexiunea s-a pierdut',
    configuration_changed: 'configurația Wi‑Fi s-a schimbat'
};
const firmwareStateLabels = {
    IDLE: 'Pregătit pentru verificare', CHECKING: 'Verificare HTTPS în curs',
    UP_TO_DATE: 'Firmware la zi', AVAILABLE: 'Actualizare disponibilă',
    WAITING_PERMISSION: 'Așteaptă permisiunea controlerului',
    INSTALLING: 'Descărcare și verificare în curs', REBOOTING: 'Controlerul repornește',
    VALIDATING: 'Validează pornirea sigură', FAILED: 'Actualizarea a eșuat'
};

function message(text) {
    $('#message').textContent = text;
    clearTimeout(message.timer);
    message.timer = setTimeout(() => { $('#message').textContent = ''; }, 6000);
}

function escapeHtml(value) {
    return String(value).replace(/[&<>'"]/g, character => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'
    })[character]);
}

function label(value, labels) {
    return labels[value] || String(value).replaceAll('_', ' ');
}

function sameTarget(left, right) {
    return left === null ? right === null
        : right !== null && Math.abs(left - right) < 0.001;
}

function temperatureOrNull(selector) {
    const input = $(selector);
    const raw = input.value.trim();
    if (raw === '') return null;
    const value = Number(raw);
    if (!Number.isFinite(value) || value < 0 || value > maximumTemperature) {
        input.focus();
        throw new Error(`Temperatura trebuie să fie între 0 și ${maximumTemperature} °C.`);
    }
    return value;
}

async function request(path, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 7000);
    const headers = {...(options.headers || {})};
    if (options.body) headers['Content-Type'] = 'application/json';
    try {
        const response = await fetch(path, {
            cache: 'no-store', credentials: 'same-origin',
            ...options, headers, signal: controller.signal
        });
        let data = {};
        try { data = await response.json(); } catch {}
        if (response.status === 401) {
            location.assign('/login');
            throw new Error('Sesiunea a expirat.');
        }
        if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
        return data;
    } catch (error) {
        if (error.name === 'AbortError') throw new Error('Cererea către dispozitiv a expirat.');
        throw error;
    } finally {
        clearTimeout(timeout);
    }
}

function processCommandResults(results = []) {
    for (const result of results) {
        const pending = pendingCommands.get(result.id);
        if (!pending) continue;
        clearTimeout(pending.timeout);
        pendingCommands.delete(result.id);
        if (result.semantic_accepted) {
            message('Comanda a fost aplicată de controler.');
            pending.resolve(true);
        } else {
            message('Controlerul a respins comanda în starea curentă.');
            pending.resolve(false);
        }
    }
}

async function command(body) {
    try {
        const admission = await request('/api/v1/commands', {
            method: 'POST', body: JSON.stringify(body)
        });
        if (!Number.isInteger(admission.command_id)) {
            throw new Error('Răspunsul comenzii nu conține un identificator valid.');
        }
        message('Comanda a fost admisă; aștept confirmarea controlerului.');
        return await new Promise(resolve => {
            const timeout = setTimeout(() => {
                pendingCommands.delete(admission.command_id);
                message('Comanda a fost admisă, dar confirmarea controlerului a expirat.');
                resolve(false);
            }, 20000);
            pendingCommands.set(admission.command_id, {resolve, timeout});
        });
    } catch (error) {
        message(error.message);
        return false;
    }
}

function probeMarkup(probe) {
    return `<div class="probe" data-probe-row="${probe.id}">
        <div class="probe-info"><span class="probe-no"></span><div><b></b><span></span></div></div>
        <strong class="probe-reading"></strong>
        <div class="probe-controls"><label>Țintă sondă<span class="input-shell">
        <input data-probe-target="${probe.id}" type="number" inputmode="decimal" min="0" step="0.5"><b>°C</b>
        </span></label><button data-probe-apply="${probe.id}">Aplică</button>
        <button data-probe-enabled="${probe.id}"></button><button data-probe-alarm="${probe.id}"></button></div>
        </div>`;
}

function renderProbes(probes) {
    const container = $('#probes');
    const present = new Set(probes.map(probe => String(probe.id)));
    container.querySelectorAll('[data-probe-row]').forEach(row => {
        if (!present.has(row.dataset.probeRow)) row.remove();
    });
    const empty = container.querySelector('.empty');
    if (empty) empty.remove();
    if (!probes.length) {
        container.innerHTML = '<p class="empty">Nicio sondă în snapshot.</p>';
        return;
    }

    for (const probe of probes) {
        const draft = probeDrafts.get(probe.id);
        if (draft && draft.pending && sameTarget(probe.target_celsius, draft.target)) {
            probeDrafts.delete(probe.id);
        }
        let row = container.querySelector(`[data-probe-row="${probe.id}"]`);
        if (!row) {
            container.insertAdjacentHTML('beforeend', probeMarkup(probe));
            row = container.querySelector(`[data-probe-row="${probe.id}"]`);
        }
        row.querySelector('.probe-no').textContent = probe.id;
        row.querySelector('.probe-info b').textContent = probe.name;
        row.querySelector('.probe-info div span').textContent =
            `${label(probe.role, {MEAT: 'Aliment', AMBIENT_MONITOR: 'Ambient', UNASSIGNED: 'Neatribuit'})} · ${probe.enabled ? 'activă' : 'dezactivată'}`;
        row.querySelector('.probe-reading').textContent = probe.current_celsius === null
            ? '—' : `${probe.current_celsius.toFixed(1)} °C`;

        const input = row.querySelector('[data-probe-target]');
        input.max = maximumTemperature;
        const currentDraft = probeDrafts.get(probe.id);
        if (document.activeElement !== input && !currentDraft) {
            input.value = probe.target_celsius ?? '';
        }
        input.oninput = () => probeDrafts.set(probe.id, {
            value: input.value, pending: false, target: null
        });
        row.querySelector('[data-probe-apply]').onclick = async () => {
            try {
                const target = temperatureOrNull(`[data-probe-target="${probe.id}"]`);
                const pendingDraft = {value: input.value, pending: true, target};
                probeDrafts.set(probe.id, pendingDraft);
                if (!await command({type: 'set_probe_target', probe_id: probe.id, target_celsius: target})) {
                    if (probeDrafts.get(probe.id) === pendingDraft) {
                        probeDrafts.delete(probe.id);
                    }
                }
            } catch (error) { message(error.message); }
        };
        const enabled = row.querySelector('[data-probe-enabled]');
        enabled.textContent = probe.enabled ? 'Dezactivează' : 'Activează';
        enabled.onclick = () => command({
            type: 'set_probe_enabled', probe_id: probe.id, enabled: !probe.enabled
        });
        const alarm = row.querySelector('[data-probe-alarm]');
        alarm.textContent = `Alarmă ${probe.alarm_enabled ? 'ON' : 'OFF'}`;
        alarm.onclick = () => command({
            type: 'set_probe_alarm_enabled', probe_id: probe.id,
            enabled: !probe.alarm_enabled
        });
    }
}

function renderSnapshot(snapshot) {
    processCommandResults(snapshot.command_results);
    const reportedLimit = Number(snapshot.limits.maximum_chamber_celsius);
    if (Number.isFinite(reportedLimit) && reportedLimit > 0) {
        maximumTemperature = reportedLimit;
    }
    for (const selector of ['#start-target', '#active-target']) $(selector).max = maximumTemperature;
    const status = snapshot.session.status;
    latestSessionStatus = status;
    document.body.dataset.session = status;
    $('#session-status').textContent = statusLabels[status] || status;
    $('#session-pill span').textContent = status;
    $('#session-pill').className = `status-pill ${status === 'FAULT' ? 'fault' : status === 'RUNNING' ? '' : 'neutral'}`;
    $('#chamber').innerHTML = snapshot.chamber.current_celsius === null
        ? '—<small>°C</small>'
        : `${snapshot.chamber.current_celsius.toFixed(1)}<small>°C</small>`;
    $('#chamber-target').textContent = snapshot.chamber.target_celsius === null
        ? 'Monitorizare · fără țintă'
        : `Țintă ${snapshot.chamber.target_celsius.toFixed(1)} °C`;
    const heater = Math.max(0, Math.min(100, snapshot.heater.demand_percent));
    $('#heater').textContent = `${heater.toFixed(0)}%`;
    $('#heater-ring').style.setProperty('--heater', `${heater}%`);
    $('#timer').textContent = snapshot.timer.started
        ? `${Math.floor(snapshot.timer.elapsed_ms / 1000)} s${snapshot.timer.completed ? ' · finalizat' : ''}`
        : 'Inactiv';
    $('#stop-reason').textContent = stopLabels[snapshot.session.stop_reason]
        || snapshot.session.stop_reason;

    const activeTarget = $('#active-target');
    if (activeTargetPending
        && sameTarget(snapshot.chamber.target_celsius, activeTargetPending.target)) {
        if (activeTargetPending.draftVersion === activeTargetDraftVersion) {
            activeTarget.dataset.dirty = 'false';
        }
        activeTargetPending = null;
    }
    if (document.activeElement !== activeTarget && activeTarget.dataset.dirty !== 'true') {
        activeTarget.value = snapshot.chamber.target_celsius ?? '';
    }
    $('#start').disabled = status === 'RUNNING' || status === 'FAULT'
        || snapshot.firmware_update_active === true;
    $('#stop').disabled = status !== 'RUNNING';
    $('#probe-count').textContent = snapshot.probes.length;
    renderProbes(snapshot.probes);

    $('#alarms').innerHTML = snapshot.alarms.length
        ? snapshot.alarms.map(alarm => `<div class="alarm"><span>${escapeHtml(alarmLabels[alarm.code] || alarm.code)}${alarm.probe_id === null ? '' : ` · sonda ${alarm.probe_id}`}${alarm.acknowledged ? ' · confirmată' : ''}</span><button data-ack="${alarm.id}" ${alarm.acknowledged ? 'disabled' : ''}>Confirmă</button></div>`).join('')
        : '<p class="empty">Nicio alarmă activă.</p>';
    $$('[data-ack]').forEach(button => {
        button.onclick = () => command({
            type: 'acknowledge_alarm', alarm_id: Number(button.dataset.ack)
        });
    });
    $('#fault').className = `fault-box${snapshot.fault ? '' : ' ok'}`;
    $('#fault').innerHTML = snapshot.fault
        ? `<div class="fault-row"><span>FAULT: ${escapeHtml(snapshot.fault.code)} · latched</span><button id="clear-fault">Șterge fault rezolvat</button></div>`
        : 'Niciun fault activ.';
    const clear = $('#clear-fault');
    if (clear) clear.onclick = () => command({type: 'clear_resolved_fault'});
}

async function refreshSnapshot() {
    try {
        renderSnapshot(await request('/api/v1/snapshot'));
        $('#connection').innerHTML = '<i></i> API conectat';
        $('#connection').className = 'status-pill';
    } catch {
        $('#connection').innerHTML = '<i></i> API indisponibil';
        $('#connection').className = 'status-pill offline';
    }
}

function signalBars(rssi) {
    return rssi >= -50 ? 4 : rssi >= -62 ? 3 : rssi >= -74 ? 2 : 1;
}

function renderNetworks(data) {
    latestScan = data;
    latestNetworks = data.networks || [];
    const scanning = data.state === 'scanning';
    $('#scan-again').disabled = scanning;
    $('#scan-summary').textContent = scanning
        ? 'Scanare 2,4 GHz în curs…'
        : data.state === 'complete'
            ? `${latestNetworks.length} rețele${data.truncated ? ' · lista este limitată la 20' : ''}`
            : data.state === 'error'
                ? 'Scanarea nu a reușit. Introdu SSID-ul manual.'
                : 'Scanarea nu a pornit încă.';
    $('#scan-error').classList.toggle('hidden', data.state !== 'error');
    $('#scan-error').textContent = data.state === 'error'
        ? 'Rețelele nu au putut fi citite. Configurarea manuală rămâne disponibilă.' : '';
    $('#networks').innerHTML = latestNetworks.length
        ? latestNetworks.map((network, index) => {
            const bars = signalBars(network.rssi_dbm);
            const supported = network.supported === true;
            return `<button class="network-option${$('#ssid').value === network.ssid ? ' selected' : ''}${supported ? '' : ' unsupported'}" data-network="${index}" type="button" ${supported ? '' : 'disabled'}><span><b>${escapeHtml(network.ssid)}</b><small>${escapeHtml(network.security)}${supported ? '' : ' · nesuportată'} · canal ${network.channel} · ${network.rssi_dbm} dBm</small></span><span class="signal" aria-label="Semnal ${bars} din 4">${[1, 2, 3, 4].map(value => `<i class="${value <= bars ? 'on' : ''}"></i>`).join('')}</span></button>`;
        }).join('')
        : `<div class="scan-placeholder">${scanning ? '<i></i>' : ''}<span>${scanning ? 'ESP32 caută rețele de 2,4 GHz…' : 'Nicio rețea vizibilă. Folosește SSID manual.'}</span></div>`;
    $$('[data-network]').forEach(button => {
        button.onclick = () => {
            const network = latestNetworks[Number(button.dataset.network)];
            $('#ssid').value = network.ssid;
            selectedNetworkSecurity = network.security;
            networkTouched = true;
            renderNetworks(data);
            $('#wifi-password').focus();
        };
    });
}

async function pollScan() {
    clearTimeout(scanPoll);
    try {
        const data = await request('/api/v1/network/scan');
        renderNetworks(data);
        if (data.state === 'scanning') scanPoll = setTimeout(pollScan, 800);
    } catch (error) {
        renderNetworks({state: 'error', networks: [], truncated: false});
        $('#scan-error').textContent = error.message;
    }
}

async function startScan() {
    clearTimeout(scanPoll);
    renderNetworks({state: 'scanning', networks: latestNetworks, truncated: false});
    try {
        await request('/api/v1/network/scan', {method: 'POST'});
        await pollScan();
    } catch (error) {
        renderNetworks({state: 'error', networks: [], truncated: false});
        $('#scan-error').textContent = error.message;
    }
}

async function refreshNetwork() {
    try {
        const network = await request('/api/v1/network');
        $('#default-password').classList.toggle('hidden', !network.default_password_warning);
        $('#network-pill span').textContent = network.sta.connected
            ? 'STA conectat' : 'STA neconectat';
        $('#network-pill').className = `status-pill ${network.sta.connected ? '' : 'offline'}`;
        $('#network-title').textContent = 'Conectare Wi‑Fi';
        $('#network-intro').textContent = 'Poți actualiza rețeaua locală fără a opri controlul smokerului.';
        const lastError = network.sta.last_error
            ? ` · ${wifiErrorLabels[network.sta.last_error] || network.sta.last_error}` : '';
        $('#sta-state').textContent = network.sta.connected
            ? `${network.sta.ssid} · ${network.sta.ip}`
            : network.sta.configured ? `${network.sta.ssid} · se reconectează${lastError}`
                : 'Neconfigurat';
        $('#ap-state').textContent = network.ap.active
            ? `${network.ap.ssid} · setup-only` : 'Oprit';
        $('#hostname').textContent = `${network.hostname}.local`;
        if (!networkTouched && !$('#ssid').value) $('#ssid').value = network.sta.ssid || '';
    } catch (error) { message(error.message); }
}

function renderFirmware(firmware) {
    $('#firmware-current').textContent = firmware.current_version || '—';
    $('#firmware-available').textContent = firmware.available_version || '—';
    $('#firmware-pill span').textContent = firmware.state;
    $('#firmware-pill').className = `status-pill ${firmware.state === 'FAILED' ? 'fault' : ['AVAILABLE', 'INSTALLING', 'REBOOTING', 'VALIDATING'].includes(firmware.state) ? '' : 'neutral'}`;
    const progress = Math.max(0, Math.min(100, Number(firmware.progress_percent) || 0));
    $('#firmware-progress').style.width = `${progress}%`;
    $('#firmware-progress').parentElement.setAttribute('aria-valuenow', String(progress));
    $('#firmware-state').textContent = firmwareStateLabels[firmware.state] || firmware.state;
    $('#firmware-error').classList.toggle('hidden', !firmware.error);
    $('#firmware-error').textContent = firmware.error || '';
    const busy = ['CHECKING', 'WAITING_PERMISSION', 'INSTALLING', 'REBOOTING', 'VALIDATING'].includes(firmware.state);
    $('#firmware-check').disabled = busy;
    $('#firmware-install').disabled = !firmware.installation_allowed;
    $('#firmware-install').textContent = firmware.available_version
        ? `Instalează ${firmware.available_version}` : 'Instalează';
}

async function refreshFirmware() {
    try {
        renderFirmware(await request('/api/v1/firmware'));
    } catch (error) {
        $('#firmware-state').textContent = 'Dispozitivul este indisponibil; după reboot, reconectarea poate dura.';
        $('#firmware-check').disabled = true;
        $('#firmware-install').disabled = true;
    }
}

function formatRelative(milliseconds) {
    const seconds = Math.max(0, Math.floor(Number(milliseconds || 0) / 1000));
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return hours ? `${hours} h ${minutes} min` : `${minutes} min`;
}

function formatUtc(seconds) {
    return Number.isFinite(seconds)
        ? new Date(seconds * 1000).toLocaleString('ro-RO', {dateStyle: 'short', timeStyle: 'short'})
        : null;
}

function historySessionLabel(session) {
    const utc = formatUtc(session.start_utc);
    const state = session.active ? 'activă' : session.interrupted ? 'întreruptă' : 'încheiată';
    return `#${session.history_id} · ${state} · ${utc || formatRelative(session.elapsed_ms)}`;
}

function renderHistorySessions(data) {
    historySessions = data.sessions || [];
    $('#history-pill span').textContent = data.status || 'FAILED';
    $('#history-pill').className = `status-pill ${data.status === 'FAILED' ? 'fault' : data.status === 'DEGRADED' ? 'offline' : 'neutral'}`;
    const select = $('#history-session');
    const retained = historySessions.some(session => session.history_id === selectedHistoryId);
    if (!retained) selectedHistoryId = historySessions[0]?.history_id || '';
    select.replaceChildren();
    if (!historySessions.length) {
        select.add(new Option('Nicio sesiune', ''));
    } else {
        for (const session of historySessions) {
            select.add(new Option(historySessionLabel(session), session.history_id));
        }
    }
    select.value = selectedHistoryId;
    const selected = historySessions.find(session => session.history_id === selectedHistoryId);
    const usage = data.capacity_bytes
        ? `${(data.used_bytes / 1048576).toFixed(2)} din ${(data.capacity_bytes / 1048576).toFixed(0)} MiB`
        : 'capacitate indisponibilă';
    $('#history-summary').textContent = selected
        ? `${selected.sample_count} mostre periodice · ${formatRelative(selected.elapsed_ms)} · ${usage}`
        : `Nicio sesiune înregistrată · ${usage}`;
    const warnings = [];
    if (data.status === 'DEGRADED') warnings.push('Stocarea istoricului este degradată; controlul local nu este afectat.');
    if (data.status === 'FAILED') warnings.push('Istoricul nu este disponibil; controlul local continuă independent.');
    if (selected?.interrupted) warnings.push('Sesiunea nu are înregistrare END și este afișată ca întreruptă.');
    if (selected?.truncated) warnings.push('Începutul sesiunii a fost evacuat; sunt păstrate cele mai noi pagini.');
    $('#history-warning').textContent = warnings.join(' ');
    $('#history-warning').classList.toggle('hidden', !warnings.length);
}

function drawHistoryChart() {
    const canvas = $('#history-chart');
    const empty = $('#history-empty');
    const observations = historyObservations;
    empty.classList.toggle('hidden', observations.length > 0);
    const bounds = canvas.getBoundingClientRect();
    const width = Math.max(300, Math.floor(bounds.width || 900));
    const height = Math.max(240, Math.floor(bounds.height || 330));
    const scale = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.floor(width * scale);
    canvas.height = Math.floor(height * scale);
    const context = canvas.getContext('2d');
    context.setTransform(scale, 0, 0, scale, 0, 0);
    context.clearRect(0, 0, width, height);
    if (!observations.length) return;
    const styles = getComputedStyle(document.documentElement);
    const colors = {
        grid: styles.getPropertyValue('--line2').trim(), text: styles.getPropertyValue('--muted').trim(),
        chamber: styles.getPropertyValue('--ember').trim(), target: styles.getPropertyValue('--gold').trim(),
        probe: styles.getPropertyValue('--sage').trim(), heater: '#8d70c9', marker: styles.getPropertyValue('--text').trim()
    };
    const padding = {left: 42, right: 38, top: 24, bottom: 34};
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const lastElapsed = Math.max(1, ...observations.map(item => Number(item.elapsed_ms) || 0));
    const temperatures = [];
    for (const item of observations) {
        if (Number.isFinite(item.chamber_celsius)) temperatures.push(item.chamber_celsius);
        if (Number.isFinite(item.target_celsius)) temperatures.push(item.target_celsius);
        for (const probe of item.probes || []) if (Number.isFinite(probe.current_celsius)) temperatures.push(probe.current_celsius);
    }
    const maximum = Math.max(10, ...temperatures) * 1.08;
    const x = item => padding.left + (Number(item.elapsed_ms) || 0) / lastElapsed * plotWidth;
    const yTemperature = value => padding.top + plotHeight - value / maximum * plotHeight;
    const yHeater = value => padding.top + plotHeight - value / 100 * plotHeight;
    context.font = '11px Arial';
    context.fillStyle = colors.text;
    context.strokeStyle = colors.grid;
    context.lineWidth = 1;
    for (let step = 0; step <= 4; step += 1) {
        const y = padding.top + plotHeight * step / 4;
        context.beginPath(); context.moveTo(padding.left, y); context.lineTo(width - padding.right, y); context.stroke();
        context.fillText(`${Math.round(maximum * (4 - step) / 4)}°`, 5, y + 4);
    }
    const line = (values, color, mapper) => {
        context.beginPath(); context.strokeStyle = color; context.lineWidth = 2; let drawing = false;
        for (const [item, value] of values) {
            if (!Number.isFinite(value)) { drawing = false; continue; }
            const pointX = x(item); const pointY = mapper(value);
            if (!drawing) context.moveTo(pointX, pointY); else context.lineTo(pointX, pointY);
            drawing = true;
        }
        context.stroke();
    };
    line(observations.map(item => [item, item.chamber_celsius]), colors.chamber, yTemperature);
    line(observations.map(item => [item, item.target_celsius]), colors.target, yTemperature);
    const probeIds = [...new Set(observations.flatMap(item => (item.probes || []).map(probe => probe.id)))];
    probeIds.forEach((probeId, index) => line(observations.map(item => {
        const probe = (item.probes || []).find(value => value.id === probeId);
        return [item, probe?.current_celsius];
    }), index % 2 ? '#4f8f82' : colors.probe, yTemperature));
    line(observations.map(item => [item, item.heater_percent]), colors.heater, yHeater);
    for (const item of observations) {
        const marker = item.kind === 'END' || item.fault || (item.alarms || []).length
            || item.timer?.completed || item.kind === 'CHANGE';
        if (!marker) continue;
        context.strokeStyle = item.fault ? colors.chamber : colors.marker;
        context.globalAlpha = .35;
        context.beginPath(); context.moveTo(x(item), padding.top); context.lineTo(x(item), padding.top + plotHeight); context.stroke();
        context.globalAlpha = 1;
    }
    context.fillStyle = colors.text;
    context.fillText('0 min', padding.left, height - 10);
    const endLabel = formatRelative(lastElapsed);
    context.fillText(endLabel, width - padding.right - context.measureText(endLabel).width, height - 10);
    context.fillText('heater %', width - padding.right - 42, padding.top - 8);
}

async function loadHistorySamples() {
    if (!selectedHistoryId) {
        historyObservations = [];
        drawHistoryChart();
        return;
    }
    const requestedHistoryId = selectedHistoryId;
    const loadToken = ++historyLoadToken;
    $('#history-session').disabled = true;
    try {
        const summary = historySessions.find(session => session.history_id === requestedHistoryId);
        const stride = Math.max(1, Math.min(65535, Math.ceil((summary?.sample_count || 0) / 1100)));
        const collected = [];
        let omittedSamples = 0;
        let omittedChanges = 0;
        const retainObservation = observation => {
            if (collected.length < historyPointBudget) {
                collected.push(observation);
                return;
            }
            if (observation.kind === 'SAMPLE') {
                omittedSamples++;
                return;
            }
            let replace = collected.findIndex(item => item.kind === 'SAMPLE');
            if (replace < 0) replace = collected.findIndex(item => item.kind === 'CHANGE');
            if (replace < 0) {
                if (observation.kind === 'CHANGE') omittedChanges++;
                return;
            }
            if (collected[replace].kind === 'SAMPLE') omittedSamples++;
            else omittedChanges++;
            collected.splice(replace, 1);
            collected.push(observation);
        };
        let after = null;
        do {
            const query = new URLSearchParams({history_id: requestedHistoryId, limit: '60', stride: String(stride)});
            if (after !== null) query.set('after', String(after));
            const page = await request(`/api/v1/history/samples?${query}`);
            for (const observation of page.observations || []) retainObservation(observation);
            after = page.continuation;
        } while (after !== null);
        if (loadToken !== historyLoadToken || requestedHistoryId !== selectedHistoryId) return;
        historyObservations = collected;
        drawHistoryChart();
        if (omittedSamples || omittedChanges) {
            const warning = $('#history-warning');
            const budget = omittedChanges
                ? `Graficul păstrează START/END și cele mai recente evenimente; ${omittedChanges} schimbări vechi și ${omittedSamples} mostre periodice au fost omise din limita de ${historyPointBudget} puncte.`
                : `Graficul păstrează toate evenimentele; ${omittedSamples} mostre periodice au fost omise din limita de ${historyPointBudget} puncte.`;
            warning.textContent = [warning.textContent, budget].filter(Boolean).join(' ');
            warning.classList.remove('hidden');
        }
    } catch (error) {
        if (loadToken !== historyLoadToken) return;
        $('#history-warning').textContent = error.message;
        $('#history-warning').classList.remove('hidden');
    } finally {
        if (loadToken === historyLoadToken) {
            $('#history-session').disabled = false;
        }
    }
}

async function refreshHistory() {
    try {
        const previous = selectedHistoryId;
        const previouslySelected = historySessions.find(item => item.history_id === previous);
        const data = await request('/api/v1/history/sessions?limit=32');
        renderHistorySessions(data);
        const currentlySelected = historySessions.find(item => item.history_id === selectedHistoryId);
        const terminalRecordArrived = previouslySelected?.active && currentlySelected && !currentlySelected.active;
        if (selectedHistoryId !== previous || currentlySelected?.active || terminalRecordArrived) {
            await loadHistorySamples();
        }
    } catch (error) {
        $('#history-pill span').textContent = 'INDISPONIBIL';
        $('#history-pill').className = 'status-pill fault';
        $('#history-warning').textContent = error.message;
        $('#history-warning').classList.remove('hidden');
    }
}

async function pollForever(task, delay) {
    await task();
    setTimeout(() => { void pollForever(task, delay); }, delay);
}

function applyTheme(value) {
    if (value === 'system') {
        document.documentElement.removeAttribute('data-theme');
        localStorage.removeItem('fumuri-theme');
    } else {
        document.documentElement.dataset.theme = value;
        localStorage.setItem('fumuri-theme', value);
    }
    $('#theme').value = value;
}

const savedTheme = localStorage.getItem('fumuri-theme');
applyTheme(savedTheme === 'dark' || savedTheme === 'light' ? savedTheme : 'system');
$('#theme').onchange = event => applyTheme(event.target.value);
$('#history-session').onchange = async event => {
    selectedHistoryId = event.target.value;
    renderHistorySessions({status: $('#history-pill span').textContent, sessions: historySessions});
    await loadHistorySamples();
};
window.addEventListener('resize', () => {
    clearTimeout(drawHistoryChart.resizeTimer);
    drawHistoryChart.resizeTimer = setTimeout(drawHistoryChart, 120);
});
$('#ssid').addEventListener('input', () => {
    networkTouched = true;
    selectedNetworkSecurity = null;
    renderNetworks(latestScan);
});
$('#active-target').addEventListener('input', event => {
    activeTargetDraftVersion += 1;
    event.target.dataset.dirty = 'true';
});
$('#start').onclick = async () => {
    try {
        await command({type: 'start_session', target_celsius: temperatureOrNull('#start-target')});
    } catch (error) { message(error.message); }
};
$('#stop').onclick = () => command({type: 'stop_session'});
$('#chamber-form').onsubmit = async event => {
    event.preventDefault();
    try {
        const target = temperatureOrNull('#active-target');
        const pendingTarget = {target, draftVersion: activeTargetDraftVersion};
        activeTargetPending = pendingTarget;
        if (!await command({type: 'set_chamber_target', target_celsius: target})) {
            if (activeTargetPending === pendingTarget) {
                activeTargetPending = null;
                if (pendingTarget.draftVersion === activeTargetDraftVersion) {
                    $('#active-target').dataset.dirty = 'false';
                }
            }
        }
    } catch (error) { message(error.message); }
};
$('#scan-again').onclick = startScan;
$('#firmware-check').onclick = async () => {
    try {
        await request('/api/v1/firmware/check', {method: 'POST'});
        message('Verificarea firmware a pornit. Controlul local continuă normal.');
        await refreshFirmware();
    } catch (error) { message(error.message); }
};
$('#firmware-install').onclick = async () => {
    if (latestSessionStatus === 'RUNNING') {
        return message('Oprește sesiunea înainte de instalarea firmware-ului.');
    }
    const version = $('#firmware-available').textContent;
    if (!/^\d+\.\d+\.\d+$/.test(version)) return message('Nu există o versiune instalabilă.');
    try {
        await request('/api/v1/firmware/install', {
            method: 'POST', body: JSON.stringify({version})
        });
        $('#start').disabled = true;
        message('Instalarea a fost admisă. Nu întrerupe alimentarea; controlerul va reporni.');
        await refreshFirmware();
    } catch (error) { message(error.message); }
};
$('#network-form').onsubmit = async event => {
    event.preventDefault();
    const ssid = $('#ssid').value;
    const wifiPassword = $('#wifi-password').value;
    if (!ssid || ssid.length > 32) return message('SSID-ul trebuie să aibă 1–32 caractere.');
    if (wifiPassword.length < 8 || wifiPassword.length > 63) {
        return message('Parola Wi‑Fi trebuie să aibă 8–63 caractere.');
    }
    try {
        await request('/api/v1/network', {
            method: 'PUT', body: JSON.stringify({ssid, wifi_password: wifiPassword})
        });
        message('Configurația a fost salvată. Reconectarea poate întrerupe temporar pagina.');
        $('#wifi-password').value = '';
        setTimeout(refreshNetwork, 1500);
    } catch (error) { message(error.message); }
};
$('#logout').onclick = async () => {
    try {
        await request('/api/v1/auth/session', {method: 'DELETE'});
        location.assign('/login');
    } catch (error) { message(error.message); }
};
$('#password-form').onsubmit = async event => {
    event.preventDefault();
    const currentPassword = $('#current-password').value;
    const newPassword = $('#new-password').value;
    if (newPassword.length < 8 || newPassword.length > 63) {
        return message('Parola nouă trebuie să aibă 8–63 caractere.');
    }
    try {
        await request('/api/v1/auth/password', {
            method: 'PUT',
            body: JSON.stringify({current_password: currentPassword, new_password: newPassword})
        });
        location.assign('/login');
    } catch (error) { message(error.message); }
};

void pollForever(refreshSnapshot, 1000);
void pollForever(refreshNetwork, 5000);
void pollForever(refreshFirmware, 1500);
void pollForever(refreshHistory, 15000);
void startScan();
)JS";

} // namespace smoker::platform::web_assets
