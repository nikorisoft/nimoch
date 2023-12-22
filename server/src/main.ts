import Fastify from "fastify";
import FastifyStatic from "@fastify/static";

import NimochConfig from "./config";
import NimochWebsocket from "./renderer/websocket";
import path from "path";

async function main() {
    const fastify = Fastify({
        logger: true
    });

    fastify.register(FastifyStatic, {
        root: NimochConfig.projects.directory,
        prefix: "/projects",
        decorateReply: false
    });
    fastify.register(FastifyStatic, {
        root: path.resolve(__dirname, "..", "..", "lib"),
        prefix: "/lib"
    });

    fastify.register(NimochWebsocket, {
        prefix: "/websocket"
    });

    await fastify.listen({
        port: NimochConfig.server.port,
        host: NimochConfig.server.host
    });

    return 0;
}

main().then((ret) => {

}, (err) => {
    console.error(err);
    process.exit(1);
});
