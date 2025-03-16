console.log("---- test5.js ----");
const id1 = setTimeout(() => {
	console.log("----test5.js Timer 1 expired ----");
}, 1000);

const id2 = setTimeout(() => {
	console.log("----test5.js Timer 2 expired ----");
}, 2000);

const id3 = setTimeout(() => {
	console.error("----test5.js This should not be printed ----");
}, 3000);

clearTimeout(id3);

console.log("----test5.js Timers scheduled ----");
