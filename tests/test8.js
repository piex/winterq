function assert(b, str) {
	if (b) {
		return;
	}
	console.error(`assertion failed: ${str}`);
}

assert(typeof Headers === 'function', "Headers not a function");

const h = new Headers();

try {
  h.append('a');
} catch (error) {
  assert(error.message === 'append requires at least 2 arguments');
}

h.append("Content-Type", "image/jpeg");

assert(h.get("Content-Type") === 'image/jpeg');

h.append("Accept-Encoding", "deflate");
h.append("Accept-Encoding", "gzip");

assert(h.get("Accept-Encoding") === "deflate, gzip"); 

assert(h.has('Content-Type') === true);

h.delete('Content-Type');

assert(h.has('Content-Type') === false);

h.set("Accept-Encoding", "br");

console.log('----------start----------');
console.log(h.get("Accept-Encoding"));
console.log('===========end===========');

assert(h.get("Accept-Encoding") === "br");

console.info('------test 8 end-----------');