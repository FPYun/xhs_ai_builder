#!/usr/bin/env python3
"""Local USB serial to AI bridge for the M5 pet demo.

CoreS3 stays offline. The computer reads serial state, serves a local page,
and optionally calls user-configured model providers.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import mimetypes
import re
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from html import escape as html_escape
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, unquote, urlparse

try:
    import serial
    import serial.tools.list_ports
except Exception:  # pragma: no cover - exercised on machines without pyserial
    serial = None


DEFAULT_HOST = "127.0.0.1"
DEFAULT_HTTP_PORT = 8788
DEFAULT_BAUD = 115200
M5_BRIDGE_ROOT = Path.home() / ".m5_ai_bridge"
AI_RECORDS_PATH = M5_BRIDGE_ROOT / "ai_records.jsonl"
SECRETS_PATH = M5_BRIDGE_ROOT / "secrets.json"
ASSETS_ROOT = M5_BRIDGE_ROOT / "pets"
HISTORY_LIMIT = 30
WORKSHOP_VERSION = 1
MIMO_TEXT_BASE_URL = "https://api.xiaomimimo.com/anthropic"
MIMO_TTS_ENDPOINT = "https://api.xiaomimimo.com/v1/chat/completions"
MIMO_TTS_MODEL = "mimo-v2.5-tts"
VALID_ACTIONS = {
    "photo",
    "bag",
    "match",
    "idle",
    "prev",
    "next",
    "select",
    "capture",
    "release",
    "confirm_release",
    "cancel",
    "friend",
    "mute",
    "unmute",
    "toggle_mute",
}


INDEX_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 Pet Companion</title>
<style>
:root{color-scheme:light dark;--bg:#f5f7fb;--surface:#fff;--soft:#eef3f8;--text:#172033;--muted:#66738a;--line:#dbe3ee;--primary:#2563eb;--primary-ink:#fff;--ok:#138a52;--warn:#b45309;--danger:#c2410c;--shadow:0 12px 34px rgba(15,23,42,.08)}
@media(prefers-color-scheme:dark){:root{--bg:#0f141b;--surface:#171f29;--soft:#111923;--text:#edf4fb;--muted:#9aa8ba;--line:#2a3644;--primary:#76a9ff;--primary-ink:#07111f;--ok:#4ade80;--warn:#fbbf24;--danger:#fb923c;--shadow:none}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}button,input,select{font:inherit}
.shell{max-width:1180px;margin:0 auto;padding:22px}.topbar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}.brand h1{margin:0;font-size:24px;letter-spacing:0}.brand p{margin:5px 0 0;color:var(--muted);font-size:14px}.top-actions{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.badge{display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line);border-radius:999px;padding:8px 12px;background:var(--surface);font-weight:800}.dot{width:9px;height:9px;border-radius:99px;background:var(--warn)}.dot.on{background:var(--ok)}
.hero{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(320px,.75fr);gap:16px;align-items:stretch;margin-bottom:16px}.panel,.card{background:var(--surface);border:1px solid var(--line);border-radius:12px;box-shadow:var(--shadow)}.panel{padding:22px}.card{padding:16px}.eyebrow{color:var(--muted);font-size:13px;font-weight:800;margin-bottom:10px}.pet-name{font-size:34px;line-height:1.12;font-weight:900;margin-bottom:14px}.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(112px,1fr));gap:10px}.stat{background:var(--soft);border:1px solid var(--line);border-radius:10px;padding:12px}.stat span{display:block;color:var(--muted);font-size:12px}.stat b{display:block;font-size:17px;margin-top:5px;overflow-wrap:anywhere}.quick,.actions{display:flex;gap:9px;flex-wrap:wrap}.quick{margin-top:18px}
button{appearance:none;border:0;border-radius:9px;background:var(--primary);color:var(--primary-ink);font-weight:850;padding:10px 14px;min-height:42px;cursor:pointer}button.secondary{background:transparent;color:var(--primary);border:1px solid var(--line)}button.subtle{background:var(--soft);color:var(--text);border:1px solid var(--line)}button.dangerbtn{background:var(--danger);color:#fff}button:disabled{opacity:.5;cursor:default}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.section-title{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:0 0 12px}h2{font-size:18px;margin:0}.row{display:flex;gap:12px;justify-content:space-between;margin:8px 0}.label{color:var(--muted)}.value{text-align:right;font-weight:750;overflow-wrap:anywhere}.small{font-size:13px;color:var(--muted);line-height:1.5}.out{border:1px solid var(--line);border-radius:10px;background:var(--soft);padding:12px;min-height:92px}.history-list{display:grid;gap:10px}.history-item{border:1px solid var(--line);border-radius:10px;background:var(--soft);padding:12px}.history-item b{display:block;margin-bottom:4px}.history-item details{margin-top:8px}input,select{width:100%;border:1px solid var(--line);border-radius:9px;background:var(--soft);color:var(--text);padding:10px}.form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}.config-line{margin:10px 0}.advanced{margin-top:16px}.advanced summary{cursor:pointer;font-weight:850}.debug-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}pre{white-space:pre-wrap;overflow:auto;border:1px solid var(--line);border-radius:10px;background:var(--soft);padding:10px;font-size:12px;max-height:260px}
.workshop{display:grid;grid-template-columns:minmax(260px,.82fr) minmax(0,1.18fr);gap:16px}.sprite-frame{display:grid;place-items:center;background:linear-gradient(180deg,var(--soft),transparent);border:1px solid var(--line);border-radius:12px;min-height:300px;overflow:hidden}.sprite-frame img{max-width:92%;max-height:290px}.asset-list{display:grid;gap:8px;margin-top:10px}.asset-link{display:flex;align-items:center;justify-content:space-between;gap:10px;border:1px solid var(--line);border-radius:9px;background:var(--soft);padding:9px 10px;color:var(--text);text-decoration:none}.asset-link span:last-child{color:var(--muted);font-size:12px}.audio-list{display:grid;gap:8px}.audio-item{border:1px solid var(--line);border-radius:10px;background:var(--soft);padding:10px}.audio-item audio{width:100%;margin-top:6px}.pill{display:inline-flex;border:1px solid var(--line);border-radius:999px;padding:5px 9px;background:var(--soft);font-size:12px;color:var(--muted);font-weight:800}
@media(max-width:860px){.shell{padding:14px}.topbar,.hero{display:block}.top-actions{margin-top:12px}.grid,.debug-grid,.workshop{grid-template-columns:1fr}.pet-name{font-size:28px}.panel,.card{padding:14px}.hero{margin-bottom:14px}.hero .card{margin-top:14px}.section-title{display:block}.section-title .actions{margin-top:10px}}
</style>
</head>
<body>
<main class="shell">
<header class="topbar">
<div class="brand"><h1>M5 Pet Companion</h1><p>捕捉、养成、好友互动和宠物周边资产。</p></div>
<div class="top-actions"><div class="badge"><span id="connDot" class="dot"></span><span id="connText">未连接</span></div><button class="secondary" onclick="refreshAll()">刷新</button></div>
</header>

<section class="hero">
<div class="panel">
<div class="eyebrow">当前伙伴</div>
<div id="petHero" class="pet-name">等待连接</div>
<div id="petStats" class="stats"></div>
<div class="quick"><button onclick="sendAction('photo')">拍照捕捉</button><button class="secondary" onclick="sendAction('bag')">背包</button><button class="secondary" onclick="sendAction('match')">对战</button><button class="subtle" onclick="sendAction('friend')">好友动态</button></div>
<div id="actionMsg" class="small config-line"></div>
</div>
<div class="card"><div class="section-title"><h2>好友状态</h2><span id="battlePill" class="pill"></span></div><div id="socialBox" class="small"></div></div>
</section>

<section class="grid">
<div class="card"><div class="section-title"><h2>伙伴详情</h2><button class="secondary" onclick="generateAi('pet')">生成人格卡</button></div><div id="petBox" class="small"></div></div>
<div class="card"><div class="section-title"><h2>设备控制</h2><span class="small">发送到 CoreS3</span></div><div class="actions" id="actions"></div></div>
</section>

<section class="card">
<div class="section-title"><h2>周边工坊</h2><div class="actions"><button onclick="generateWorkshop(['persona','sprite2d','voice','social','merch','modelBrief'])">生成整套资产</button><button class="secondary" onclick="generateWorkshop(['sprite2d'])">重画 2D</button><button class="secondary" onclick="generateWorkshop(['voice'])">生成语音</button></div></div>
<div class="workshop">
<div>
<label class="small">选择宠物<select id="petSelect" onchange="selectWorkshopPet()"></select></label>
<div id="assetStatus" class="small config-line">正在读取背包...</div>
<div class="sprite-frame" id="spriteFrame"><span class="small">暂无 2D 形象</span></div>
<div id="assetLinks" class="asset-list"></div>
</div>
<div>
<div id="workshopMsg" class="small config-line">资产会保存到电脑本地，并按宠物长期保留。</div>
<div id="workshopOutput" class="out small"></div>
<div id="audioBox" class="audio-list config-line"></div>
</div>
</div>
</section>

<section class="card">
<div class="section-title"><h2>AI 创作</h2><div class="actions"><button onclick="generateAi('pet')">宠物人格</button><button class="secondary" onclick="generateAi('social')">好友事件</button></div></div>
<div id="aiMsg" class="small config-line">等待生成</div>
<div id="aiOutput" class="out small">生成的故事、个性和互动建议会显示在这里。</div>
<details class="advanced"><summary>模型设置</summary>
<div class="form-grid config-line">
<label>Provider<select id="provider"><option value="local-template">本地模板</option><option value="openai">GPT / OpenAI</option><option value="deepseek">DeepSeek</option><option value="mimo-compatible">Mimo / Anthropic</option></select></label>
<label>模型<input id="model" placeholder="gpt-4o-mini / deepseek-chat / mimo-v2.5"></label>
<label>接口地址<input id="baseUrl" placeholder="可选；Mimo 可填 https://api.xiaomimimo.com/anthropic"></label>
<label>临时 API Key<input id="apiKey" type="password" placeholder="可留空，优先使用电脑本地密钥"></label>
</div>
<div id="configInfo" class="small">正在读取本地密钥配置...</div>
<div class="actions config-line"><button class="secondary" onclick="useMimoDefaults()">套用 Mimo 配置</button><button class="secondary" onclick="useLocal()">本地模板</button></div>
</details>
</section>

<section class="card">
<div class="section-title"><h2>记忆卡片</h2><button class="secondary" onclick="loadHistory()">刷新记录</button></div>
<div id="historyInfo" class="small config-line"></div>
<div id="historyBox" class="history-list small">暂无记录</div>
</section>

<details class="card advanced">
<summary>高级连接</summary>
<div id="statusBox" class="stats config-line"></div>
<div class="actions"><button class="secondary" onclick="listPorts()">查看端口</button></div>
<div class="debug-grid"><pre id="ports"></pre><pre id="raw"></pre></div>
</details>
</main>
<script>
const $=id=>document.getElementById(id);
const esc=v=>String(v??'-').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
let state={}, pets=[], selectedPet=null;
const actionNames=['photo','bag','match','idle','prev','next','select','capture','release','confirm_release','cancel','friend','mute','unmute'];
const actionLabels={photo:'拍照',bag:'背包',match:'对战',idle:'主页',prev:'上一只',next:'下一只',select:'选择',capture:'捕捉',release:'放生',confirm_release:'确认放生',cancel:'取消',friend:'好友',mute:'静音',unmute:'开声'};
function row(k,v){return `<div class="row"><span class="label">${esc(k)}</span><span class="value">${esc(v)}</span></div>`}
function metric(k,v){return `<div class="stat"><span>${esc(k)}</span><b>${esc(v)}</b></div>`}
function loadPrefs(){['provider','model','baseUrl','apiKey'].forEach(id=>{const v=localStorage.getItem('aiBridge_'+id); if(v!==null)$(id).value=v})}
function savePrefs(){['provider','model','baseUrl','apiKey'].forEach(id=>localStorage.setItem('aiBridge_'+id,$(id).value.trim()))}
function render(){
  const s=state.status||{}, pet=s.pet||null, bag=s.bag||null, friend=s.friend||null, battle=s.battle||null;
  $('connDot').className='dot'+(state.connected?' on':'');
  $('connText').textContent=state.connected?'已连接':'未连接';
  $('petHero').textContent=pet?(pet.species||'未命名伙伴'):'暂无当前伙伴';
  $('petStats').innerHTML=[
    ['等级',pet?pet.level:'-'],['阶段',pet?pet.stage:'-'],['XP',pet?`${pet.xp}/${pet.nextXp}`:'-'],['背包',bag?`${bag.count}/${bag.capacity}`:'-']
  ].map(([k,v])=>metric(k,v)).join('');
  $('statusBox').innerHTML=[
    ['连接',state.connected?'已连接':'未连接'],['端口',state.port||'-'],['画面',s.screenLabel||s.screen||'-'],['更新',state.updatedAt||'-']
  ].map(([k,v])=>metric(k,v)).join('');
  $('petBox').innerHTML=pet?row('名称',pet.species)+row('等级',pet.level)+row('阶段',pet.stage)+row('XP',`${pet.xp}/${pet.nextXp}`)+row('成长目标',pet.growthGoal)+row('战绩',`${pet.wins}/${pet.battles}`):'暂无当前伙伴。';
  $('battlePill').textContent=battle?`${battle.last||'-'} · diff ${battle.diff}`:'';
  $('socialBox').innerHTML=(friend?row('最近对手',friend.recent)+row('关系',`${friend.label} ${friend.score}/100`)+row('连战',friend.streak)+row('下次奖励',`+${friend.nextXp} XP`):'暂无好友动态。')+(battle?row('最近对战',`${battle.last} · 奖励 ${battle.xp} XP`):'');
  $('raw').textContent=(state.rawLines||[]).join('\n')||'暂无原始状态';
  $('actions').innerHTML=actionNames.map(a=>`<button class="${['release','confirm_release'].includes(a)?'dangerbtn':'subtle'}" onclick="sendAction('${a}')">${actionLabels[a]||a}</button>`).join('');
}
async function refreshAll(){const r=await fetch('/api/status');state=await r.json();render();await loadPets()}
async function listPorts(){const r=await fetch('/api/ports');$('ports').textContent=JSON.stringify(await r.json(),null,2)}
async function sendAction(type){$('actionMsg').textContent='正在发送...';const r=await fetch('/api/action?type='+encodeURIComponent(type),{method:'POST'});const j=await r.json();$('actionMsg').textContent=j.ok?'已发送：'+(actionLabels[type]||type):'失败：'+(j.error||'设备无响应');await refreshAll()}
function snapshot(kind){const s=state.status||{};if(kind==='pet')return {source:'serial-status',pet:s.pet||{},context:{screen:s.screenLabel||s.screen,bag:s.bag||{}}};return {source:'serial-status',relationship:s.friend||{},battle:s.battle||{},pet:s.pet||{}}}
function providerPayload(){savePrefs();return {provider:$('provider').value,model:$('model').value.trim(),baseUrl:$('baseUrl').value.trim(),apiKey:$('apiKey').value.trim()}}
function useMimoDefaults(){$('provider').value='mimo-compatible';$('model').value='mimo-v2.5';$('baseUrl').value='https://api.xiaomimimo.com/anthropic';savePrefs();$('aiMsg').textContent='已套用 Mimo 配置。'}
function renderAiResult(result){return Object.keys(result||{}).map(k=>row(k,result[k])).join('')+`<details><summary>JSON</summary><pre>${esc(JSON.stringify(result||{},null,2))}</pre></details>`}
async function generateAi(kind){const body={kind,...providerPayload(),snapshot:snapshot(kind)};$('aiMsg').textContent='生成中...';const r=await fetch('/api/ai',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const j=await r.json();$('aiMsg').textContent=(kind==='pet'?'宠物人格':'好友事件')+(j.fallback?' · 本地/回退':' · 已生成')+(j.error?' · '+j.error:'')+(j.savedPath?' · 已保存':'');$('aiOutput').innerHTML=renderAiResult(j.result);await loadHistory()}
async function loadPets(){const r=await fetch('/api/pets');const j=await r.json();pets=j.pets||[];$('petSelect').innerHTML=pets.length?pets.map(p=>`<option value="${esc(p.petId)}">${esc((p.active?'★ ':'')+(p.species||'伙伴')+' #'+(Number(p.index)+1))}</option>`).join(''):'<option value="">暂无宠物</option>';if(selectedPet&&pets.some(p=>p.petId===selectedPet.petId))$('petSelect').value=selectedPet.petId;selectWorkshopPet()}
function selectWorkshopPet(){const id=$('petSelect').value;selectedPet=pets.find(p=>p.petId===id)||pets[0]||null;if(selectedPet)$('petSelect').value=selectedPet.petId;renderAssets(selectedPet)}
function renderAssets(pet){if(!pet){$('assetStatus').textContent='背包为空或未连接设备。';$('spriteFrame').innerHTML='<span class="small">暂无 2D 形象</span>';$('assetLinks').innerHTML='';$('workshopOutput').innerHTML='';$('audioBox').innerHTML='';return}
  const a=pet.assets||{};$('assetStatus').textContent=`${pet.species||'伙伴'} · ${pet.element||'-'} · 资产 ${a.hasManifest?'已生成':'未生成'}`;
  $('spriteFrame').innerHTML=a.spriteUrl?`<img src="${a.spriteUrl}?t=${Date.now()}" alt="宠物 2D 形象">`:'<span class="small">暂无 2D 形象</span>';
  const links=a.files||[];$('assetLinks').innerHTML=links.map(f=>`<a class="asset-link" href="${f.url}" target="_blank"><span>${esc(f.name)}</span><span>${esc(f.kind)}</span></a>`).join('');
  const m=a.manifest||{};$('workshopOutput').innerHTML=m.petName?renderAiResult({petName:m.petName, generatedAt:m.generatedAt, assetVersion:m.assetVersion, fallback:m.fallback?'是':'否'}):'为这只宠物生成后，会在这里看到资产摘要。';
  const aud=a.audio||[];$('audioBox').innerHTML=aud.length?aud.map(item=>`<div class="audio-item"><b>${esc(item.label)}</b><div class="small">${esc(item.text)}</div><audio controls src="${item.url}"></audio></div>`).join(''):'';
}
async function generateWorkshop(targets){if(!selectedPet){$('workshopMsg').textContent='没有可生成的宠物。';return}
  $('workshopMsg').textContent='周边资产生成中...';const body={...providerPayload(),petId:selectedPet.petId,index:selectedPet.index,pet:selectedPet,targets};
  const r=await fetch('/api/workshop/generate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const j=await r.json();
  $('workshopMsg').textContent=(j.ok?'已生成':'生成失败')+(j.fallback?' · 本地/回退':'')+(j.error?' · '+j.error:'')+(j.audioError?' · 语音：'+j.audioError:'');
  $('workshopOutput').innerHTML=renderAiResult(j.summary||j.bundle||j);await loadPets()
}
async function loadHistory(){const r=await fetch('/api/history');const j=await r.json();$('historyInfo').textContent='保存位置：'+(j.path||'-');const records=j.records||[];$('historyBox').innerHTML=records.length?records.map(rec=>{const res=rec.result||{};const title=res.petName||res.eventTitle||rec.kind;return `<div class="history-item"><b>${esc(title)}</b><div>${esc(rec.time)} · ${esc(rec.kind)} · ${esc(rec.provider)}${rec.fallback?' · 本地/回退':''}</div><details><summary>查看内容</summary>${renderAiResult(res)}</details></div>`}).join(''):'暂无记录'}
async function loadLocalConfig(){const r=await fetch('/api/local-config');const j=await r.json();const p=j.providers||{};$('configInfo').textContent='本地密钥：Mimo '+(p['mimo-compatible']?'已配置':'未配置')}
function useLocal(){$('provider').value='local-template';generateAi('pet')}
loadPrefs();refreshAll();loadHistory();loadLocalConfig();setInterval(refreshAll,2500);
</script>
</body>
</html>"""


