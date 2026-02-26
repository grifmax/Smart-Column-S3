import * as esbuild from 'esbuild';

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
    // CSS: main.css → data/main.css, переименуем после сборки
};

if (isWatch) {
    const ctx = await esbuild.context(opts);
    await ctx.watch();
    console.log('[WATCH] Watching src/web/ for changes...');
} else {
    esbuild.buildSync(opts);
    // Переименовать выходные файлы: main.js → app.js, main.css → style.css
    const fs = await import('fs');
    fs.renameSync('data/main.js', 'data/app.js');
    fs.renameSync('data/main.css', 'data/style.css');
    const jsSize = fs.statSync('data/app.js').size;
    const cssSize = fs.statSync('data/style.css').size;
    console.log(`[OK] Built data/app.js (${(jsSize / 1024).toFixed(1)} KB)`);
    console.log(`[OK] Built data/style.css (${(cssSize / 1024).toFixed(1)} KB)`);
}
