console.log("==== test2.js ====");
function assert(b, str) {
	if (b) {
		return;
	}
	throw Error(`assertion failed: ${str}`);
}

assert(typeof globalThis.a === "undefined");

globalThis.a = 2;

assert(globalThis.a === 2);

setTimeout(() => {
	console.log("==== test2.js timeout ====");
	globalThis.a = 2.2;
	assert(globalThis.a === 2.2);
}, 100);
