function assert(b, str) {
	if (b) {
		return;
	}
	throw Error(`assertion failed: ${str}`);
}
// console.log(typeof globalThis.a);
assert(typeof globalThis.a === "undefined");

globalThis.a = 1;

assert(globalThis.a === 1);

setTimeout(() => {
	console.log("---- test1.js timeout ----");
	globalThis.a = 1.1;
	assert(globalThis.a === 1.1);
}, 200);
