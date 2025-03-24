try {
  const e1 = new Event();
  console.log('----------start----------');
  console.log(e1);
  console.log('===========end===========');
} catch (error) {
  console.log('----------start----------');
  console.error(error);
  console.log('===========end===========');
}
const e1 = new Event("tt");
console.log('----------start----------');
console.log(e1);
console.log('===========end===========');
console.info(e1 instanceof Event);

const ce1 = new CustomEvent("tt");
console.log('----------start----------');
console.log(ce1);
console.log('===========end===========');
console.info(ce1 instanceof CustomEvent);
console.info(ce1 instanceof Event);

console.info("--------end---------");