@dataclass
class BridgeState:
    connected: bool = False
    port: str | None = None
    status: dict[str, Any] = field(default_factory=dict)
    raw_lines: list[str] = field(default_factory=list)
    updated_at: str | None = None
    error: str | None = None


def now_label() -> str:
    return time.strftime("%H:%M:%S")


def list_serial_ports() -> list[dict[str, str]]:
    if serial is None:
        return []
    return [
        {"device": item.device, "description": item.description, "hwid": item.hwid}
        for item in serial.tools.list_ports.comports()
    ]


def choose_port(preferred: str | None = None) -> str | None:
    ports = list_serial_ports()
    if preferred:
        return preferred
    for item in ports:
        if "303A:1001" in item.get("hwid", ""):
            return item["device"]
    return ports[0]["device"] if ports else None


def parse_status_lines(lines: list[str]) -> dict[str, Any]:
    status: dict[str, Any] = {}
    for line in lines:
        if line.startswith("ui "):
            match = re.search(
                r"screen=(?P<screen>\S+) label=(?P<label>.*?) buttons=(?P<buttons>.*?) bag=(?P<count>\d+)/(?P<capacity>\d+) battle=(?P<phase>\S+) (?P<phase_label>.*)$",
                line,
            )
            if match:
                status["screen"] = match.group("screen")
                status["screenLabel"] = match.group("label")
                status["buttons"] = match.group("buttons")
                status["bag"] = {
                    "count": int(match.group("count")),
                    "capacity": int(match.group("capacity")),
                }
                status["battlePhase"] = match.group("phase")
                status["battlePhaseLabel"] = match.group("phase_label")
            continue
        if line.startswith("pet active=none"):
            match = re.search(r"bag=(?P<count>\d+)/(?P<capacity>\d+)", line)
            status["pet"] = None
            if match:
                status["bag"] = {
                    "count": int(match.group("count")),
                    "capacity": int(match.group("capacity")),
                }
            continue
        if line.startswith("pet active="):
            match = re.search(
                r"active=(?P<active>\d+)/(?P<total>\d+) species=(?P<species>.*?) level=(?P<level>\d+) stage=(?P<stage>\S+) xp=(?P<xp>\d+)/(?P<next>\d+) goal=(?P<goal>.*?) stats=(?P<power>\d+)/(?P<agility>\d+)/(?P<spirit>\d+) wins=(?P<wins>\d+)/(?P<battles>\d+)",
                line,
            )
            if match:
                status["pet"] = {
                    "index": int(match.group("active")) - 1,
                    "active": True,
                    "activeIndex": int(match.group("active")),
                    "bagCount": int(match.group("total")),
                    "species": match.group("species"),
                    "level": int(match.group("level")),
                    "stage": match.group("stage"),
                    "xp": int(match.group("xp")),
                    "nextXp": int(match.group("next")),
                    "growthGoal": match.group("goal"),
                    "power": int(match.group("power")),
                    "agility": int(match.group("agility")),
                    "spirit": int(match.group("spirit")),
                    "wins": int(match.group("wins")),
                    "battles": int(match.group("battles")),
                }
            continue
        if line.startswith("friend recent=none"):
            status["friend"] = None
            continue
        if line.startswith("friend recent="):
            match = re.search(
                r"recent=(?P<recent>\S+) label=(?P<label>.*?) score=(?P<score>\d+)/100 battles=(?P<battles>\d+) streak=(?P<streak>\d+) next_xp=(?P<next>\d+) notice=(?P<notice>.*)",
                line,
            )
            if match:
                status["friend"] = {
                    "recent": match.group("recent"),
                    "label": match.group("label"),
                    "score": int(match.group("score")),
                    "battles": int(match.group("battles")),
                    "streak": int(match.group("streak")),
                    "nextXp": int(match.group("next")),
                    "notice": match.group("notice"),
                }
            continue
        if line.startswith("battle last=none"):
            status["battle"] = None
            continue
        if line.startswith("battle last="):
            match = re.search(
                r"last=(?P<last>\S+) id=(?P<id>\S+) diff=(?P<diff>-?\d+) xp=(?P<xp>\d+) friend_bonus=(?P<bonus>\d+)",
                line,
            )
            if match:
                status["battle"] = {
                    "last": match.group("last"),
                    "id": match.group("id"),
                    "diff": int(match.group("diff")),
                    "xp": int(match.group("xp")),
                    "friendBonus": int(match.group("bonus")),
                }
    return status


