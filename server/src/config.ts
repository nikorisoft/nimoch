import config from "config";
import path from "path";

const configValues = {
    server: {
        host: config.get<string>("server.host"),
        port: config.get<number>("server.port")
    },
    projects: {
        directory: path.resolve(__dirname, config.get<string>("projects.directory"))
    }
};

export default configValues;
