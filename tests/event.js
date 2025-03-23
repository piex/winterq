try {
  const e1 = new Event();
  console.log('----------start----------');
  console.log(e1);
  console.log('===========end===========');
} catch (error) {
  console.error(error);
}
const e1 = new Event("tt");
console.log('----------start----------');
console.log(e1);
console.log('===========end===========');
console.info(e1 instanceof Event);

