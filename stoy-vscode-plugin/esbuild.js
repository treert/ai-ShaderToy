const esbuild = require('esbuild');

const isWatch = process.argv.includes('--watch');

/** @type {import('esbuild').BuildOptions} */
const sharedOptions = {
    bundle: true,
    minify: !isWatch,
    sourcemap: isWatch,
    platform: 'node',
    target: 'es2020',
    format: 'cjs',
    external: ['vscode'],
};

async function build() {
    const contexts = await Promise.all([
        esbuild.context({
            ...sharedOptions,
            entryPoints: ['./src/extension.ts'],
            outfile: './out/client.js',
        }),
        esbuild.context({
            ...sharedOptions,
            entryPoints: ['./src/server/server.ts'],
            outfile: './out/server.js',
        }),
    ]);

    if (isWatch) {
        console.log('Watching for changes...');
        await Promise.all(contexts.map(ctx => ctx.watch()));
    } else {
        await Promise.all(contexts.map(ctx => ctx.rebuild()));
        await Promise.all(contexts.map(ctx => ctx.dispose()));
        console.log('Build complete.');
    }
}

build().catch((err) => {
    console.error(err);
    process.exit(1);
});
