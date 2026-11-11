// ============================================================================
// AI chat: prompt, tool loop, rendering, proposals
// ============================================================================

import { chatCompletion, describeProviderError } from './provider.js';
import { buildStatePack } from './context.js';
import { TOOL_DEFINITIONS, runTool, toolSummary } from './tools.js';

const CHAT_STORAGE_KEY = 'sc3.ai.chat';
const PROPOSALS_STORAGE_KEY = 'sc3.ai.proposals';
const MAX_HISTORY_MESSAGES = 14;
const MAX_ROUNDS = 4;

const SYSTEM_RULES = `Ты — технолог-аналитик установки Smart-Column S3 (ректификация, дистилляция, НБК, затирание, hold, ферментация).
Отвечай по-русски, кратко и по делу. Опирайся ТОЛЬКО на данные из контекста и инструментов; если данных нет — прямо скажи, чего не хватает.
Не выдумывай числа и события. Ссылайся на конкретные метрики и фазы.
У тебя нет доступа к управлению: нельзя включать нагрев, насос, клапаны, менять уставки или запускать процесс. Ты только объясняешь, диагностируешь и предлагаешь изменения параметров оператору.
Любые советы не должны противоречить safety-лимитам и логике preflight.
Инструменты: get_status, get_health, list_processes, get_process, compare_runs, get_logs, get_live_chart. Вызывай их, когда нужны факты, и не повторяй один и тот же вызов без причины.
Формат ответа для телефона:
- начни с 1–3 предложений вывода, детали — ниже короткими пунктами;
- не вставляй в текст служебные ключи (phasePowerPercent.body, saturationSec, floodRisk) — они живут в поле evidence;
- секунды переводи в минуты/часы, Unix-время — в дату и время, дроби пиши с запятой (6,4 °C);
- не противоречь себе: если предлагаешь поднять мощность, не пиши рядом, что фазу надо сократить, если это не одно и то же действие.
Если предлагаешь изменить параметры, добавь в самый конец ответа ОДИН fenced-блок json строго такого вида:
{"findings":[{"code":"...","detail":"...","evidence":"..."}],"proposals":[{"parameterKey":"...","previousValue":0,"suggestedValue":0,"expectedEffect":"...","confidence":0.0,"evidence":"..."}]}
Без proposals блок можно не добавлять.`;

const state = {
    config: null,
    history: [],
    busy: false,
    aborter: null,
    elements: null,
    providerLabel: ''
};

function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
}

