console.log("==== test6.js ====");
const delay = (durationMs) => {
	return new Promise((resolve) => setTimeout(resolve, durationMs));
};

console.log("====test6.js before delay====");

Promise.resolve().then(() => {
	console.log("====test6.js then====");
});

console.log("====test6.js after resolve====");

await delay(100);
console.log("====test6.js delay 100====");

await delay(100);
console.log("====test6.js delay 200====");

await delay(100);
console.log("====test6.js delay 300====");