def parse_bagstatus_lines(lines: list[str]) -> dict[str, Any] | None:
    for line in lines:
        text = line.strip()
        if text.startswith("bagstatus "):
            text = text[len("bagstatus ") :].strip()
        if not text.startswith("{"):
            continue
        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            continue
        if isinstance(parsed, dict) and "pets" in parsed:
            return parsed
    return None


def save_ai_record(record: dict[str, Any], path: Path = AI_RECORDS_PATH) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, ensure_ascii=False) + "\n")
    return str(path)


def load_ai_records(limit: int = HISTORY_LIMIT, path: Path = AI_RECORDS_PATH) -> dict[str, Any]:
    if not path.exists():
        return {"path": str(path), "records": []}
    records: list[dict[str, Any]] = []
    for line in reversed(path.read_text(encoding="utf-8").splitlines()):
        if not line.strip():
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
        if len(records) >= limit:
            break
    return {"path": str(path), "records": records}


def load_local_secrets(path: Path = SECRETS_PATH) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def local_api_key_for(provider: str, path: Path = SECRETS_PATH) -> str:
    providers = load_local_secrets(path).get("providers") or {}
    if not isinstance(providers, dict):
        return ""
    entry = providers.get(provider)
    if entry is None and provider == "mimo-compatible":
        entry = providers.get("mimo")
    if isinstance(entry, str):
        return entry.strip()
    if isinstance(entry, dict):
        return str(entry.get("apiKey") or entry.get("api_key") or "").strip()
    return ""


