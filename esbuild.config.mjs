import * as esbuild from 'esbuild';

const isWatch = process.argv.includes('--watch');

const opts = {
    entryPoints: ['src/web/main.js'],
    bundle: true,
    minify: !isWatch,
    sourcemap: isWatch ? 'inline' : false,
    outfile: 'data/app.js',
    target: ['es2020'],
    charset: 'utf8',
};

if (isWatch) {
    const ctx = await esbuild.context(opts);
    await ctx.watch();
    console.log('👀 Watching src/web/ for changes...');
} else {
    const result = esbuild.buildSync(opts);
    const fs = await import('fs');
    const stat = fs.statSync('data/app.js');
    console.log(`✅ Built data/app.js (${(stat.size / 1024).toFixed(1)} KB)`);
}
