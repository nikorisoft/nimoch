/**
 * @typedef {Object} DefaultSceneOptions
 * @property {string} animationSelector Selector for animating elements
 * @property {InputConfig[]} inputs
 * @property {AnimationConfig[]} animations
 */
/**
 * @typedef {Object} InputConfig
 * @property {string} selector Selector for the image element
 * @property {string} name Name of the input stream
 * @property {string} inputOffset Offset in the input stream
 * @property {string?} duration Duration of the input stream
 * @property {string} sceneOffset Start offset in the scene
 */
/**
 * @typedef {Object} AnimationConfig
 * @property {string} selector Selector for the image element
 * @property {string} startTime Start time
 * @property {string?} stopTime Stop time
 * @property {string?} name Animation name (CSS)
 */
/**
 * @type {DefaultSceneOptions} Default scene options
 */
const options = {
    animationSelector: "",
    inputs: [],
    animations: []
};
let rendererUUID = ""; // Use it to call API
let rendererFPS = { num: 30000, den: 1001 };
let frameDuration = 2002;

const regexTime = /([0-9.]+)\s*(s|ms|us|f|)/;
/**
 * Parse string time to a specified unit
 * @param {string} input Input time
 * @param {string} outputUnit Output time unit
 * @returns {number}
 */
function nimochTime(input, outputUnit = "") {
    // Input -> inernal time stamp (1/60000)
    const m = regexTime.exec(input);
    if (m == null) {
        throw new Error("Failed to parse time string: " + input);
    }
    const num = parseFloat(m[1]);
    const unit = m[2];

    let time = num;

    switch (unit) {
        case "s":
            time *= 60000;
            break;
        case "ms":
            time *= 60;
            break;
        case "us":
            time *= 60.0 / 1000.0;
            break;
        case "f":
            time = time * 1001;
            break;
    }

    switch (outputUnit) {
        case "s":
            return time / 60000;
        case "ms":
            return time / 60;
        case "us":
            return time * 1000.0 / 60.0;
        case "f":
            return time / 1001;
        default:
            return Math.round(time);
    }
}

/**
 * @typedef {Object} Rational
 * @property {number} den Denominator
 * @property {number} num Number
 */

/**
 * @typedef {Object} InternalInputConfig
 * @property {string} selector Selector for the image element
 * @property {string} name Name of the input stream
 * @property {number} inputOffset Offset in the input stream
 * @property {number?} duration Duration of the input stream
 * @property {number} sceneOffset Start offset in the scene
 * @property {string | null} blob Blob for the last image
 */
/**
 * @typedef {Object} InternalAnimationConfig
 * @property {string} selector Selector for the image element
 * @property {number} startTime Start time
 * @property {number?} stopTime Stop time
 * @property {string?} name Animation name (CSS)
 */

/**
 * @type {InternalInputConfig[]}
 */
const inputs = [];
/**
 * @type {InternalAnimationConfig[]}
 */
const animations = [];

/**
 * Initialize the scene
 * @param {string} uuid UUID for renderer
 * @param {Rational} fps
 */
function init(uuid, fps) {
    const elements = document.querySelectorAll(options.animationSelector);

    for (const e of elements) {
        e.style.animationPlayState = "paused";
    }

    rendererUUID = uuid;
    rendererFPS = fps;
    frameDuration = Math.floor(60000 * fps.den / fps.num);

    for (const i of options.inputs) {
        const internal = {
            selector: i.selector,
            name: i.name,
            inputOffset: nimochTime(i.inputOffset),
            sceneOffset: nimochTime(i.sceneOffset),
            blob: null
        };
        if (i.duration != null) {
            internal.duration = nimochTime(i.duration);
        }
        inputs.push(internal);
    }

    for (const a of options.animations) {
        const internal = {
            selector: a.selector,
            name: a.name,
            startTime: nimochTime(a.startTime),
        };
        if (a.stopTime != null) {
            internal.stopTime = nimochTime(a.stopTime);
        }

        animations.push(internal);
    }

    console.log("Scene initialized:", uuid, fps.num, fps.den);
}

async function showFrame(time) {
    const timeInTimeBase = nimochTime(time);
    const ms = nimochTime(timeInTimeBase, "ms");

    console.debug("showFrame", time, ms);

    for (const i of inputs) {
        if (time < i.sceneOffset) {
            continue;
        }
        if (i.duration != null && time >= i.sceneOffset + i.duration) {
            continue;
        }
        const pts = (time - i.sceneOffset) + i.inputOffset;

        const image = await loadImage(i.name, pts);
        const blob = new Blob([image], { type: "image/png"} );
        const url = URL.createObjectURL(blob);

        if (i.blob != null) {
            URL.revokeObjectURL(i.blob);
            i.blob = null;
        }

        const imageElements = document.querySelectorAll(i.selector);
        for (const e of imageElements) {
            e.src = url;
        }

        i.blob = url;
    }

    if (animations.length === 0) {
        const elements = document.querySelectorAll(options.animationSelector);
        for (const e of elements) {
            e.style.animationDelay = `-${ms.toString()}ms`;
        }
    } else {
        for (const a of animations) {
            if (time < a.startTime || (a.stopTime != null && time > a.stopTime)) {
                continue;
            }
            const elements = document.querySelectorAll(a.selector);
            const ms = nimochTime((time - a.startTime).toString(), "ms");

            for (const e of elements) {
                if (a.name != null) {
                    e.style.animationName = a.name;
                }
                e.style.animationDelay = `-${ms.toString()}ms`;
            }
        }
    }
}

function play() {

}

/**
 * Load decoder image
 * @param {string} name Name of input
 * @param {string} time Time
 * @returns {Promise<ArrayBuffer>}
 */
async function loadImage(name, time) {
    const response = await fetch(`/stream/${rendererUUID}/${name}/${time}`);

    return response.arrayBuffer();
}
