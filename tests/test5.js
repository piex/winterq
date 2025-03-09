console.log("==== test5.js ====");
const id1 = setTimeout(() => {
	console.log("Timer 1 expired.");
}, 1000);

const id2 = setTimeout(() => {
	console.log("Timer 2 expired.");
}, 2000);

const id3 = setTimeout(() => {
	throw Error("This should not be printed.");
}, 3000);

clearTimeout(id3);

console.log("Timers scheduled");
