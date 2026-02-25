/**
 * Скрипт разбивки style.css на модули
 * Запуск: node scripts/split-css.mjs
 */
import fs from 'fs';
import path from 'path';

const SRC = 'data/style.css';
const OUT = 'src/web/styles';

const lines = fs.readFileSync(SRC, 'utf-8').replace(/\r\n/g, '\n').split('\n');

const modules = [
    // Переменные и тема
    ['_variables.css', 1, 29],
    ['_theme-dark.css', 30, 56],
    // Base layout
    ['_base.css', 57, 89],
    // Status bar, nav
    ['_status-bar.css', 90, 168],
    // Top menu dropdown
    ['_menu.css', 169, 243],
    // Operator screen (Instrument + Compact)
    ['_operator-screen.css', 244, 596],
    ['_operator-scheme.css', 597, 656],
    ['_operator-instrument.css', 657, 717],
    ['_operator-compact.css', 718, 786],
    // Landing
    ['_landing.css', 787, 1002],
    // Cards, values, modes, metrics
    ['_cards.css', 1003, 1096],
    // Buttons
    ['_buttons.css', 1097, 1170],
    // Controls, forms
    ['_forms.css', 1171, 1254],
    // Logs
    ['_logs.css', 1255, 1296],
    // Responsive
    ['_responsive.css', 1297, 1430],
    // Scrollbar
    ['_scrollbar.css', 1431, 1448],
    // History
    ['_history.css', 1449, 1556],
    // Modal
    ['_modal.css', 1557, 1744],
];

fs.mkdirSync(OUT, { recursive: true });

let totalBytes = 0;
const imports = [];
modules.forEach(([file, startLine, endLine]) => {
    const chunk = lines.slice(startLine - 1, endLine);
    while (chunk.length && chunk[0].trim() === '') chunk.shift();
    while (chunk.length && chunk[chunk.length - 1].trim() === '') chunk.pop();
    const content = chunk.join('\n') + '\n';
    const outPath = path.join(OUT, file);
    fs.writeFileSync(outPath, content, 'utf-8');
    totalBytes += content.length;
    imports.push(file);
    console.log(`  ${outPath} (${chunk.length} lines)`);
});

// Создаём main.css с @import
const mainCss = imports.map(f => `@import './${f}';`).join('\n') + '\n';
fs.writeFileSync(path.join(OUT, 'main.css'), mainCss, 'utf-8');
console.log(`\n  ${path.join(OUT, 'main.css')} (entry point)`);
console.log(`\n✅ Created ${modules.length} CSS modules (${(totalBytes / 1024).toFixed(1)} KB total)`);
