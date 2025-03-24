console.info("--------start---------");

console.info("--------Event---------");

console.info("NONE: ", Event.NONE);
console.info("CAPTURING_PHASE: ", Event.CAPTURING_PHASE);
console.info("AT_TARGET: ", Event.AT_TARGET);
console.info("BUBBLING_PHASE: ", Event.BUBBLING_PHASE);


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
console.info(e1 instanceof Event);
console.info(e1.type);

const event = new Event('test', { bubbles: true, cancelable: true });

console.info("--------CustomEvent---------");

console.info("NONE: ", CustomEvent.NONE);
console.info("CAPTURING_PHASE: ", CustomEvent.CAPTURING_PHASE);
console.info("AT_TARGET: ", CustomEvent.AT_TARGET);
console.info("BUBBLING_PHASE: ", CustomEvent.BUBBLING_PHASE);


const ce1 = new CustomEvent("c-tt");
console.info(ce1 instanceof CustomEvent);
console.info(ce1 instanceof Event);
console.info(ce1.type);

const customEvent = new CustomEvent('custom', {
  bubbles: true,
  detail: { message: 'Hello from CustomEvent' }
});

console.info("--------EventTarget---------");

const target = new EventTarget();
console.info(target instanceof EventTarget);

// target.addEventListener('test', (event) => {
//   console.log('Event type:', event.type);
//   console.log('Target:', event.target === target);
//   console.log('Current target:', event.currentTarget === target);
//   console.log('Event phase:', event.eventPhase);
//   console.log('Bubbles:', event.bubbles);
//   console.log('Cancelable:', event.cancelable);
//   console.log('Timestamp:', event.timeStamp);
// });

console.info("--------end---------");