function escapeHtml(text) {
    return String(text)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// Small markdown subset: enough for headings, lists, bold, inline code and
// fenced blocks. No library, no HTML injection.
function renderMarkdown(raw) {
    const source = String(raw || '');
    const blocks = [];
    let text = source.replace(/```(\w+)?\n([\s\S]*?)```/g, (match, lang, code) => {
        const index = blocks.length;
        blocks.push(`<pre class="ai-code"><code>${escapeHtml(code.trim())}</code></pre>`);
        return `\u0000BLOCK${index}\u0000`;
    });

    text = escapeHtml(text);

    const lines = text.split(/\r?\n/);
    const html = [];
    let listOpen = false;
    const closeList = () => {
        if (listOpen) {
            html.push('</ul>');
            listOpen = false;
        }
    };

    lines.forEach((line) => {
        const trimmed = line.trim();
        if (!trimmed) {
            closeList();
            return;
        }
        const bullet = trimmed.match(/^[•\-*]\s+(.*)$/);
        if (bullet) {
            if (!listOpen) {
                html.push('<ul>');
                listOpen = true;
            }
            html.push(`<li>${inline(bullet[1])}</li>`);
            return;
        }
        const heading = trimmed.match(/^(#{1,4})\s+(.*)$/);
        if (heading) {
            closeList();
            const level = Math.min(4, heading[1].length + 2);
            html.push(`<h${level}>${inline(heading[2])}</h${level}>`);
            return;
        }
        closeList();
        html.push(`<p>${inline(trimmed)}</p>`);
    });
    closeList();

    let result = html.join('');
    blocks.forEach((block, index) => {
        result = result.replace(`\u0000BLOCK${index}\u0000`, block);
    });
    return result;
}

function inline(text) {
    return text
        .replace(/`([^`]+)`/g, '<code>$1</code>')
        .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
        .replace(/(^|\s)\*([^*]+)\*/g, '$1<em>$2</em>');
}

function extractProposalBlock(content) {
    const matches = [...String(content).matchAll(/```json\s*([\s\S]*?)```/gi)];
    for (const match of matches) {
        try {
            const parsed = JSON.parse(match[1]);
            if (parsed && (Array.isArray(parsed.proposals) || Array.isArray(parsed.findings))) {
                return { parsed, raw: match[0] };
            }
        } catch (_) {
            // A malformed block is shown as code; nothing else to do.
        }
    }
    return null;
}

function loadProposalJournal() {
    try {
        const raw = localStorage.getItem(PROPOSALS_STORAGE_KEY);
        return raw ? JSON.parse(raw) : [];
    } catch (_) {
        return [];
    }
}

function saveProposalJournal(entries) {
    try {
        localStorage.setItem(PROPOSALS_STORAGE_KEY, JSON.stringify(entries.slice(-100)));
    } catch (_) {
        // Journal is a convenience, not a requirement.
    }
}

function journalSummary() {
    const entries = loadProposalJournal().slice(-8);
    if (!entries.length) return '';
    const lines = entries.map((entry) => {
        const verdict = entry.accepted ? 'принято' : 'не принято';
        return `- ${entry.parameterKey}: ${entry.previousValue} → ${entry.suggestedValue} (${verdict})`;
    });
    return `Журнал прошлых предложений (для контекста, не выдумывай эффект без данных):\n${lines.join('\n')}`;
}

async function buildSystemMessage() {
    let pack = null;
    try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        if (response.ok) pack = buildStatePack(await response.json());
    } catch (_) {
        pack = null;
    }

    const parts = [SYSTEM_RULES];
    if (pack) {
        parts.push(`Текущее состояние установки (JSON):\n${JSON.stringify(pack)}`);
    } else {
        parts.push('Текущее состояние получить не удалось: используй инструмент get_status.');
    }
    const journal = journalSummary();
    if (journal) parts.push(journal);
    return parts.join('\n\n');
}

function trimHistory(history) {
    // Keep the visible tail; tool payloads are dropped first because they are
    // the biggest and the least needed after the answer is produced.
    const trimmed = history.slice(-MAX_HISTORY_MESSAGES);
    let budget = 60000;
    const kept = [];
    for (let i = trimmed.length - 1; i >= 0; i -= 1) {
        const message = trimmed[i];
        const size = JSON.stringify(message).length;
        if (size > budget && kept.length) break;
        budget -= size;
        kept.unshift(message);
    }
    return kept;
}

function renderMessage(role, content, { proposalBlock = null } = {}) {
    const nodes = state.elements;
    const wrap = el('div', `ai-msg ai-msg-${role}`);
    const bubble = el('div', 'ai-bubble');
    if (role === 'assistant') {
        bubble.innerHTML = renderMarkdown(content);
        if (proposalBlock) {
            bubble.appendChild(renderProposals(proposalBlock, content));
        }
    } else {
        bubble.textContent = content;
    }
    wrap.appendChild(bubble);
    nodes.messages.appendChild(wrap);
    nodes.messages.scrollTop = nodes.messages.scrollHeight;
    return wrap;
}

function renderProposals(parsed, sourceText) {
    const container = el('div', 'ai-proposals');
    (parsed.findings || []).forEach((finding) => {
        const item = el('div', 'ai-finding');
        item.appendChild(el('strong', '', finding.code ? String(finding.code) : 'наблюдение'));
        item.appendChild(el('p', '', String(finding.detail || '')));
        if (finding.evidence) item.appendChild(el('small', '', `Основание: ${finding.evidence}`));
        container.appendChild(item);
    });

    (parsed.proposals || []).forEach((proposal) => {
        const card = el('div', 'ai-proposal');
        const head = el('div', 'ai-proposal-head');
        head.appendChild(el('span', 'ai-proposal-key', String(proposal.parameterKey || 'параметр')));
        head.appendChild(el('span', 'ai-proposal-delta',
            `${Number(proposal.previousValue)} → ${Number(proposal.suggestedValue)}`));
        card.appendChild(head);
        if (proposal.expectedEffect) card.appendChild(el('p', '', String(proposal.expectedEffect)));
        if (proposal.evidence) card.appendChild(el('small', '', `Основание: ${proposal.evidence}`));
        if (Number.isFinite(Number(proposal.confidence))) {
            card.appendChild(el('small', 'ai-confidence', `уверенность: ${Math.round(Number(proposal.confidence) * 100)} %`));
        }

        const accept = el('button', 'btn btn-primary ai-accept', 'Принять в журнал');
        accept.type = 'button';
        accept.addEventListener('click', () => {
            const journal = loadProposalJournal();
            journal.push({
                at: Date.now(),
                parameterKey: proposal.parameterKey,
                previousValue: proposal.previousValue,
                suggestedValue: proposal.suggestedValue,
                expectedEffect: proposal.expectedEffect || '',
                confidence: proposal.confidence ?? null,
                evidence: proposal.evidence || '',
                accepted: true
            });
            saveProposalJournal(journal);
            accept.textContent = 'В журнале';
            accept.disabled = true;
            card.classList.add('ai-proposal-accepted');
        });
        card.appendChild(accept);
        container.appendChild(card);
    });

    if (sourceText) {
        container.dataset.sourceLength = String(sourceText.length);
    }
    return container;
}

function scrollToMessageStart(node) {
    const list = state.elements.messages;
    if (!list || !node) return;
    const listRect = list.getBoundingClientRect();
    const nodeRect = node.getBoundingClientRect();
    // Bottom-anchored scrolling cuts the first line of a long answer on a
    // phone; put the answer's top edge just below the list edge instead.
    const delta = nodeRect.top - listRect.top - 8;
    list.scrollTop += delta;
}

function renderToolNote(text) {
    const node = el('div', 'ai-tool-note', text);
    state.elements.messages.appendChild(node);
    state.elements.messages.scrollTop = state.elements.messages.scrollHeight;
    return node;
}

function setBusy(busy, label = '') {
    state.busy = busy;
    const nodes = state.elements;
    nodes.send.disabled = busy;
    nodes.input.disabled = busy;
    nodes.status.textContent = label;
    nodes.status.classList.toggle('ai-status-active', busy);
    nodes.stop?.classList.toggle('ai-stop-visible', busy);
    if (busy) {
        nodes.typing.hidden = false;
    } else {
        nodes.typing.hidden = true;
    }
}

async function requestAnswer(userText) {
    state.history.push({ role: 'user', content: userText });
    renderMessage('user', userText);
    document.getElementById('ai')?.classList.add('ai-has-chat');

    if (!state.config?.baseUrl || !state.config?.model) {
        renderMessage('assistant', 'Провайдер и модель ещё не выбраны. Откройте «⚙️ Настройки», укажите адрес OpenAI-совместимого API, ключ и модель — после этого можно спрашивать.');
        setBusy(false, 'Нужна настройка');
        saveChat();
        return;
    }

    state.aborter = new AbortController();
    setBusy(true, 'Думаю…');

    try {
        for (let round = 0; round < MAX_ROUNDS; round += 1) {
            const systemMessage = { role: 'system', content: await buildSystemMessage() };
            const messages = [systemMessage, ...trimHistory(state.history)];
            const { message, usage } = await chatCompletion(state.config, messages, {
                signal: state.aborter.signal,
                tools: TOOL_DEFINITIONS
            });

            const assistantMessage = {
                role: 'assistant',
                content: message.content || ''
            };
            if (Array.isArray(message.tool_calls) && message.tool_calls.length) {
                assistantMessage.tool_calls = message.tool_calls;
            }
            state.history.push(assistantMessage);

            if (!assistantMessage.tool_calls) {
                const block = extractProposalBlock(assistantMessage.content);
                const displayText = block
                    ? assistantMessage.content.replace(block.raw, '').trim()
                    : assistantMessage.content;
                const answerNode = renderMessage('assistant', displayText, { proposalBlock: block?.parsed || null });
                scrollToMessageStart(answerNode);
                saveChat();
                const tokens = usage ? ` · ${usage.total_tokens ?? usage.totalTokens ?? '?'} токенов` : '';
                setBusy(false, `Готово${tokens}`);
                return;
            }

            setBusy(true, 'Смотрю данные…');
            for (const call of assistantMessage.tool_calls) {
                const name = call.function?.name;
                let args = {};
                try {
                    args = call.function?.arguments ? JSON.parse(call.function.arguments) : {};
                } catch (_) {
                    args = {};
                }
                let resultText;
                try {
                    const result = await runTool(name, args);
                    resultText = result.text;
                    renderToolNote(`🔧 ${toolSummary(name, args, result, null)}`);
                } catch (error) {
                    resultText = JSON.stringify({ error: String(error?.message || error) });
                    renderToolNote(`⚠️ ${toolSummary(name, args, null, error)}`);
                }
                state.history.push({
                    role: 'tool',
                    tool_call_id: call.id,
                    content: resultText
                });
            }
        }

        renderMessage('assistant', 'Не удалось получить финальный ответ: модель превысила лимит шагов с инструментами. Уточните вопрос.');
        setBusy(false, 'Остановлено');
    } catch (error) {
        if (error?.name === 'AbortError') {
            renderMessage('assistant', 'Запрос отменён.');
            setBusy(false, 'Отменено');
            return;
        }
        renderMessage('assistant', `Ошибка: ${describeProviderError(error)}`);
        setBusy(false, 'Ошибка');
    } finally {
        state.aborter = null;
        saveChat();
    }
}

function saveChat() {
    try {
        const visible = state.history
            .filter((message) => message.role === 'user' || (message.role === 'assistant' && !message.tool_calls))
            .slice(-30);
        window.sessionStorage.setItem(CHAT_STORAGE_KEY, JSON.stringify(visible));
    } catch (_) {
        // Storage may be full or disabled; chat still works in memory.
    }
}

function restoreChat() {
    try {
        const raw = window.sessionStorage.getItem(CHAT_STORAGE_KEY);
        if (!raw) return;
        const messages = JSON.parse(raw);
        if (!Array.isArray(messages) || !messages.length) return;
        state.history = messages;
        messages.forEach((message) => {
            if (message.role === 'assistant') {
                const block = extractProposalBlock(message.content);
                const text = block ? message.content.replace(block.raw, '').trim() : message.content;
                renderMessage('assistant', text, { proposalBlock: block?.parsed || null });
            } else {
                renderMessage('user', message.content);
            }
        });
    } catch (_) {
        state.history = [];
    }
}

export function initChat(elements, config) {
    state.elements = elements;
    state.config = config;
    restoreChat();
    if (!state.history.length) {
        renderMessage('assistant', 'Здравствуйте. Я вижу текущее состояние установки и историю прогонов. Спросите, что происходит сейчас, что показал последний прогон или что стоит улучшить. Управлять нагревом и насосом я не могу — только объясняю и предлагаю.');
    }
}

export function updateChatConfig(config) {
    state.config = config;
}

export function isBusy() {
    return state.busy;
}

export function sendMessage(text) {
    const value = String(text || '').trim();
    if (!value || state.busy) return;
    requestAnswer(value).catch((error) => {
        console.error('AI chat failed', error);
        setBusy(false, 'Ошибка');
    });
}

export function abortRequest() {
    if (state.aborter) {
        state.aborter.abort();
    }
}

export function clearChat() {
    state.history = [];
    window.sessionStorage.removeItem(CHAT_STORAGE_KEY);
    state.elements.messages.innerHTML = '';
    document.getElementById('ai')?.classList.remove('ai-has-chat');
    renderMessage('assistant', 'История чата очищена. Текущее состояние и прогоны по-прежнему доступны.');
}
