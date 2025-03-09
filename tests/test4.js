console.log("==== test4.js ====");
function assert(b, str) {
	if (b) {
		return;
	}
	throw Error(`assertion failed: ${str}`);
}

assert(typeof globalThis.a === "undefined");

globalThis.a = 4;

assert(globalThis.a === 4);

setTimeout(() => {
	console.log("==== test4.js timeout ====");
	globalThis.a = 4.4;
	assert(globalThis.a === 4.4);
}, 220);
