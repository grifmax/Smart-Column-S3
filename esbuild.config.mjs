import * as esbuild from 'esbuild';
import fs from 'fs';
import { gzipSync } from 'zlib';

const isWatch = process.argv.includes('--watch');

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
};

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
    gzipBuildArtifacts();

    const jsSize = fs.statSync('data/app.js').size;
    const cssSize = fs.statSync('data/style.css').size;
    const jsGzSize = fs.statSync('data/app.js.gz').size;
    const cssGzSize = fs.statSync('data/style.css.gz').size;
    console.log(`[OK] Built data/app.js (${(jsSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/style.css (${(cssSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/app.js.gz (${(jsGzSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/style.css.gz (${(cssGzSize / 1024).toFixed(1)} KB)`);
}