def local_config_status(path: Path = SECRETS_PATH) -> dict[str, Any]:
    return {
        "path": str(path),
        "providers": {
            "mimo-compatible": bool(local_api_key_for("mimo-compatible", path)),
            "openai": bool(local_api_key_for("openai", path)),
            "deepseek": bool(local_api_key_for("deepseek", path)),
        },
    }


def finalize_ai_response(
    *,
    kind: str,
    provider: str,
    model: str,
    snapshot: dict[str, Any],
    fallback: bool,
    result: dict[str, Any],
    error: str | None = None,
    persist: bool = True,
) -> dict[str, Any]:
    response: dict[str, Any] = {"provider": provider, "fallback": fallback, "result": result}
    if error:
        response["error"] = error
    if not persist:
        return response
    record = {
        "time": time.strftime("%Y-%m-%d %H:%M:%S"),
        "kind": kind,
        "provider": provider,
        "model": model,
        "fallback": fallback,
        "error": error,
        "snapshot": snapshot,
        "result": result,
    }
    try:
        response["savedPath"] = save_ai_record(record)
    except OSError as exc:
        response["saveError"] = str(exc)
    return response


def local_ai(kind: str, snapshot: dict[str, Any]) -> dict[str, str]:
    if kind == "social":
        rel = snapshot.get("relationship") or {}
        battle = snapshot.get("battle") or {}
        score = rel.get("score", "-")
        streak = rel.get("streak", "-")
        recent = rel.get("recent", "未知对手")
        battle_last = battle.get("last", "未记录")
        return {
            "eventTitle": "桥接站台的再会",
            "eventText": f"最近对手 {recent} 与当前伙伴完成了 {battle_last} 对战，友情 {score}/100，连战 {streak} 次。它们已经开始记住彼此的节奏。",
            "relationshipLabel": str(rel.get("label") or "等待好友"),
            "nextActionHint": "下一次可以安排轻量复赛，把结果写成新的羁绊动态。",
            "rematchFlavorText": "它们没有说破胜负，只把这次交锋当成下次见面前的暗号。",
            "memoryTag": f"recent:{recent}; score:{score}; streak:{streak}",
            "relationshipMoment": "适合展示成一张好友动态卡：对手、气氛、下次约战目标。",
            "sharedGoal": "积累羁绊，解锁更稳定的连战奖励和更有个性的赛后对话。",
        }
    pet = snapshot.get("pet") or {}
    species = pet.get("species") or "新伙伴"
    level = pet.get("level", "-")
    stage = pet.get("stage", "-")
    goal = pet.get("growthGoal", "-")
    return {
        "petName": str(species),
        "personality": f"等级 {level}、阶段 {stage} 的稳健型伙伴。它会先观察环境，再决定靠元素优势还是耐心周旋。",
        "catchStory": "它像从训练日常里被发现的小主角：没有宏大身世，只有一次拍照、一段识别结果和一个愿意继续培养它的玩家。",
        "evolutionHint": f"成长目标是“{goal}”。下一阶段叙事可以围绕一次挑战、一次好友复赛或一次新的捕捉样本展开。",
        "idleLine": "今天先把背包整理好，下一次拍照也许就是新的线索。",
        "battleLine": "别急着赢，先看清对面的节奏。",
        "flavorText": "CoreS3 保持离线运行，电脑端只负责把已有事实写成更有生命感的卡片。",
        "likes": "喜欢被反复查看详情，也喜欢在背包里排到醒目的位置。",
        "quirk": "遇到熟悉对手时会格外专注，像在等待一次默契的再战。",
        "growthArc": "从被拍照发现，到形成战斗风格，再到拥有固定好友关系。",
        "dailyRitual": "每天一次拍照、一次背包巡检、一次轻量对战，就是它的训练日常。",
        "signatureMove": "按当前元素和物种自动生成招式名，不使用任何已有动漫或游戏专有招式。",
    }


def mimo_messages_endpoint(base_url: str) -> str:
    url = base_url.rstrip("/")
    if url.endswith("/anthropic"):
        return f"{url}/v1/messages"
    return url


def provider_endpoint(provider: str, base_url: str) -> str:
    if provider == "mimo-compatible":
        return mimo_messages_endpoint(base_url or MIMO_TEXT_BASE_URL)
    if base_url:
        return base_url
    if provider == "openai":
        return "https://api.openai.com/v1/chat/completions"
    if provider == "deepseek":
        return "https://api.deepseek.com/chat/completions"
    return ""


def provider_model(provider: str, model: str) -> str:
    if model:
        return model
    if provider == "openai":
        return "gpt-4o-mini"
    if provider == "deepseek":
        return "deepseek-chat"
    if provider == "mimo-compatible":
        return "mimo-v2.5"
    return ""


def ai_prompt(kind: str, snapshot: dict[str, Any]) -> str:
    if kind == "social":
        fields = (
            "eventTitle, eventText, relationshipLabel, nextActionHint, rematchFlavorText, "
            "memoryTag, relationshipMoment, sharedGoal"
        )
        role = "五行宠物机的好友社交事件生成器"
    else:
        fields = (
            "petName, personality, catchStory, evolutionHint, idleLine, battleLine, flavorText, "
            "likes, quirk, growthArc, dailyRitual, signatureMove"
        )
        role = "五行宠物机的宠物人格生成器"
    return (
        f"你是{role}。只根据输入事实生成中文展示文案，不改变等级、战绩、友情分或对战结果。"
        "可以借鉴收集养成游戏、伙伴冒险动画、竞技对战和校园社团日常的叙事结构，但不得使用真实作品的角色名、地名、组织名、招式名或原台词。"
        "文案面向玩家，不提 API、模型、HOST、CLIENT、UDP、串口等底层细节。每个字段控制在一到两句。"
        f"严格输出 JSON，字段为：{fields}。\n输入：\n"
        + json.dumps(snapshot, ensure_ascii=False, indent=2)
    )


def extract_ai_text(response: Any) -> str:
    if isinstance(response, str):
        return response
    if not isinstance(response, dict):
        return json.dumps(response, ensure_ascii=False)
    for key in ("output_text", "text"):
        if response.get(key):
            return str(response[key])
    result = response.get("result")
    if result is not None:
        return result if isinstance(result, str) else json.dumps(result, ensure_ascii=False)
    choices = response.get("choices") or []
    if choices:
        content = ((choices[0] or {}).get("message") or {}).get("content")
        if content:
            return str(content)
    content = response.get("content") or []
    if isinstance(content, list):
        texts = [str(item.get("text")) for item in content if isinstance(item, dict) and item.get("text")]
        if texts:
            return "\n".join(texts)
    return json.dumps(response, ensure_ascii=False)


