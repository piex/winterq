console.log("---- test8.js ----");
function assert(b, str) {
	if (b) {
		return;
	}
	console.error(`assertion failed: ${str}`);
}

assert(typeof Headers === "function", "Headers not a function");

const h = new Headers();

try {
	h.append("a");
} catch (error) {
	assert(error.message === "append requires at least 2 arguments");
}

h.append("Content-Type", "image/jpeg");

assert(h.get("Content-Type") === "image/jpeg");

h.append("Accept-Encoding", "deflate");
h.append("Accept-Encoding", "gzip");

assert(h.get("Accept-Encoding") === "deflate, gzip");

assert(h.has("Content-Type") === true);

h.delete("Content-Type");

assert(h.has("Content-Type") === false);

h.set("Accept-Encoding", "br");

assert(h.get("Accept-Encoding") === "br");

const h2 = new Headers({
	"Set-Cookie": "name1=value1",
});

h2.append("Set-Cookie", "name2=value2");

const cookies = h2.getSetCookie();

assert(Array.isArray(cookies));
assert(cookies.length === 2);
assert(cookies[0] === "name1=value1");
assert(cookies[1] === "name2=value2");

const h3 = new Headers();
h3.append("Content-Type", "application/json");
h3.append("Authorization", "Bearer token");
h3.append("X-Custom-Header", "v1");
h3.append("X-Custom-Header", "v2");

const result = {};

h3.forEach((value, key) => {
	result[key] = value;
});

assert(result['Content-Type'] === "application/json");
assert(result['Authorization'] === "Bearer token");
assert(result['X-Custom-Header'] === "v1, v2");

console.log("---- test8.js end----");
