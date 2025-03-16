console.log("---- test3.js ----");
function assert(b, str) {
	if (b) {
		return;
	}
	throw Error(`assertion failed: ${str}`);
}

assert(typeof globalThis.a === "undefined");

globalThis.a = 3;

assert(globalThis.a === 3);

setTimeout(() => {
	console.log("---- test3.js timeout ----");
	globalThis.a = 3.3;
	assert(globalThis.a === 3.3);
}, 150);