def parse_ai_json(text: str) -> dict[str, Any]:
    cleaned = text.strip()
    if cleaned.startswith("```"):
        cleaned = re.sub(r"^```(?:json)?\s*", "", cleaned, flags=re.IGNORECASE)
        cleaned = re.sub(r"\s*```$", "", cleaned)
    try:
        parsed = json.loads(cleaned)
        return parsed if isinstance(parsed, dict) else {"text": parsed}
    except json.JSONDecodeError:
        start = cleaned.find("{")
        end = cleaned.rfind("}")
        if start >= 0 and end > start:
            parsed = json.loads(cleaned[start : end + 1])
            return parsed if isinstance(parsed, dict) else {"text": parsed}
        raise


def request_model_json(
    *,
    provider: str,
    model: str,
    base_url: str,
    api_key: str,
    prompt: str,
    timeout: float = 30.0,
    max_tokens: int = 1200,
) -> dict[str, Any]:
    endpoint = provider_endpoint(provider, base_url)
    if provider == "mimo-compatible":
        body = {
            "model": model,
            "max_tokens": max_tokens,
            "system": "只输出有效 JSON，不要输出 Markdown。",
            "messages": [{"role": "user", "content": [{"type": "text", "text": prompt}]}],
            "stream": False,
            "temperature": 0.8,
            "thinking": {"type": "disabled"},
        }
        headers = {"Content-Type": "application/json", "api-key": api_key}
    else:
        body = {
            "model": model,
            "messages": [
                {"role": "system", "content": "只输出有效 JSON，不要输出 Markdown。"},
                {"role": "user", "content": prompt},
            ],
            "temperature": 0.8,
            "response_format": {"type": "json_object"},
        }
        headers = {"Content-Type": "application/json", "Authorization": f"Bearer {api_key}"}
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(body).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        raw = response.read().decode("utf-8", errors="replace")
    return parse_ai_json(extract_ai_text(json.loads(raw)))


def call_model(payload: dict[str, Any], timeout: float = 30.0, persist: bool = True) -> dict[str, Any]:
    kind = payload.get("kind") or "pet"
    provider = payload.get("provider") or "local-template"
    snapshot = payload.get("snapshot") or {}
    model = provider_model(provider, (payload.get("model") or "").strip())
    if provider == "local-template":
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=True,
            result=local_ai(kind, snapshot),
            persist=persist,
        )

    base_url = (payload.get("baseUrl") or "").strip()
    api_key = (payload.get("apiKey") or "").strip() or local_api_key_for(provider)
    if not provider_endpoint(provider, base_url):
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=True,
            error="请填写兼容接口地址",
            result=local_ai(kind, snapshot),
            persist=persist,
        )
    if not model:
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=True,
            error="请填写模型名",
            result=local_ai(kind, snapshot),
            persist=persist,
        )
    if not api_key:
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=True,
            error="请填写 API Key，或使用本地模板",
            result=local_ai(kind, snapshot),
            persist=persist,
        )
    try:
        result = request_model_json(
            provider=provider,
            model=model,
            base_url=base_url,
            api_key=api_key,
            prompt=ai_prompt(kind, snapshot),
            timeout=timeout,
            max_tokens=900,
        )
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=False,
            result=result,
            persist=persist,
        )
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError) as exc:
        return finalize_ai_response(
            kind=kind,
            provider=provider,
            model=model,
            snapshot=snapshot,
            fallback=True,
            error=str(exc),
            result=local_ai(kind, snapshot),
            persist=persist,
        )


def rgb565_to_hex(value: Any) -> str | None:
    try:
        raw = int(value)
    except (TypeError, ValueError):
        return None
    r = ((raw >> 11) & 0x1F) * 255 // 31
    g = ((raw >> 5) & 0x3F) * 255 // 63
    b = (raw & 0x1F) * 255 // 31
    return f"#{r:02x}{g:02x}{b:02x}"


def pet_color(pet: dict[str, Any]) -> str:
    genes = pet.get("genes") if isinstance(pet.get("genes"), dict) else {}
    color = rgb565_to_hex(genes.get("accentColor") or pet.get("accentColor"))
    if color:
        return color
    colors = ["#ef4444", "#3b82f6", "#22c55e", "#f59e0b", "#a855f7", "#64748b"]
    try:
        return colors[int(pet.get("elementIndex", 5)) % len(colors)]
    except (TypeError, ValueError):
        return colors[-1]


def stable_pet_id(pet: dict[str, Any]) -> str:
    genes = pet.get("genes") if isinstance(pet.get("genes"), dict) else {}
    parts = [
        str(pet.get("index", "")),
        str(pet.get("elementIndex", "")),
        str(pet.get("speciesIndex", "")),
        str(pet.get("visualVariantIndex", "")),
        str(genes.get("seed") or pet.get("seed") or ""),
        str(pet.get("capturedAtSec", "")),
        str(pet.get("species") or ""),
    ]
    digest = hashlib.sha1("|".join(parts).encode("utf-8")).hexdigest()[:10]
    return f"pet-{pet.get('index', 0)}-{digest}"


def safe_pet_id(value: str) -> str:
    clean = re.sub(r"[^A-Za-z0-9_.-]", "-", value).strip(".-")
    return clean or "pet-unknown"


