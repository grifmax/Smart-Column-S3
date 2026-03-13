import * as esbuild from 'esbuild';
import fs from 'fs';
import { gzipSync } from 'zlib';

const isWatch = process.argv.includes('--watch');
const appVersion = readFirmwareVersion();

const opts = {
    entryPoints: [
        'src/web/main.js',
        'src/web/styles/main.css',
    ],
    bundle: true,
    minify: !isWatch,
    sourcemap: isWatch ? 'inline' : false,
    outdir: 'data',
    entryNames: '[name]',
    target: ['es2020'],
    charset: 'utf8',
    define: {
        __APP_VERSION__: JSON.stringify(appVersion),
    },
};

function readFirmwareVersion() {
    const configText = fs.readFileSync('src/config.h', 'utf8');
    const match = configText.match(/#define\s+FIRMWARE_VERSION\s+"([^"]+)"/);
    if (!match) {
        throw new Error('Unable to read firmware version from src/config.h');
    }
    return match[1];
}

function replaceAll(text, replacements) {
    let next = text;
    for (const [pattern, value] of replacements) {
        next = next.replace(pattern, value);
    }
    return next;
}

function stampStaticAssetVersions() {
    const htmlFiles = ['data/index.html', 'data/charts.html', 'data/logs.html', 'data/calibration.html'];
    const htmlReplacements = [
        [/href="style\.css(?:\?v=[^"]*)?"/g, `href="style.css?v=${appVersion}"`],
        [/href="charts-style\.css(?:\?v=[^"]*)?"/g, `href="charts-style.css?v=${appVersion}"`],
        [/src="app\.js(?:\?v=[^"]*)?"/g, `src="app.js?v=${appVersion}"`],
        [/src="charts\.js(?:\?v=[^"]*)?"/g, `src="charts.js?v=${appVersion}"`],
        [/data="schemes\/column-tesla\.svg(?:\?v=[^"]*)?"/g, `data="schemes/column-tesla.svg?v=${appVersion}"`],
    ];

    for (const file of htmlFiles) {
        if (!fs.existsSync(file)) continue;
        const source = fs.readFileSync(file, 'utf8');
        const stamped = replaceAll(source, htmlReplacements);
        fs.writeFileSync(file, stamped, 'utf8');
    }

    const swFile = 'data/service-worker.js';
    if (fs.existsSync(swFile)) {
        const source = fs.readFileSync(swFile, 'utf8');
        const stamped = source.replace(/__APP_VERSION__/g, appVersion);
        fs.writeFileSync(swFile, stamped, 'utf8');
    }
}

function gzipBuildArtifacts() {
    const targets = ['app.js', 'style.css', 'index.html'];
    for (const file of targets) {
        const srcPath = `data/${file}`;
        const gzPath = `${srcPath}.gz`;
        const source = fs.readFileSync(srcPath);
        const compressed = gzipSync(source, { level: 9 });
        fs.writeFileSync(gzPath, compressed);
    }
}

if (isWatch) {
    const ctx = await esbuild.context(opts);
    await ctx.watch();
    console.log('[WATCH] Watching src/web/ for changes...');
} else {
    esbuild.buildSync(opts);
    fs.renameSync('data/main.js', 'data/app.js');
    fs.renameSync('data/main.css', 'data/style.css');
    stampStaticAssetVersions();
    gzipBuildArtifacts();

    const jsSize = fs.statSync('data/app.js').size;
    const cssSize = fs.statSync('data/style.css').size;
    const jsGzSize = fs.statSync('data/app.js.gz').size;
    const cssGzSize = fs.statSync('data/style.css.gz').size;
    console.log(`[OK] Built data/app.js (${(jsSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/style.css (${(cssSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/app.js.gz (${(jsGzSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/style.css.gz (${(cssGzSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Stamped static assets with version ${appVersion}`);
}