def pet_asset_dir(pet_id: str, root: Path = ASSETS_ROOT) -> Path:
    return root / safe_pet_id(pet_id)


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def read_json_file(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def workshop_defaults(pet: dict[str, Any]) -> dict[str, Any]:
    name = str(pet.get("species") or "未命名伙伴")
    element = str(pet.get("element") or "未知元素")
    level = pet.get("level", "-")
    return {
        "persona": {
            "petName": name,
            "personality": f"{element} 系伙伴，性格稳定，喜欢在训练前观察对手。",
            "catchStory": "它从一次拍照识别中被发现，逐渐把玩家的日常训练当成自己的冒险地图。",
            "evolutionHint": f"当前等级 {level}，下一段成长适合围绕复赛、背包整理和新样本展开。",
            "idleLine": "我准备好了，下一次拍照会有新发现。",
            "battleLine": "看清节奏，再决定出手。",
            "flavorText": "一只保留设备离线精神、又拥有专属故事的口袋伙伴。",
        },
        "spriteSpec": {
            "bodyShape": "rounded",
            "mark": "star",
            "mood": "focused",
            "accessory": "badge",
            "palette": "element-accent",
        },
        "voiceLines": [
            {"key": "intro", "label": "登场", "text": "我准备好了，今天从一次小小的训练开始。"},
            {"key": "battle", "label": "对战", "text": "别急着赢，先看清对面的节奏。"},
            {"key": "friend", "label": "好友", "text": "再来一次吧，这次我们会更有默契。"},
        ],
        "socialEvent": {
            "eventTitle": "训练后的约定",
            "eventText": "它记住了最近的对手，也把下一次复赛当成新的目标。",
            "relationshipLabel": "新的伙伴",
            "nextActionHint": "安排一次轻量对战，看看羁绊是否继续提升。",
            "rematchFlavorText": "它没有急着庆祝，只把胜负写进下一次见面的暗号里。",
        },
        "merchPack": {
            "sticker": "圆角贴纸，主体为宠物正面头像和元素色边框。",
            "badge": "徽章使用元素色底纹，保留名字和等级。",
            "card": "收藏卡正面放 2D 形象，背面放人格和战绩摘要。",
            "standee": "亚克力立牌采用半身姿态，底座写成长目标。",
            "desktopPet": "桌宠版本保留待机台词和好友复赛提示。",
        },
        "modelBrief": {
            "style": "原创电子宠物，圆润低多边形比例，适合后续建模。",
            "shape": "大头、小身体、元素色光环、清晰眼睛和短尾巴。",
            "materials": "柔和塑料质感，局部半透明元素特效。",
            "prompt": "Create an original cute elemental digital pet based on the saved 2D sprite, rounded toy-like proportions, simple readable silhouette, no existing anime or game IP.",
        },
    }


def workshop_prompt(pet: dict[str, Any], targets: list[str]) -> str:
    return (
        "你是原创电子宠物周边设定师。根据输入宠物事实，生成中文 JSON。"
        "不得复制或引用任何真实动漫、游戏、角色、招式、地名、组织名。"
        "输出字段必须包含 persona, spriteSpec, voiceLines, socialEvent, merchPack, modelBrief。"
        "persona 包含 petName, personality, catchStory, evolutionHint, idleLine, battleLine, flavorText。"
        "spriteSpec 包含 bodyShape, mark, mood, accessory, palette，供本地 SVG 渲染使用。"
        "voiceLines 是 3 条以内数组，每条包含 key, label, text，text 适合语音合成且不超过 40 个汉字。"
        "socialEvent 包含 eventTitle, eventText, relationshipLabel, nextActionHint, rematchFlavorText。"
        "merchPack 包含 sticker, badge, card, standee, desktopPet。"
        "modelBrief 包含 style, shape, materials, prompt。"
        f"本次目标：{', '.join(targets)}。\n宠物事实：\n"
        + json.dumps(pet, ensure_ascii=False, indent=2)
    )


def merge_workshop_bundle(defaults: dict[str, Any], generated: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(generated, dict):
        return defaults
    merged = dict(defaults)
    for key, default_value in defaults.items():
        value = generated.get(key)
        if isinstance(default_value, dict):
            current = dict(default_value)
            if isinstance(value, dict):
                current.update({k: v for k, v in value.items() if v not in (None, "")})
            merged[key] = current
        elif isinstance(default_value, list):
            merged[key] = value if isinstance(value, list) and value else default_value
        else:
            merged[key] = value or default_value
    return merged


def render_sprite_svg(pet: dict[str, Any], bundle: dict[str, Any]) -> str:
    spec = bundle.get("spriteSpec") if isinstance(bundle.get("spriteSpec"), dict) else {}
    persona = bundle.get("persona") if isinstance(bundle.get("persona"), dict) else {}
    name = str(persona.get("petName") or pet.get("species") or "M5 Pet")
    color = pet_color(pet)
    seed = int(hashlib.sha1(stable_pet_id(pet).encode("utf-8")).hexdigest()[:8], 16)
    shape = str(spec.get("bodyShape") or "rounded").lower()
    mark = str(spec.get("mark") or "star").lower()
    accessory = str(spec.get("accessory") or "badge").lower()
    cx, cy = 256, 256
    body = {
        "circle": f"<circle cx='{cx}' cy='{cy}' r='112' fill='{color}'/>",
        "leaf": f"<path d='M256 118 C376 172 374 316 256 398 C138 316 136 172 256 118Z' fill='{color}'/>",
        "diamond": f"<path d='M256 112 L398 256 L256 404 L114 256Z' fill='{color}'/>",
    }.get(shape, f"<rect x='132' y='136' width='248' height='240' rx='86' fill='{color}'/>")
    ears = (
        f"<path d='M184 162 L152 82 L232 142Z' fill='{color}'/>"
        f"<path d='M328 162 L360 82 L280 142Z' fill='{color}'/>"
        if seed % 3 == 0
        else f"<circle cx='174' cy='164' r='48' fill='{color}'/><circle cx='338' cy='164' r='48' fill='{color}'/>"
    )
    tail = (
        f"<path d='M374 294 Q468 286 430 204' fill='none' stroke='{color}' stroke-width='34' stroke-linecap='round'/>"
        if seed % 2
        else f"<path d='M138 306 Q48 336 96 404' fill='none' stroke='{color}' stroke-width='34' stroke-linecap='round'/>"
    )
    if "moon" in mark:
        mark_svg = "<path d='M278 164 A42 42 0 1 1 232 218 A34 34 0 1 0 278 164Z' fill='white' opacity='.78'/>"
    elif "line" in mark:
        mark_svg = "<path d='M198 190 H314' stroke='white' stroke-width='18' stroke-linecap='round' opacity='.76'/>"
    elif "dot" in mark:
        mark_svg = "<circle cx='256' cy='184' r='26' fill='white' opacity='.76'/>"
    else:
        mark_svg = "<path d='M256 144 L274 186 L320 190 L285 219 L296 264 L256 240 L216 264 L227 219 L192 190 L238 186Z' fill='white' opacity='.72'/>"
    badge = (
        f"<circle cx='342' cy='332' r='34' fill='white' opacity='.9'/><text x='342' y='343' text-anchor='middle' font-size='28' font-family='Arial' fill='{color}'>★</text>"
        if accessory
        else ""
    )
    label = html_escape(name[:8])
    return (
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"
        "<rect width='512' height='512' rx='54' fill='#f8fafc'/>"
        f"<circle cx='256' cy='256' r='214' fill='{color}' opacity='.12'/>"
        f"{tail}{ears}{body}{mark_svg}"
        "<circle cx='212' cy='246' r='28' fill='white'/><circle cx='300' cy='246' r='28' fill='white'/>"
        "<circle cx='218' cy='252' r='12' fill='#111827'/><circle cx='294' cy='252' r='12' fill='#111827'/>"
        "<path d='M210 316 Q256 354 302 316' fill='none' stroke='white' stroke-width='22' stroke-linecap='round'/>"
        f"{badge}<text x='256' y='462' text-anchor='middle' font-size='31' font-family='Arial, sans-serif' font-weight='700' fill='{color}'>{label}</text>"
        "</svg>"
    )


def extract_tts_audio(response: dict[str, Any]) -> bytes:
    choices = response.get("choices") or []
    if not choices:
        raise ValueError("TTS response has no choices")
    message = choices[0].get("message") or {}
    audio = message.get("audio") or {}
    data = audio.get("data")
    if not data:
        raise ValueError("TTS response has no audio data")
    return base64.b64decode(data)


def call_mimo_tts(text: str, api_key: str, timeout: float = 45.0) -> bytes:
    body = {
        "model": MIMO_TTS_MODEL,
        "messages": [
            {"role": "user", "content": "亲切、轻快、像电子宠物在和玩家说话。"},
            {"role": "assistant", "content": text[:90]},
        ],
        "audio": {"format": "wav", "voice": "mimo_default"},
    }
    request = urllib.request.Request(
        MIMO_TTS_ENDPOINT,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json", "api-key": api_key},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        raw = response.read().decode("utf-8", errors="replace")
    return extract_tts_audio(json.loads(raw))


def asset_files(pet_id: str, root: Path = ASSETS_ROOT) -> dict[str, Any]:
    base = pet_asset_dir(pet_id, root)
    manifest = read_json_file(base / "manifest.json") if (base / "manifest.json").exists() else None
    files = []
    for name, kind in [
        ("manifest.json", "清单"),
        ("persona.json", "人格"),
        ("social.json", "好友"),
        ("merch_pack.json", "周边"),
        ("model_brief.json", "3D 设定"),
        ("voice_lines.json", "台词"),
        ("sprite.svg", "2D 形象"),
    ]:
        if (base / name).exists():
            files.append({"name": name, "kind": kind, "url": f"/assets/{pet_id}/{name}"})
    audio = []
    audio_dir = base / "audio"
    if audio_dir.exists():
        voice_lines = read_json_file(base / "voice_lines.json") or []
        labels = {str(item.get("key")): item for item in voice_lines if isinstance(item, dict)}
        for path in sorted(audio_dir.glob("*.wav")):
            key = path.stem
            meta = labels.get(key, {})
            audio.append(
                {
                    "label": meta.get("label") or key,
                    "text": meta.get("text") or "",
                    "url": f"/assets/{pet_id}/audio/{path.name}",
                }
            )
    return {
        "hasManifest": manifest is not None,
        "manifest": manifest or {},
        "files": files,
        "audio": audio,
        "spriteUrl": f"/assets/{pet_id}/sprite.svg" if (base / "sprite.svg").exists() else "",
        "root": str(base),
    }


def pet_with_assets(pet: dict[str, Any], root: Path = ASSETS_ROOT) -> dict[str, Any]:
    copy = dict(pet)
    pet_id = str(copy.get("petId") or stable_pet_id(copy))
    copy["petId"] = pet_id
    copy["assets"] = asset_files(pet_id, root)
    return copy


def demo_pet() -> dict[str, Any]:
    return {
        "index": 0,
        "active": True,
        "demo": True,
        "petId": "demo-local-pet",
        "species": "示例伙伴",
        "element": "火",
        "elementIndex": 0,
        "speciesIndex": 0,
        "visualVariantIndex": 0,
        "level": 1,
        "stage": "幼体",
        "xp": 0,
        "nextXp": 20,
        "growthGoal": "完成一次捕捉",
        "wins": 0,
        "battles": 0,
        "genes": {"seed": 20260601, "accentColor": 63488, "bodyScale": 100},
    }


def generate_workshop_assets(payload: dict[str, Any], root: Path = ASSETS_ROOT) -> dict[str, Any]:
    pet = payload.get("pet") if isinstance(payload.get("pet"), dict) else {}
    if not pet:
        return {"ok": False, "error": "missing pet"}
    targets = payload.get("targets") if isinstance(payload.get("targets"), list) else []
    targets = [str(item) for item in targets] or ["persona", "sprite2d", "voice", "social", "merch", "modelBrief"]
    pet_id = safe_pet_id(str(payload.get("petId") or pet.get("petId") or stable_pet_id(pet)))
    pet["petId"] = pet_id
    provider = str(payload.get("provider") or "local-template")
    model = provider_model(provider, str(payload.get("model") or "").strip())
    base_url = str(payload.get("baseUrl") or "").strip()
    api_key = str(payload.get("apiKey") or "").strip() or local_api_key_for(provider)
    defaults = workshop_defaults(pet)
    generated: dict[str, Any] | None = None
    fallback = provider == "local-template"
    error = ""
    if provider != "local-template":
        if not api_key:
            error = "缺少 API Key，已使用本地模板"
            fallback = True
        else:
            try:
                generated = request_model_json(
                    provider=provider,
                    model=model,
                    base_url=base_url,
                    api_key=api_key,
                    prompt=workshop_prompt(pet, targets),
                    timeout=45.0,
                    max_tokens=1600,
                )
            except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError) as exc:
                error = str(exc)
                fallback = True
    bundle = merge_workshop_bundle(defaults, generated)
    asset_dir = pet_asset_dir(pet_id, root)
    asset_dir.mkdir(parents=True, exist_ok=True)

    if any(item in targets for item in ("persona", "sprite2d", "voice", "social", "merch", "modelBrief")):
        write_json(asset_dir / "persona.json", bundle["persona"])
        write_json(asset_dir / "social.json", bundle["socialEvent"])
        write_json(asset_dir / "merch_pack.json", bundle["merchPack"])
        write_json(asset_dir / "model_brief.json", bundle["modelBrief"])
        write_json(asset_dir / "voice_lines.json", bundle["voiceLines"])
    if "sprite2d" in targets or not (asset_dir / "sprite.svg").exists():
        (asset_dir / "sprite.svg").write_text(render_sprite_svg(pet, bundle), encoding="utf-8")

    audio_error = ""
    if "voice" in targets:
        tts_key = str(payload.get("apiKey") or "").strip() or local_api_key_for("mimo-compatible")
        if tts_key:
            audio_dir = asset_dir / "audio"
            audio_dir.mkdir(parents=True, exist_ok=True)
            for item in bundle.get("voiceLines", [])[:3]:
                if not isinstance(item, dict):
                    continue
                key = safe_pet_id(str(item.get("key") or "line"))
                text = str(item.get("text") or "").strip()
                if not text:
                    continue
                try:
                    (audio_dir / f"{key}.wav").write_bytes(call_mimo_tts(text, tts_key))
                except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, ValueError, OSError) as exc:
                    audio_error = str(exc)
                    break
        else:
            audio_error = "缺少 Mimo API Key，已保留文本台词"

    persona = bundle.get("persona", {})
    manifest = {
        "assetVersion": WORKSHOP_VERSION,
        "petId": pet_id,
        "petName": persona.get("petName") or pet.get("species"),
        "source": "m5-ai-bridge",
        "provider": provider,
        "model": model,
        "fallback": fallback,
        "error": error,
        "audioError": audio_error,
        "generatedAt": time.strftime("%Y-%m-%d %H:%M:%S"),
        "pet": {k: v for k, v in pet.items() if k != "assets"},
    }
    write_json(asset_dir / "manifest.json", manifest)
    try:
        save_ai_record(
            {
                "time": manifest["generatedAt"],
                "kind": "workshop",
                "provider": provider,
                "model": model,
                "fallback": fallback,
                "error": error or audio_error,
                "snapshot": {"pet": manifest["pet"], "targets": targets},
                "result": {
                    "petName": manifest["petName"],
                    "assetRoot": str(asset_dir),
                    "sprite": "sprite.svg",
                    "audioError": audio_error,
                },
            }
        )
    except OSError:
        pass
    return {
        "ok": True,
        "petId": pet_id,
        "fallback": fallback,
        "error": error,
        "audioError": audio_error,
        "bundle": bundle,
        "summary": {
            "petName": manifest["petName"],
            "assetRoot": str(asset_dir),
            "sprite": "sprite.svg",
            "audio": "已生成" if asset_files(pet_id, root)["audio"] else "未生成",
            "fallback": fallback,
        },
        "assets": asset_files(pet_id, root),
    }


class SerialBridge:
    def __init__(self, port: str | None, baud: int = DEFAULT_BAUD) -> None:
        self.preferred_port = port
        self.baud = baud
        self.lock = threading.Lock()
        self.serial_obj: Any = None
        self.state = BridgeState(port=port)

    def close(self) -> None:
        with self.lock:
            if self.serial_obj is not None:
                try:
                    self.serial_obj.close()
                finally:
                    self.serial_obj = None

    def connect_locked(self) -> bool:
        if serial is None:
            self.state = BridgeState(connected=False, port=self.preferred_port, error="pyserial is not installed")
            return False
        if self.serial_obj is not None and getattr(self.serial_obj, "is_open", False):
            return True
        port = choose_port(self.preferred_port)
        if not port:
            self.state = BridgeState(connected=False, port=None, error="no serial port found")
            return False
        try:
            self.serial_obj = serial.Serial(port, self.baud, timeout=0.05, write_timeout=0.5)
            self.state.port = port
            self.state.connected = True
            return True
        except Exception as exc:
            self.serial_obj = None
            self.state = BridgeState(connected=False, port=port, error=str(exc))
            return False

    def read_lines_locked(self, seconds: float) -> list[str]:
        lines: list[str] = []
        if self.serial_obj is None:
            return lines
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                raw = self.serial_obj.readline()
            except Exception as exc:
                self.state.connected = False
                self.state.error = str(exc)
                break
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                lines.append(line)
        return lines

    def request_status(self) -> BridgeState:
        with self.lock:
            if not self.connect_locked():
                return self.state
            try:
                self.serial_obj.reset_input_buffer()
                self.serial_obj.write(b"STATUS\n")
                self.serial_obj.flush()
                lines = self.read_lines_locked(1.2)
                status = parse_status_lines(lines)
                self.state.connected = True
                self.state.raw_lines = lines
                self.state.status = status
                self.state.updated_at = now_label()
                self.state.error = None if status else "no STATUS response parsed"
            except Exception as exc:
                self.state.connected = False
                self.state.error = str(exc)
                self.close()
            return self.state

    def request_pets(self) -> dict[str, Any]:
        with self.lock:
            if self.connect_locked():
                try:
                    self.serial_obj.reset_input_buffer()
                    self.serial_obj.write(b"BAGSTATUS\n")
                    self.serial_obj.flush()
                    lines = self.read_lines_locked(1.8)
                    parsed = parse_bagstatus_lines(lines)
                    if parsed is not None:
                        self.state.connected = True
                        self.state.raw_lines = lines
                        self.state.updated_at = now_label()
                        pets = [pet_with_assets(pet) for pet in parsed.get("pets", []) if isinstance(pet, dict)]
                        parsed["pets"] = pets
                        parsed["source"] = "BAGSTATUS"
                        return parsed
                except Exception as exc:
                    self.state.connected = False
                    self.state.error = str(exc)
                    self.close()
        snapshot = self.snapshot()
        pet = snapshot.get("status", {}).get("pet")
        pets = [pet_with_assets(pet)] if isinstance(pet, dict) else []
        if not snapshot.get("connected") and not pets:
            pets = [pet_with_assets(demo_pet())]
        return {
            "ok": bool(pets) and bool(snapshot.get("connected")),
            "count": len(pets),
            "capacity": snapshot.get("status", {}).get("bag", {}).get("capacity", 0),
            "selectedIndex": pet.get("index", 0) if isinstance(pet, dict) else 0,
            "pets": pets,
            "source": "STATUS-fallback" if snapshot.get("connected") else "demo-fallback",
            "error": snapshot.get("error"),
        }

    def send_action(self, action: str) -> dict[str, Any]:
        if action not in VALID_ACTIONS:
            return {"ok": False, "error": "unsupported action"}
        with self.lock:
            if not self.connect_locked():
                return {"ok": False, "error": self.state.error or "serial not connected"}
            try:
                self.serial_obj.write(f"ACT {action}\n".encode("utf-8"))
                self.serial_obj.flush()
                lines = self.read_lines_locked(0.8)
                ok = any("serial ok:" in line for line in lines)
                error_line = next((line for line in lines if "serial error:" in line), "")
                return {
                    "ok": ok and not error_line,
                    "error": error_line.replace("serial error:", "").strip() if error_line else "",
                    "lines": lines,
                }
            except Exception as exc:
                self.state.connected = False
                self.state.error = str(exc)
                self.close()
                return {"ok": False, "error": str(exc)}

    def snapshot(self) -> dict[str, Any]:
        state = self.request_status()
        return {
            "connected": state.connected,
            "port": state.port,
            "status": state.status,
            "rawLines": state.raw_lines,
            "updatedAt": state.updated_at,
            "error": state.error,
        }


def make_handler(bridge: SerialBridge):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: Any) -> None:
            sys.stderr.write("[%s] %s\n" % (now_label(), fmt % args))

        def send_json(self, data: Any, status: int = 200) -> None:
            body = json.dumps(data, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_html(self) -> None:
            body = INDEX_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_file(self, path: Path) -> None:
            if not path.exists() or not path.is_file():
                self.send_json({"error": "not found"}, 404)
                return
            content_type = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
            body = path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def read_json_body(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length") or "0")
            if length <= 0:
                return {}
            raw = self.rfile.read(length).decode("utf-8", errors="replace")
            return json.loads(raw) if raw else {}

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path in ("/", "/index.html"):
                self.send_html()
                return
            if parsed.path == "/api/status":
                self.send_json(bridge.snapshot())
                return
            if parsed.path == "/api/pets":
                self.send_json(bridge.request_pets())
                return
            if parsed.path == "/api/assets":
                pet_id = (parse_qs(parsed.query).get("petId") or [""])[0]
                self.send_json(asset_files(safe_pet_id(pet_id)))
                return
            if parsed.path.startswith("/assets/"):
                relative = unquote(parsed.path[len("/assets/") :])
                parts = relative.split("/", 1)
                if len(parts) != 2:
                    self.send_json({"error": "not found"}, 404)
                    return
                pet_id, file_part = safe_pet_id(parts[0]), parts[1]
                root = pet_asset_dir(pet_id).resolve()
                target = (root / file_part).resolve()
                try:
                    target.relative_to(root)
                except ValueError:
                    self.send_json({"error": "not found"}, 404)
                    return
                self.send_file(target)
                return
            if parsed.path == "/api/ports":
                self.send_json({"ports": list_serial_ports()})
                return
            if parsed.path == "/api/history":
                self.send_json(load_ai_records())
                return
            if parsed.path == "/api/local-config":
                self.send_json(local_config_status())
                return
            self.send_json({"error": "not found"}, 404)

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/api/action":
                action = (parse_qs(parsed.query).get("type") or [""])[0]
                self.send_json(bridge.send_action(action))
                return
            if parsed.path == "/api/ai":
                try:
                    self.send_json(call_model(self.read_json_body()))
                except json.JSONDecodeError as exc:
                    self.send_json({"error": f"invalid JSON: {exc}"}, 400)
                return
            if parsed.path == "/api/workshop/generate":
                try:
                    self.send_json(generate_workshop_assets(self.read_json_body()))
                except json.JSONDecodeError as exc:
                    self.send_json({"ok": False, "error": f"invalid JSON: {exc}"}, 400)
                return
            self.send_json({"error": "not found"}, 404)

    return Handler


def run_self_test() -> None:
    sample = [
        "ui screen=idle label=休闲 buttons=背包/对战/拍照 bag=2/6 battle=finding 寻找训练师",
        "pet active=1/2 species=岩陵兽 level=3 stage=Cub xp=22/40 goal=再战一次 stats=28/20/26 wins=1/2",
        "friend recent=A1B2C3 label=好友 score=64/100 battles=3 streak=2 next_xp=10 notice=ready",
        "battle last=win id=00ABCDEF diff=12 xp=18 friend_bonus=10",
    ]
    parsed = parse_status_lines(sample)
    assert parsed["screen"] == "idle"
    assert parsed["bag"]["count"] == 2
    assert parsed["pet"]["species"] == "岩陵兽"
    assert parsed["friend"]["score"] == 64
    assert parsed["battle"]["diff"] == 12
    bag = parse_bagstatus_lines(
        [
            json.dumps(
                {
                    "ok": True,
                    "count": 1,
                    "capacity": 6,
                    "selectedIndex": 0,
                    "pets": [parsed["pet"] | {"element": "土", "elementIndex": 2, "speciesIndex": 1}],
                },
                ensure_ascii=False,
            )
        ]
    )
    assert bag and bag["count"] == 1
    pet = local_ai("pet", {"pet": parsed["pet"]})
    social = local_ai("social", {"relationship": parsed["friend"], "battle": parsed["battle"]})
    assert "petName" in pet
    assert "growthArc" in pet
    assert "eventTitle" in social
    assert "sharedGoal" in social
    assert parse_ai_json('```json\n{"ok": true}\n```')["ok"] is True
    assert parse_ai_json('result:\n{"ok": true}')["ok"] is True
    assert provider_endpoint("mimo-compatible", "") == "https://api.xiaomimimo.com/anthropic/v1/messages"
    assert provider_model("mimo-compatible", "") == "mimo-v2.5"
    with tempfile.TemporaryDirectory() as temp_dir:
        root = Path(temp_dir) / "pets"
        test_path = Path(temp_dir) / "ai_records.jsonl"
        save_ai_record({"kind": "pet", "result": pet}, test_path)
        history = load_ai_records(path=test_path)
        assert history["records"][0]["result"]["petName"] == "岩陵兽"
        result = generate_workshop_assets(
            {
                "provider": "local-template",
                "pet": parsed["pet"] | {"element": "土", "elementIndex": 2, "speciesIndex": 1},
                "targets": ["persona", "sprite2d", "social", "merch", "modelBrief"],
            },
            root=root,
        )
        assert result["ok"] is True
        asset_dir = pet_asset_dir(result["petId"], root)
        assert (asset_dir / "manifest.json").exists()
        assert (asset_dir / "sprite.svg").exists()
        assert asset_files(result["petId"], root)["spriteUrl"]
        secrets_path = Path(temp_dir) / "secrets.json"
        secrets_path.write_text(
            json.dumps({"providers": {"mimo-compatible": {"apiKey": "secret"}}}),
            encoding="utf-8",
        )
        assert local_api_key_for("mimo-compatible", secrets_path) == "secret"
        assert local_config_status(secrets_path)["providers"]["mimo-compatible"] is True
    print("self-test ok")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="M5 CoreS3 USB serial AI bridge")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--http-port", type=int, default=DEFAULT_HTTP_PORT)
    parser.add_argument("--serial-port", default=None, help="Example: COM7")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()
        return 0

    bridge = SerialBridge(args.serial_port, args.baud)
    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(bridge))
    print(f"M5 AI bridge listening on http://{args.host}:{args.http_port}")
    print("CoreS3 stays offline; the computer calls AI providers.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping bridge...")
    finally:
        bridge.close()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
